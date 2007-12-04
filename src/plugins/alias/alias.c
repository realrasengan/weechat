/*
 * Copyright (c) 2003-2007 by FlashCode <flashcode@flashtux.org>
 * See README for License detail, AUTHORS for developers list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* alias.c: Alias plugin for WeeChat */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "../weechat-plugin.h"
#include "alias.h"


static struct t_weechat_plugin *weechat_plugin = NULL;

static struct t_config_file *alias_config_file = NULL;
static struct t_alias *alias_list = NULL;
static struct t_alias *last_alias = NULL;
static struct t_hook *alias_command = NULL;
static struct t_hook *unalias_command = NULL;


/*
 * alias_search: search an alias
 */

static struct t_alias *
alias_search (char *alias_name)
{
    struct t_alias *ptr_alias;
    
    for (ptr_alias = alias_list; ptr_alias;
         ptr_alias = ptr_alias->next_alias)
    {
        if (weechat_strcasecmp (alias_name, ptr_alias->name) == 0)
            return ptr_alias;
    }
    return NULL;
}

/*
 * alias_add_word: add word to string and increment length
 *                 This function should NOT be called directly.
 */

static void
alias_add_word (char **alias, int *length, char *word)
{
    int length_word;

    if (!word)
        return;
    
    length_word = strlen (word);
    if (length_word == 0)
        return;
    
    if (*alias == NULL)
    {
        *alias = (char *) malloc (length_word + 1);
        strcpy (*alias, word);
    }
    else
    {
        *alias = realloc (*alias, strlen (*alias) + length_word + 1);
        strcat (*alias, word);
    }
    *length += length_word;
}

/*
 * alias_replace_args: replace arguments ($1, $2, .. or $*) in alias arguments
 */

static char *
alias_replace_args (char *alias_args, char *user_args)
{
    char **argv, *start, *pos, *res;
    int argc, length_res, args_count;
    
    argv = weechat_string_explode (user_args, " ", 0, 0, &argc);
    
    res = NULL;
    length_res = 0;
    args_count = 0;
    start = alias_args;
    pos = start;
    while (pos && pos[0])
    {
        if ((pos[0] == '\\') && (pos[1] == '$'))
        {
            pos[0] = '\0';
            alias_add_word (&res, &length_res, start);
            alias_add_word (&res, &length_res, "$");
            pos[0] = '\\';
            start = pos + 2;
            pos = start;
        }
        else
        {
            if (pos[0] == '$')
            {
                if (pos[1] == '*')
                {
                    args_count++;
                    pos[0] = '\0';
                    alias_add_word (&res, &length_res, start);
                    alias_add_word (&res, &length_res, user_args);
                    pos[0] = '$';
                    start = pos + 2;
                    pos = start;
                }
                else
                {
                    if ((pos[1] >= '1') && (pos[1] <= '9'))
                    {
                        args_count++;
                        pos[0] = '\0';
                        alias_add_word (&res, &length_res, start);
                        if (pos[1] - '0' <= argc)
                            alias_add_word (&res, &length_res, argv[pos[1] - '1']);
                        pos[0] = '$';
                        start = pos + 2;
                        pos = start;
                    }
                    else
                        pos++;
                }
            }
            else
                pos++;
        }
    }
    
    if (start < pos)
        alias_add_word (&res, &length_res, start);
    
    if ((args_count == 0) && user_args && user_args[0])
    {
        alias_add_word (&res, &length_res, " ");
        alias_add_word (&res, &length_res, user_args);
    }
    
    if (argv)
        weechat_string_free_exploded (argv);
    
    return res;
}

/*
 * alias_cb: callback for alias (called when user uses an alias)
 */

static int
alias_cb (void *data, void *buffer, int argc, char **argv,
          char **argv_eol)
{
    struct t_alias *ptr_alias;
    char **commands, **ptr_cmd, **ptr_next_cmd;
    char *args_replaced, *alias_command;
    int some_args_replaced, length1, length2;
    
    /* make C compiler happy */
    (void) argc;
    (void) argv;
    
    ptr_alias = (struct t_alias *)data;
    
    if (ptr_alias->running)
    {
        weechat_printf (NULL,
                        _("%sError: circular reference when "
                          "calling alias \"/%s\""),
                        weechat_prefix ("error"),
                        ptr_alias->name);
        return PLUGIN_RC_FAILED;
    }
    else
    {		
        /* an alias can contain many commands separated by ';' */
        commands = weechat_string_split_command (ptr_alias->command, ';');
        if (commands)
        {
            some_args_replaced = 0;
            ptr_alias->running = 1;
            for (ptr_cmd = commands; *ptr_cmd; ptr_cmd++)
            {
                ptr_next_cmd = ptr_cmd;
                ptr_next_cmd++;

                args_replaced = (argc > 1) ?
                    alias_replace_args (*ptr_cmd, argv_eol[1]) : NULL;
                if (args_replaced)
                {
                    some_args_replaced = 1;
                    if (*ptr_cmd[0] == '/')
                        weechat_command (buffer, args_replaced);
                    else
                    {
                        alias_command = (char *) malloc (1 + strlen(args_replaced) + 1);
                        if (alias_command)
                        {
                            strcpy (alias_command, "/");
                            strcat (alias_command, args_replaced);
                            weechat_command (buffer, alias_command);
                            free (alias_command);
                        }
                    }
                    free (args_replaced);
                }
                else
                {
                    /* if alias has arguments, they are now
                       arguments of the last command in the list (if no $1,$2,..$*) was found */
                    if ((*ptr_next_cmd == NULL) && argv_eol[1] && (!some_args_replaced))
                    {
                        length1 = strlen (*ptr_cmd);
                        length2 = strlen (argv_eol[1]);
                        
                        alias_command = (char *) malloc ( 1 + length1 + 1 + length2 + 1);
                        if (alias_command)
                        {
                            if (*ptr_cmd[0] != '/')
                                strcpy (alias_command, "/");
                            else
                                strcpy (alias_command, "");
                            
                            strcat (alias_command, *ptr_cmd);
                            strcat (alias_command, " ");
                            strcat (alias_command, argv_eol[1]);
                            
                            weechat_command (buffer, alias_command);
                            free (alias_command);
                        }
                    }
                    else
                    {
                        if (*ptr_cmd[0] == '/')
                            (void) weechat_command(buffer, *ptr_cmd);
                        else
                        {
                            alias_command = (char *) malloc (1 + strlen (*ptr_cmd) + 1);
                            if (alias_command)
                            {
                                strcpy (alias_command, "/");
                                strcat (alias_command, *ptr_cmd);
                                weechat_command (buffer, alias_command);
                                free (alias_command);
                            }
                        }
                    }
                }
            }
            ptr_alias->running = 0;
            weechat_string_free_splitted_command (commands);
        }
    }
    return PLUGIN_RC_SUCCESS;
}

/*
 * alias_new: create new alias and add it to alias list
 */

static struct t_alias *
alias_new (char *name, char *command)
{
    struct t_alias *new_alias, *ptr_alias;
    struct t_hook *new_hook;

    if (!name || !name[0] || !command || !command[0])
        return NULL;
    
    while (name[0] == '/')
    {
	name++;
    }
    
    ptr_alias = alias_search (name);
    if (ptr_alias)
    {
	if (ptr_alias->command)
	    free (ptr_alias->command);
	ptr_alias->command = strdup (command);
	return ptr_alias;
    }
    
    if ((new_alias = ((struct t_alias *) malloc (sizeof (struct t_alias)))))
    {
        new_hook = weechat_hook_command (name, "[alias]", NULL, NULL, NULL,
                                         alias_cb, new_alias);
        if (!new_hook)
        {
            free (new_alias);
            return NULL;
        }
        
        new_alias->hook = new_hook;
        new_alias->name = strdup (name);
        new_alias->command = strdup (command);
	new_alias->running = 0;
        
        new_alias->prev_alias = last_alias;
        new_alias->next_alias = NULL;
        if (alias_list)
            last_alias->next_alias = new_alias;
        else
            alias_list = new_alias;
        last_alias = new_alias;
        
        return new_alias;
    }
    
    return NULL;
}

/*
 * alias_get_final_command: get final command pointed by an alias
 */

static char *
alias_get_final_command (struct t_alias *alias)
{
    struct t_alias *ptr_alias;
    char *result;
    
    if (alias->running)
    {
        weechat_printf (NULL,
                        _("%sError: circular reference when calling alias "
                          "\"/%s\""),
                        weechat_prefix ("error"),
                        alias->name);
        return NULL;
    }
    
    ptr_alias = alias_search ((alias->command[0] == '/') ?
                             alias->command + 1 : alias->command);
    if (ptr_alias)
    {
        alias->running = 1;
        result = alias_get_final_command (ptr_alias);
        alias->running = 0;
        return result;
    }
    return (alias->command[0] == '/') ?
        alias->command + 1 : alias->command;
}

/*
 * alias_free: free an alias and reomve it from list
 */

static void
alias_free (struct t_alias *alias)
{
    struct t_alias *new_alias_list;

    /* remove alias from list */
    if (last_alias == alias)
        last_alias = alias->prev_alias;
    if (alias->prev_alias)
    {
        (alias->prev_alias)->next_alias = alias->next_alias;
        new_alias_list = alias_list;
    }
    else
        new_alias_list = alias->next_alias;
    
    if (alias->next_alias)
        (alias->next_alias)->prev_alias = alias->prev_alias;

    /* free data */
    if (alias->hook)
        weechat_unhook (alias->hook);
    if (alias->name)
        free (alias->name);
    if (alias->command)
        free (alias->command);
    free (alias);
    alias_list = new_alias_list;
}

/*
 * alias_free_all: free all alias
 */

static void
alias_free_all ()
{
    while (alias_list)
        alias_free (alias_list);
}

/*
 * alias_config_read_line: read an alias in configuration file
 */

void
alias_config_read_line (void *config_file, char *option_name, char *value)
{
    /* make C compiler happy */
    (void) config_file;
    
    /* create new alias */
    if (!alias_new (option_name, value))
    {
        weechat_printf (NULL,
                        "%sAlias: error creating alias \"%s\" => \"%s\"",
                        weechat_prefix ("error"),
                        option_name, value);
    }
}

/*
 * alias_config_write_section: write alias section in configuration file
 *                             Return:  0 = successful
 *                                     -1 = write error
 */

void
alias_config_write_section (void *config_file)
{
    struct t_alias *ptr_alias;
    char *string;
    
    for (ptr_alias = alias_list; ptr_alias;
         ptr_alias = ptr_alias->next_alias)
    {
        string = (char *)malloc (strlen (ptr_alias->command) + 4);
        if (string)
        {
            strcpy (string, "\"");
            strcat (string, ptr_alias->command);
            strcat (string, "\"");
            weechat_config_write_line (config_file,
                                       ptr_alias->name,
                                       string);
            free (string);
        }
    }
}

/*
 * alias_config_write_default_aliases: write default aliases in configuration file
 */

void
alias_config_write_default_aliases (void *config_file)
{
    weechat_config_write_line (config_file, "SAY", "\"msg *\"");
    weechat_config_write_line (config_file, "BYE", "\"quit\"");
    weechat_config_write_line (config_file, "EXIT", "\"quit\"");
    weechat_config_write_line (config_file, "SIGNOFF", "\"quit\"");
    weechat_config_write_line (config_file, "C", "\"clear\"");
    weechat_config_write_line (config_file, "CL", "\"clear\"");
    weechat_config_write_line (config_file, "CLOSE", "\"buffer close\"");
    weechat_config_write_line (config_file, "CHAT", "\"dcc chat\"");
    weechat_config_write_line (config_file, "IG", "\"ignore\"");
    weechat_config_write_line (config_file, "J", "\"join\"");
    weechat_config_write_line (config_file, "K", "\"kick\"");
    weechat_config_write_line (config_file, "KB", "\"kickban\"");
    weechat_config_write_line (config_file, "LEAVE", "\"part\"");
    weechat_config_write_line (config_file, "M", "\"msg\"");
    weechat_config_write_line (config_file, "MUB", "\"unban *\"");
    weechat_config_write_line (config_file, "N", "\"names\"");
    weechat_config_write_line (config_file, "Q", "\"query\"");
    weechat_config_write_line (config_file, "T", "\"topic\"");
    weechat_config_write_line (config_file, "UB", "\"unban\"");
    weechat_config_write_line (config_file, "UNIG", "\"unignore\"");
    weechat_config_write_line (config_file, "W", "\"who\"");
    weechat_config_write_line (config_file, "WC", "\"window merge\"");
    weechat_config_write_line (config_file, "WI", "\"whois\"");
    weechat_config_write_line (config_file, "WW", "\"whowas\"");
}

/*
 * alias_config_init: init alias configuration file
 */

static int
alias_config_init ()
{
    struct t_config_section *ptr_section;
    
    alias_config_file = weechat_config_new (ALIAS_CONFIG_FILENAME);
    if (alias_config_file)
    {
        ptr_section = weechat_config_new_section (alias_config_file, "alias",
                                                  alias_config_read_line,
                                                  alias_config_write_section,
                                                  alias_config_write_default_aliases);
        if (ptr_section)
            return 1;
        weechat_config_free (alias_config_file);
    }
    return 0;
}

/*
 * alias_config_read: read alias configuration file
 */

static int
alias_config_read ()
{
    return weechat_config_read (alias_config_file);
}

/*
 * alias_config_reaload: reload alias configuration file
 */

static int
alias_config_reload ()
{
    alias_free_all ();
    return weechat_config_reload (alias_config_file);
}

/*
 * alias_config_write: write alias configuration file
 */

static int
alias_config_write ()
{
    return weechat_config_write (alias_config_file);
}

/*
 * alias_command_cb: display or create alias
 */

static int
alias_command_cb (void *data, void *buffer, int argc, char **argv,
                  char **argv_eol)
{
    char *alias_name;
    struct t_alias *ptr_alias;
    
    /* make C compiler happy */
    (void) data;
    (void) buffer;
    
    if (argc > 1)
    {
        alias_name = (argv[1][0] == '/') ? argv[1] + 1 : argv[1];
        if (argc > 2)
        {
            /* Define new alias */
            if (!alias_new (alias_name, argv_eol[2]))
            {
                weechat_printf (NULL,
                                _("%sAlias: error creating alias \"%s\" "
                                  "=> \"%\""),
                                weechat_prefix ("error"),
                                alias_name, argv_eol[2]);
                return PLUGIN_RC_FAILED;
            }
            weechat_printf (NULL,
                            _("%sAlias \"%s\" => \"%s\" created"),
                            weechat_prefix ("info"),
                            alias_name, argv_eol[2]);
        }
        else
        {
            /* Display one alias */
            ptr_alias = alias_search (alias_name);
	    if (ptr_alias)
	    {
		weechat_printf (NULL, "");
		weechat_printf (NULL, _("Alias:"));
		weechat_printf (NULL, "  %s %s=>%s %s",
                                ptr_alias->name,
                                weechat_color ("color_chat_delimiters"),
                                weechat_color ("color_chat"),
                                ptr_alias->command);
	    }
            else
                weechat_printf (NULL,
                                _("%sNo alias found."),
                                weechat_prefix ("info"));
        }
    }
    else
    {
        /* List all aliases */
        if (alias_list)
        {
            weechat_printf (NULL, "");
            weechat_printf (NULL, _("List of aliases:"));
            for (ptr_alias = alias_list; ptr_alias;
                 ptr_alias = ptr_alias->next_alias)
            {
                weechat_printf (NULL,
                                "  %s %s=>%s %s",
                                ptr_alias->name,
                                weechat_color ("color_chat_delimiters"),
                                weechat_color ("color_chat"),
                                ptr_alias->command);
            }
        }
        else
            weechat_printf (NULL,
                            _("%sNo alias defined."),
                            weechat_prefix ("info"));
    }
    return 0;
}

/*
 * unalias_command_cb: remove an alias
 */

int
unalias_command_cb (void *data, void *buffer, int argc, char **argv,
                    char **argv_eol)
{
    char *alias_name;
    struct t_alias *ptr_alias;
    
    /* make C compiler happy */
    (void) data;
    (void) buffer;
    (void) argv_eol;
    
    if (argc > 1)
    {
        alias_name = (argv[1][0] == '/') ? argv[1] + 1 : argv[1];
        ptr_alias = alias_search (alias_name);
        if (!ptr_alias)
        {
            weechat_printf (NULL,
                            _("%sAlias \"%s\" not found"),
                            weechat_prefix ("error"),
                            alias_name);
            return -1;
        }
        alias_free (ptr_alias);
        weechat_printf (NULL,
                        _("%sAlias \"%s\" removed"),
                        weechat_prefix ("info"),
                        alias_name);
    }
    return 0;
}

/*
 * weechat_plugin_init: initialize alias plugin
 */

int
weechat_plugin_init (struct t_weechat_plugin *plugin)
{
    weechat_plugin = plugin;
    
    if (!alias_config_init ())
    {
        weechat_printf (NULL,
                        "%sAlias: error creating configuration file \"%s\"",
                        weechat_prefix("error"),
                        ALIAS_CONFIG_FILENAME);
        return PLUGIN_RC_FAILED;
    }
    alias_config_read ();
    
    alias_command = weechat_hook_command ("alias",
                                          N_("create an alias for a command"),
                                          N_("[alias_name [command [arguments]]]"),
                                          N_("alias_name: name of alias\n"
                                             "   command: command name (many "
                                             "commands can be separated by "
                                             "semicolons)\n"
                                             " arguments: arguments for "
                                             "command\n\n"
                                             "Note: in command, special "
                                             "variables $1, $2,..,$9 "
                                             "are replaced by arguments given "
                                             "by user, and $* "
                                             "is replaced by all arguments.\n"
                                             "Variables $nick, $channel and "
                                             "$server are "
                                             "replaced by current nick/channel"
                                             "/server."),
                                          "%- %h",
                                          alias_command_cb, NULL);
    
    unalias_command = weechat_hook_command ("unalias", N_("remove an alias"),
                                            N_("alias_name"),
                                            N_("alias_name: name of alias to "
                                               "remove"),
                                            "%h",
                                            unalias_command_cb, NULL);
    
    return PLUGIN_RC_SUCCESS;
}

/*
 * weechat_plugin_end: end alias plugin
 */

int
weechat_plugin_end ()
{
    alias_config_write ();
    alias_free_all ();
    weechat_config_free (alias_config_file);
    weechat_unhook (alias_command);
    weechat_unhook (unalias_command);
    return PLUGIN_RC_SUCCESS;
}
