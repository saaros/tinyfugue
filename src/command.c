/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/*****************************************************************
 * Fugue command handlers
 *****************************************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "command.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "output.h"
#include "macro.h"
#include "keyboard.h"	/* find_key(), find_efunc() */
#include "expand.h"     /* process_macro() */
#include "search.h"
#include "signals.h"    /* suspend(), shell() */
#include "commands.h"

#define ON (!cstrcmp(args, "on"))
#define OFF (!cstrcmp(args, "off"))

char  *current_command = NULL;
TFILE *current_file = NULL;
int    current_lineno = 0;
int    log_on = 0;
int    concat = 0;
int wnmatch = 4, wnlines = 5, wdmatch = 2, wdlines = 5;

extern int restrict;

STATIC_BUFFER(pattern);
STATIC_BUFFER(body);

static int  FDECL(do_watch,(char *args, char *name, int *wlines, int *wmatch, int *flag));
static void FDECL(split_args,(char *args));
static int  FDECL(read_file_commands,(TFILE *file, int tinytalk));
static int  FDECL(do_set,(char *args, int exportflag, int localflag));

static HANDLER (handle_beep_command);
static HANDLER (handle_bind_command);
static HANDLER (handle_cat_command);
static HANDLER (handle_dokey_command);
static HANDLER (handle_echo_command);
static HANDLER (handle_eval_command);
static HANDLER (handle_gag_command);
static HANDLER (handle_hilite_command);
static HANDLER (handle_hook_command);
static HANDLER (handle_input_command);
static HANDLER (handle_lcd_command);
static HANDLER (handle_let_command);
static HANDLER (handle_listworlds_command);
static HANDLER (handle_load_command);
static HANDLER (handle_nogag_command);
static HANDLER (handle_nohilite_command);
static HANDLER (handle_quit_command);
static HANDLER (handle_recall_command);
static HANDLER (handle_restrict_command);
static HANDLER (handle_save_command);
static HANDLER (handle_set_command);
static HANDLER (handle_setenv_command);
static HANDLER (handle_sh_command);
static HANDLER (handle_shift_command);
static HANDLER (handle_suspend_command);
static HANDLER (handle_time_command);
static HANDLER (handle_trigger_command);
static HANDLER (handle_trigpc_command);
static HANDLER (handle_unbind_command);
static HANDLER (handle_undef_command);
static HANDLER (handle_undeft_command);
static HANDLER (handle_unhook_command);
static HANDLER (handle_untrig_command);
static HANDLER (handle_version_command);
static HANDLER (handle_watchdog_command);
static HANDLER (handle_watchname_command);
static HANDLER (handle_world_command);

typedef struct Command {
    char *name;
    Handler *func;
} Command;

  /* It is IMPORTANT that the commands be in alphabetical order! */

static Command cmd_table[] =
{
  { "ADDWORLD"    , handle_addworld_command    },
  { "BEEP"        , handle_beep_command        },
  { "BIND"        , handle_bind_command        },
  { "CAT"         , handle_cat_command         },
  { "DC"          , handle_dc_command          },
  { "DEF"         , handle_def_command         },
  { "DOKEY"       , handle_dokey_command       },
  { "ECHO"        , handle_echo_command        },
  { "EDIT"        , handle_edit_command        },
  { "EVAL"        , handle_eval_command        },
  { "EXPORT"      , handle_export_command      },
  { "GAG"         , handle_gag_command         },
  { "HELP"        , handle_help_command        },
  { "HILITE"      , handle_hilite_command      },
  { "HOOK"        , handle_hook_command        },
  { "INPUT"       , handle_input_command       },
  { "KILL"        , handle_kill_command        },
  { "LCD"         , handle_lcd_command         },
  { "LET"         , handle_let_command         },
  { "LIST"        , handle_list_command        },
  { "LISTSOCKETS" , handle_listsockets_command },
  { "LISTWORLDS"  , handle_listworlds_command  },
  { "LOAD"        , handle_load_command        },
  { "LOG"         , handle_log_command         },
  { "NOGAG"       , handle_nogag_command       },
  { "NOHILITE"    , handle_nohilite_command    },
  { "PS"          , handle_ps_command          },
  { "PURGE"       , handle_purge_command       },
  { "PURGEWORLD"  , handle_purgeworld_command  },
  { "QUIT"        , handle_quit_command        },
  { "QUOTE"       , handle_quote_command       },
  { "RECALL"      , handle_recall_command      },
  { "RECORDLINE"  , handle_recordline_command  },
  { "REPEAT"      , handle_repeat_command      },
  { "RESTRICT"    , handle_restrict_command    },
  { "SAVE"        , handle_save_command        },
  { "SAVEWORLD"   , handle_saveworld_command   },
  { "SEND"        , handle_send_command        },
  { "SET"         , handle_set_command         },
  { "SETENV"      , handle_setenv_command      },
  { "SH"          , handle_sh_command          },
  { "SHIFT"       , handle_shift_command       },
  { "SUSPEND"     , handle_suspend_command     },
  { "TEST"        , handle_test_command        },  
  { "TIME"        , handle_time_command        },  
  { "TRIGGER"     , handle_trigger_command     },  
  { "TRIGPC"      , handle_trigpc_command      },
  { "UNBIND"      , handle_unbind_command      },
  { "UNDEF"       , handle_undef_command       },
  { "UNDEFN"      , handle_undefn_command      },
  { "UNDEFT"      , handle_undeft_command      },
  { "UNHOOK"      , handle_unhook_command      },
  { "UNSET"       , handle_unset_command       },
  { "UNTRIG"      , handle_untrig_command      },
  { "UNWORLD"     , handle_unworld_command     },
  { "VERSION"     , handle_version_command     },
  { "WATCHDOG"    , handle_watchdog_command    },
  { "WATCHNAME"   , handle_watchname_command   },
  { "WORLD"       , handle_world_command       },
};

#define NUM_CMDS (sizeof(cmd_table) / sizeof(Command))

/*****************************************
 * Find, process and run commands/macros *
 *****************************************/

/* handle_command
 * Execute a single command line that has already been expanded.
 * note: cmd_line will be written into.
 */
int handle_command(cmd_line)
    char *cmd_line;
{
    char *old_command, *args;
    Handler *handler;
    Macro *macro;
    int result;
    extern int input_is_complete;

    while (*cmd_line == '/') cmd_line++;
    if (!*cmd_line || isspace(*cmd_line)) return 0;
    old_command = current_command;
    while (isspace(*cmd_line)) cmd_line++;
    current_command = cmd_line;
    for (args = current_command; *args && !isspace(*args); args++);
    if (*args) *args++ = '\0';
    while (isspace(*args)) args++;
    stripstr(current_command);
    stripstr(args);
    if (*current_command == '@') {
        if ((handler = find_command(current_command + 1))) {
            result = (*handler)(args);
        } else {
            cmderror("not a builtin command");
            result = 0;
        }
    } else if ((macro = find_macro(current_command))) {
        result = do_macro(macro, args);
    } else if ((handler = find_command(current_command))) {
        result = (*handler)(args);
    } else {
        cmderror("no such command or macro");
        result = 0;
    }
    current_command = NULL;
    if (input_is_complete)  /* true if cmd was "/dokey newline" */
        result = handle_input_line();
    current_command = old_command;
    return result;
}

Handler *find_command(cmd)
    char *cmd;
{
    int i;

    i = binsearch(cmd, (GENERIC*)cmd_table, NUM_CMDS, sizeof(Command), cstrcmp);
    return (i < 0) ? (Handler *)NULL : cmd_table[i].func;
}

static int handle_trigger_command(args)
    char *args;
{
    if (background) background++;  /* Nesting count. Prevents BACKGROUND hook */
    if (borg) check_trigger(args, 0);
    if (background) background--;     /* BUG if %{background} was altered */
    return 1;
}

/**********
 * Worlds *
 **********/

static int handle_listworlds_command(args)
    char *args;
{
    int full = FALSE;
    char c;

    startopt(args, "c");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
            case 'c':  full = TRUE; break;
            default:   return 0;
        }
    }
    return list_worlds(full, *args ? args : NULL, NULL);
}

static int handle_world_command(args)
    char *args;
{
    World *where = NULL;
    int autologin = login;
    int quietlogin = quiet;
    char c, *port;

    startopt(args, "lqn");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
            case 'l':  autologin = FALSE; break;
            case 'q':  quietlogin = TRUE; break;
            case 'n':  no_sock(); return 1;
            default:   return 0;
        }
    }

    if (!*args) {
        if ((where = get_world_header()) == NULL)
            tfputs("% No default world is set.", tferr);
    } else if ((port = strchr(args, ' ')) == NULL) {
        if ((where = find_world(args)) == NULL)
            do_hook(H_CONFAIL, "%% Connection to %s failed: %s", "%s %s",
                args, "world unknown");
    } else {
        for (*port++ = '\0'; isspace(*port); port++);
        if (strchr(port, ' ')) {
            tfputs("% Too many arguments.", tferr);
        } else {
            if (restrict >= RESTRICT_WORLD)
                tfputs("% \"/world <host> <port>\" restricted", tferr);
            else {
                where = new_world(NULL, "", "", args, port, "", "");
                where->flags |= WORLD_TEMP;
            }
        }
    }

    if (!where) return 0;
    opensock(where, autologin, quietlogin);
    return 1;
}

/*************
 * Variables *
 *************/

static int do_set(args, exportflag, localflag)
    char *args;
    int exportflag, localflag;
{
    char *value = "";
    int i;

    if (!*args) {
        if (!localflag) listvar(exportflag);
        return !localflag;
    } else if ((value = strchr(args, '='))) {
        *value++ = '\0';
        if (!*args) {
            tfputs("% missing variable name", tferr);
            return 0;
        }
    } else if ((value = strchr(args, ' '))) {
        for (*value++ = '\0'; *value && isspace(*value); value++);
    } else {
        if (localflag) return 0;
        if ((value = getvar(args))) {
            oprintf("%% %s=%s", args, value);
            return 1;
        } else {
            oprintf("%% %s not defined at global level", args);
            return 0;
        }
    }

    for (i = 0; args[i]; i++) {
        if (!(isalpha(args[i]) || args[i]=='_' || (i>0 && isdigit(args[i])))) {
            oputs("% illegal variable name.");
            return 0;
        }
    }

    if (localflag) return setlocalvar(args, value) ? 1 : 0;
    else return setvar(args, value, exportflag) ? 1 : 0;
}

static int handle_set_command(args)
    char *args;
{
    return do_set(args, FALSE, FALSE);
}

static int handle_setenv_command(args)
    char *args;
{
    return do_set(args, TRUE, FALSE);
}

static int handle_let_command(args)
    char *args;
{
    return do_set(args, FALSE, TRUE);
}

/********
 * Misc *
 ********/

static int handle_eval_command(args)
    char *args;
{
    return process_macro(args, "", SUB_MACRO);
}

static int handle_quit_command(args)
    char *args;
{
    extern int quit_flag;
    return quit_flag = 1;
}

static int handle_recall_command(args)
    char *args;
{
    return recall_history(args, tfout);
}

static int handle_sh_command(args)
    char *args;
{
    char *cmd;

    if (restrict >= RESTRICT_SHELL) {
        tfputs("% /sh:  restricted", tferr);
        return 0;
    }

    if (*args) {
        cmd = args;
        do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "command", cmd);
    } else {
        if ((cmd = getvar("SHELL")) == NULL) cmd = "/bin/sh";
        do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "shell", cmd);
    }
    return shell(cmd);
}

static int handle_suspend_command(args)
    char *args;
{
    return suspend();
}

static int handle_shift_command(args)
    char *args;
{
    return do_shift((*args) ? (atoi(args)) : 1);
}
    
static int handle_version_command(args)
    char *args;
{
    extern char version[];
    oprintf("%% %s.", version);
    return 1;
}

#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX MAXPATHLEN
# else
#  define PATH_MAX 1024
# endif
#endif

static int handle_lcd_command(args)
    char *args;
{
    char buffer[PATH_MAX + 1];
    STATIC_BUFFER(dirname);

    expand_filename(Stringcpy(dirname, args));
    if (dirname->len && chdir(dirname->s) < 0) {
        operror(dirname->s);
        return 0;
    }

#ifdef HAVE_GETCWD
    oprintf("%% Current directory is %s", getcwd(buffer, PATH_MAX));
#else
# ifdef HAVE_GETWD
    oprintf("%% Current directory is %s", getwd(buffer));
# endif
#endif
    return 1;
}

static int handle_echo_command(args)
    char *args;
{
    char c;
    short attrs = 0, wflag = 0;
    World *world = NULL;

    startopt(args, "a:w:");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
        case 'a': case 'f':
            if ((attrs |= parse_attrs(&args)) < 0) return 0;
            break;
        case 'w':
            wflag = 1;
            if (!*args) world = xworld();
            else if ((world = find_world(args)) == NULL) {
                tfprintf(tferr, "%% World %s not found.", args);
                return 0;
            } else if (!world->socket) {
                tfprintf(tferr, "%% Not connected to %s.", args);
                return 0;
            }
            break;
        default:
            return 0;
        }
    }
    if (!wflag)
        oputa(new_aline(args, attrs | F_NEWLINE));
    else if (world)
        world_output(world->socket, new_aline(args, attrs | F_NEWLINE));
    else {
        tfputs("% No current world.", tferr);
        return 0;
    }
    return 1;
}

static int handle_input_command(args)
    char *args;
{
    handle_input_string(args, strlen(args));
    return 1;
}

static int handle_time_command(args)
    char *args;
{
    char *str;

    if (!(str = tftime(args, time(NULL)))) return 0;
    oputs(str);
    return 1;
}

static int handle_restrict_command(args)
    char *args;
{
    int level;
    static char *enum_restrict[] = { "none", "shell", "file", "world", NULL };

    if (!*args) {
        oprintf("%% restriction level: %s", enum_restrict[restrict]);
        return restrict;
    } else if ((level = enum2int(args, enum_restrict, "/restrict")) < 0) {
        return 0;
    } else if (level < restrict) {
        oputs("% Restriction level can not be lowered.");
        return 0;
    }
    return restrict = level;
}


/********************
 * Generic handlers *
 ********************/   

static int read_file_commands(file, tinytalk)
    TFILE *file;
    int tinytalk;    /* Accept tinytalk-style world defs at top of file? */
{
    Stringp line, cmd;
    char *p;
    int done = 0, error = 0;

    Stringinit(line);
    Stringinit(cmd);
    current_file = file;
    current_lineno = 0;
    while (!done) {
        done = !tfgetS(line, file);
        current_lineno++;
        if (line->len) {
            if (line->s[0] == ';') continue;         /* skip comment lines */
            for (p = line->s; isspace(*p); p++);     /* strip leading space */
            Stringcat(cmd, p);
            if (line->s[line->len - 1] == '\\') {
                if (line->len < 2 || line->s[line->len - 2] != '%') {
                    Stringterm(cmd, cmd->len - 1);
                    continue;
                }
            }
        }
        if (!cmd->len) continue;
        if (*cmd->s == '/') {
            tinytalk = FALSE;
            /* Never use SUB_FULL here.  Libraries will break. */
            process_macro(cmd->s, NULL, SUB_NONE);
        } else if (tinytalk) {
            handle_addworld_command(cmd->s);
        } else {
            tfprintf(tferr, "%% %s, line %d: Invalid command. Aborting.\n",
                current_file->name, current_lineno);
            error = 1;
            break;
        }
        Stringterm(cmd, 0);
    }
    Stringfree(line);
    Stringfree(cmd);
    current_file = NULL;
    return !error;
}

int do_file_load(args, tinytalk)
    char *args;
    int tinytalk;
{
    register TFILE *cmdfile;
    int result;

    if ((cmdfile = tfopen(tfname(args, NULL), "r")) == NULL) {
        if (!tinytalk)
            do_hook(H_LOADFAIL, "%% %s: %s", "%s %s", args, STRERROR(errno));
        return 0;
    }
    do_hook(H_LOAD, "%% Loading commands from %s.", "%s", cmdfile->name);
    result = read_file_commands(cmdfile, tinytalk);
    tfclose(cmdfile);
    return result;
}


/**************************
 * Toggles with arguments *
 **************************/

static int handle_beep_command(args)
    char *args;
{
    int beeps = 0;

    if (!*args) beeps = 3;
    else if (ON) setivar("beep", 1, FALSE);
    else if (OFF) setivar("beep", 0, FALSE);
    else if (isdigit(*args)) beeps = atoi(args);
    else return 0;

    if (beep) bell(beeps);
    return 1;
}

static int handle_cat_command(args)
    char *args;
{
    concat = (*args == '%') ? 2 : 1;
    return 1;
}

static int do_watch(args, name, wlines, wmatch, flag)
    char *args, *name;
    int *wlines, *wmatch, *flag;
{
    int lines = 0, match = 0;
    char *ptr;

    if (!*args) {
        oprintf("%% %s %sabled.", name, *flag ? "en" : "dis");
        return 1;
    } else if (OFF) {
        setvar(name, "0", FALSE);
        oprintf("%% %s disabled.", name);
        return 1;
    }
    ptr = args;
    if (ON) for (ptr += 2; isspace(*ptr); ptr++);
    if ((match = numarg(&ptr)) < 0) return 0;
    if ((lines = numarg(&ptr)) < 0) return 0;
    *wmatch = match;
    *wlines = lines;
    setvar(name, "1", FALSE);
    oprintf("%% %s enabled, searching for %d out of %d lines",
        name, *wmatch, *wlines);
    return 1;
}

static int handle_watchdog_command(args)
    char *args;
{
    return do_watch(args, "watchdog", &wdlines, &wdmatch, &watchdog);
}

static int handle_watchname_command(args)
    char *args;
{
    return do_watch(args, "watchname", &wnlines, &wnmatch, &watchname);
}


/**********
 * Macros *
 **********/

static int handle_undef_command(args)              /* Undefine a macro. */
    char *args;
{
    return remove_macro(args, 0, 0);
}

static int handle_undeft_command(args)
    char *args;
{
    return remove_macro(args, F_ATTR, 0);
}

static int handle_save_command(args)
    char *args;
{
    if (restrict >= RESTRICT_FILE)
        tfputs("% /save: restricted", tferr);
    else if (*args)
        return save_macros(args);
    else
        tfputs("% Missing filename.", tferr);
    return 0;
}

static int handle_load_command(args)
    char *args;
{                   
    if (restrict >= RESTRICT_FILE) {
        tfputs("% /load: restricted", tferr);
        return 0;
    }
    if (*args) return do_file_load(args, FALSE);
    tfputs("% Missing filename.", tferr);
    return FALSE;
}

/*
 * Generic utility to split arguments into pattern and body.
 * Note: I can get away with this only because none of the functions
 * that use it are reentrant.  Be careful.
 */

static void split_args(args)
    char *args;
{
    char *place;

    place = strchr(args, '=');
    if (place == NULL) {
        Stringcpy(pattern, args);
        Stringterm(body, 0);
    } else {
        Stringncpy(pattern, args, place - args);
        while (isspace(*++place));
        Stringcpy(body, place);
    }
    stripString(pattern);
    stripString(body);
}

/***********
 * Hilites *
 ***********/

static int handle_hilite_command(args)
    char *args;
{
    if (!*args) {
        setvar("hilite", "1", FALSE);
        oputs("% Hilites are enabled.");
        return 0;
    } else {
        split_args(args);
        return add_macro(new_macro("", pattern->s, "", 0, NULL, body->s,
            hpri, 100, F_HILITE, 0));
    }
}

static int handle_nohilite_command(args)
    char *args;
{
    if (!*args) {
        setvar("hilite", "0", FALSE);
        oputs("% Hilites are disabled.");
        return 1;
    } else {
        return remove_macro(args, F_HILITE, 0);
    }
}


/********
 * Gags *
 ********/

static int handle_gag_command(args)
    char *args;
{
    if (!*args) {
        setvar("gag", "1", FALSE);
        oputs("% Gags are enabled.");
        return 0;
    } else {
        split_args(args);
        return add_macro(new_macro("", pattern->s, "", 0, NULL, body->s,
            gpri, 100, F_GAG, 0));
    }
}

static int handle_nogag_command(args)
    char *args;
{
    if (!*args) {
        setvar("gag", "0", FALSE);
        oputs("% Gags are disabled.");
        return 1;
    } else {
        return remove_macro(args, F_GAG, 0);
    }
}


/************
 * Triggers *
 ************/

static int handle_trigpc_command(args)
    char *args;
{
    int pri, prob;

    if ((pri = numarg(&args)) < 0) return 0;
    if ((prob = numarg(&args)) < 0) return 0;
    split_args(args);
    return add_macro(new_macro("", pattern->s, "", 0, NULL, body->s, pri,
        prob, F_NORM, 0));
}

static int handle_untrig_command(args)
    char *args;
{
    return remove_macro(args, F_NORM, 0);
}


/*********
 * Hooks *
 *********/

static int handle_hook_command(args)
    char *args;
{
    if (!*args) oprintf("%% Hooks %sabled", hookflag ? "en" : "dis");
    else if (OFF) setvar("hook", "0", FALSE);
    else if (ON) setvar("hook", "1", FALSE);
    else {
        split_args(args);
        return add_hook(pattern->s, body->s);
    }
    return 1;
}


static int handle_unhook_command(args)
    char *args;
{
    return remove_macro(args, 0, 1);
}


/********
 * Keys *
 ********/

static int handle_unbind_command(args)
    char *args;
{
    Macro *macro;

    if (!*args) return 0;
    if ((macro = find_key(print_to_ascii(args)))) kill_macro(macro);
    else tfprintf(tferr, "%% No binding for %s", args);
    return macro ? 1 : 0;
}

static int handle_bind_command(args)
    char *args;
{
    Macro *spec;

    if (!*args) return 0;
    split_args(args);
    spec = new_macro("", NULL, print_to_ascii(pattern->s), 0, NULL, body->s,
        0, 0, 0, 0);
    if (!install_bind(spec)) return 0;
    return add_macro(spec);
}

static int handle_dokey_command(args)
    char *args;
{
    EditFunc *func;

    if ((func = find_efunc(args))) return (*func)();
    else tfprintf(tferr, "%% No editing function %s", args); 
    return 0;
}

