/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.c,v 35004.18 1997/04/02 23:50:13 hawkeye Exp $ */


/*****************************************************************
 * Fugue command handlers
 *****************************************************************/

#include "config.h"
#include <errno.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "commands.h"
#include "command.h"
#include "world.h"	/* World, find_world() */
#include "socket.h"	/* openworld() */
#include "output.h"	/* oflush(), bell() */
#include "macro.h"
#include "keyboard.h"	/* find_key(), find_efunc() */
#include "expand.h"     /* process_macro() */
#include "search.h"
#include "signals.h"    /* suspend(), shell() */
#include "variable.h"

#define ON (!cstrcmp(args, "on"))
#define OFF (!cstrcmp(args, "off"))

CONST char  *current_command = NULL;
TFILE *loadfile = NULL;
int    loadline = 0;
int wnmatch = 4, wnlines = 5, wdmatch = 2, wdlines = 5;

extern int errno;
extern int restrict;
extern int quit_flag;

static char *pattern, *body;

static int  FDECL(do_watch,(char *args, CONST char *name, int *wlines,
                  int *wmatch, int flag));
static void FDECL(split_args,(char *args));

static HANDLER (handle_beep_command);
static HANDLER (handle_bind_command);
static HANDLER (handle_connect_command);
static HANDLER (handle_echo_command);
static HANDLER (handle_eval_command);
static HANDLER (handle_gag_command);
static HANDLER (handle_hilite_command);
static HANDLER (handle_hook_command);
static HANDLER (handle_lcd_command);
static HANDLER (handle_let_command);
static HANDLER (handle_load_command);
static HANDLER (handle_localecho_command);
static HANDLER (handle_quit_command);
static HANDLER (handle_restrict_command);
static HANDLER (handle_save_command);
static HANDLER (handle_set_command);
static HANDLER (handle_setenv_command);
static HANDLER (handle_sh_command);
static HANDLER (handle_substitute_command);
static HANDLER (handle_suspend_command);
static HANDLER (handle_trigger_command);
static HANDLER (handle_trigpc_command);
static HANDLER (handle_unbind_command);
static HANDLER (handle_undef_command);
static HANDLER (handle_unhook_command);
static HANDLER (handle_untrig_command);
static HANDLER (handle_version_command);
static HANDLER (handle_watchdog_command);
static HANDLER (handle_watchname_command);

typedef struct Command {
    CONST char *name;
    Handler *func;
} Command;

  /* It is IMPORTANT that the commands be in alphabetical order! */

static CONST Command cmd_table[] =
{
  { "ADDWORLD"    , handle_addworld_command    },
  { "BEEP"        , handle_beep_command        },
  { "BIND"        , handle_bind_command        },
  { "CONNECT"     , handle_connect_command     },
  { "DC"          , handle_dc_command          },
  { "DEF"         , handle_def_command         },
  { "DOKEY"       , handle_dokey_command       },
  { "ECHO"        , handle_echo_command        },
  { "EDIT"        , handle_edit_command        },
  { "EVAL"        , handle_eval_command        },
  { "EXPORT"      , handle_export_command      },
  { "FG"          , handle_fg_command          },
  { "GAG"         , handle_gag_command         },
  { "HELP"        , handle_help_command        },
  { "HILITE"      , handle_hilite_command      },
  { "HISTSIZE"    , handle_histsize_command    },
  { "HOOK"        , handle_hook_command        },
  { "INPUT"       , handle_input_command       },
  { "KILL"        , handle_kill_command        },
  { "LCD"         , handle_lcd_command         },
  { "LET"         , handle_let_command         },
  { "LIST"        , handle_list_command        },
  { "LISTSOCKETS" , handle_listsockets_command },
  { "LISTWORLDS"  , handle_listworlds_command  },
  { "LOAD"        , handle_load_command        },
  { "LOCALECHO"   , handle_localecho_command   },
  { "LOG"         , handle_log_command         },
  { "PROMPT"      , handle_prompt_command      },
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
  { "SET"         , handle_set_command         },
  { "SETENV"      , handle_setenv_command      },
  { "SH"          , handle_sh_command          },
  { "SHIFT"       , handle_shift_command       },
  { "SUBSTITUTE"  , handle_substitute_command  },
  { "SUSPEND"     , handle_suspend_command     },
  { "TEST"        , handle_test_command        },  
  { "TRIGGER"     , handle_trigger_command     },  
  { "TRIGPC"      , handle_trigpc_command      },
  { "UNBIND"      , handle_unbind_command      },
  { "UNDEF"       , handle_undef_command       },
  { "UNDEFN"      , handle_undefn_command      },
  { "UNHOOK"      , handle_unhook_command      },
  { "UNSET"       , handle_unset_command       },
  { "UNTRIG"      , handle_untrig_command      },
  { "UNWORLD"     , handle_unworld_command     },
  { "VERSION"     , handle_version_command     },
  { "WATCHDOG"    , handle_watchdog_command    },
  { "WATCHNAME"   , handle_watchname_command   },
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
    String *cmd_line;
{
    CONST char *old_command;
    Handler *handler;
    Macro *macro;
    int result, builtin = 0, truth = 1;
    char *str, *end;
    extern int pending_line, read_depth;

    str = cmd_line->s + 1;
    if (!*str || isspace(*str)) return 0;
    old_command = current_command;
    current_command = str;
    while (*str && !isspace(*str)) str++;
    if (*str) *str++ = '\0';
    while (isspace(*str)) str++;
    for (end = cmd_line->s + cmd_line->len - 1; isspace(*end); end--);
    *++end = '\0';
    while (*current_command) {
        if (*current_command == '@')
            builtin = 1;
        else if (*current_command == '!')
            truth = !truth;
        else
            break;
        current_command++;
    }
    if (builtin) {
        if ((handler = find_command(current_command))) {
            result = (*handler)(str);
        } else {
            eprintf("not a builtin command");
            result = 0;
        }
    } else if ((macro = find_macro(current_command))) {
        result = do_macro(macro, str);
    } else if ((handler = find_command(current_command))) {
        result = (*handler)(str);
    } else {
        eprintf("no such command or macro");
        result = 0;
    }
    current_command = NULL;
    if (pending_line && !read_depth)  /* "/dokey newline" and not in read() */
        result = handle_input_line();
    current_command = old_command;
    return truth ? result : !result;
}

Handler *find_command(name)
    CONST char *name;
{
    Command *cmd;

    cmd = (Command *)binsearch((GENERIC*)name, (GENERIC*)cmd_table,
        NUM_CMDS, sizeof(Command), cstrstructcmp);
    return cmd ? cmd->func : (Handler *)NULL;
}

static int handle_trigger_command(args)
    char *args;
{
    World *world = NULL;
    int usedefault = TRUE, globalflag = FALSE, opt;
    long hook = 0;

    if (!borg) return 0;

    startopt(args, "gw:h:");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
            case 'g':
                usedefault = FALSE;
                globalflag = TRUE;
                break;
            case 'w':
                usedefault = FALSE;
                if (!(world = (*args) ? find_world(args) : xworld())) {
                    eprintf("No world %s", args);
                    return 0;
                }
                break;
            case 'h':
                hook = parse_hook(&args);
                if (hook < 0) return 0;
                break;
            default:
                return 0;
          }
    }

    if (usedefault) {
        globalflag = TRUE;
        world = xworld();
    }

    return find_and_run_matches(args, hook, NULL, world, globalflag);

}

static int handle_substitute_command(args)
    char *args;
{
    Aline *old;
    extern Aline *incoming_text;

    if (!incoming_text) {
        eprintf("not called from trigger");
        return 0;
    }
    old = incoming_text;
    (incoming_text = new_aline(args, old->attrs))->links = 1;
    free_aline(old);
    return 1;
}

/**********
 * Worlds *
 **********/

static int handle_connect_command(args)
    char *args;
{
    char *port = NULL;
    int autologin = login, quietlogin = quietflag, opt;

    startopt(args, "lq");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
            case 'l':  autologin = FALSE; break;
            case 'q':  quietlogin = TRUE; break;
            default:   return 0;
        }
    }
    for (port = args; *port && !isspace(*port); port++);
    if (*port) {
        *port = '\0';
        while (isspace(*++port));
    }
    return openworld(args, *port ? port : NULL, autologin, quietlogin);
}

static int handle_localecho_command(args)
    char *args;
{
    if (!*args) return local_echo(-1);
    else if (ON) local_echo(1);
    else if (OFF) local_echo(0);
    return 1;
}

/*************
 * Variables *
 *************/

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
    int c, subflag = SUB_MACRO;
    extern CONST char *enum_sub[];

    startopt(args, "s:");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
        case 's':
            if ((subflag = enum2int(args, enum_sub, "/eval -s")) < 0)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return process_macro(args, NULL, subflag);
}

static int handle_quit_command(args)
    char *args;
{
    return quit_flag = 1;
}

static int handle_sh_command(args)
    char *args;
{
    CONST char *cmd;

    if (restrict >= RESTRICT_SHELL) {
        eprintf("restricted");
        return 0;
    }

    if (*args) {
        cmd = args;
        do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "command", cmd);
    } else {
        /* Note: on unix, system("") does nothing, but SHELL should always be
         * defined, so it won't happen.  On os/2, SHELL usually isn't defined,
         * and system("") will choose choose the default interpreter; SHELL
         * will be used if defined, of course.
         */
        if ((cmd = getvar("SHELL")) == NULL) cmd = "";
        do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "shell", cmd);
    }
    return shell(cmd);
}

static int handle_suspend_command(args)
    char *args;
{
    return suspend();
}

static int handle_version_command(args)
    char *args;
{
    extern CONST char version[], sysname[], copyright[], contrib[], mods[];
    oprintf("%% %s.", version);
    oprintf("%% %s.", copyright);
    if (*contrib) oprintf("%% %s", contrib);
    if (*mods)    oprintf("%% %s", mods);
    if (*sysname) oprintf("%% Built for %s", sysname);
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

    args = expand_filename(args);
    if (*args && chdir(args) < 0) {
        operror(args);
        return 0;
    }

#ifdef HAVE_getcwd
    oprintf("%% Current directory is %s", getcwd(buffer, PATH_MAX));
#else
# ifdef HAVE_getwd
    oprintf("%% Current directory is %s", getwd(buffer));
# endif
#endif
    return 1;
}

static int handle_echo_command(args)
    char *args;
{
    char c;
    attr_t attrs = 0;
    World *world = NULL;
    int raw = FALSE;
    TFILE *tfile = tfout;

    startopt(args, "a:ew:r");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
        case 'a': case 'f':
            if ((attrs |= parse_attrs(&args)) < 0) return 0;
            break;
        case 'e':
            tfile = tferr;
            break;
        case 'w':
            if (!(world = (*args) ? find_world(args) : xworld())) {
                eprintf("No world %s", args);
                return 0;
            }
            break;
        case 'r':
            raw = TRUE;
            break;
        default:
            return 0;
        }
    }

    if (raw)
        write(STDOUT_FILENO, args, strlen(args));
    else if (world)
        world_output(world, new_aline(args, attrs));
    else
        tfputa(new_aline(args, attrs), tfile);
    return 1;
}

static int handle_restrict_command(args)
    char *args;
{
    int level;
    static CONST char *enum_restrict[] =
        { "none", "shell", "file", "world", NULL };

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

int do_file_load(args, tinytalk)
    CONST char *args;
    int tinytalk;
{
    Stringp line, cmd;
    int done = 0, error = 0;
    TFILE *old_file = loadfile;
    int old_lineno = loadline;

    if ((loadfile = tfopen(expand_filename(args), "r")) == NULL) {
        if (!tinytalk || errno != ENOENT)
            do_hook(H_LOADFAIL, "%% %s: %s", "%s %s", args, strerror(errno));
        loadfile = old_file;
        return 0;
    }
    do_hook(H_LOAD, "%% Loading commands from %s.", "%s", loadfile->name);
    oflush();  /* Load could take awhile, so flush pending output first. */

    Stringninit(line, 80);
    Stringninit(cmd, 192);
    loadline = 0;
    while (!done) {
        if (interrupted()) {
            eprintf("file load interrupted.");
            error = 1;
            break;
        }
        done = !tfgetS(line, loadfile);
        loadline++;
        if (line->len) {
            char *p;
            if (line->s[0] == ';') continue;         /* skip comment lines */
            for (p = line->s; isspace(*p); p++);     /* strip leading space */
            Stringcat(cmd, p);
            if (line->s[line->len - 1] == '\\') {
                if (line->len < 2 || line->s[line->len - 2] != '%') {
                    Stringterm(cmd, cmd->len - 1);
                    continue;
                }
            } else {
                p = line->s + line->len - 1;
                while (p > line->s && isspace(*p)) p--;
                if (*p == '\\')
                    eprintf("WARNING: whitespace following final '\\'");
            }
        }
        if (!cmd->len) continue;
        if (*cmd->s == '/') {
            tinytalk = FALSE;
            /* Never use SUB_FULL here.  Libraries will break. */
            process_macro(cmd->s, NULL, SUB_KEYWORD);
        } else if (tinytalk) {
            handle_addworld_command(cmd->s);
        } else {
            eprintf("Invalid command. Aborting.");
            error = 1;
            break;
        }
        Stringterm(cmd, 0);
    }

    Stringfree(line);
    Stringfree(cmd);
    if (tfclose(loadfile) != 0)
        eputs("load: unknown error reading file");
    loadfile = old_file;
    loadline = old_lineno;
    return !error;
}


/**************************
 * Toggles with arguments *
 **************************/

static int handle_beep_command(args)
    char *args;
{
    int n = 0;

    if (!*args) n = 3;
    else if (ON) setivar("beep", 1, FALSE);
    else if (OFF) setivar("beep", 0, FALSE);
    else if (isdigit(*args) && (n = atoi(args)) < 0) return 0;

    bell(n);
    return 1;
}

static int do_watch(args, name, wlines, wmatch, flag)
    char *args;
    CONST char *name;
    int *wlines, *wmatch, flag;
{
    int out_of, match;

    if (!*args) {
        oprintf("%% %s %sabled.", name, flag ? "en" : "dis");
        return 1;
    } else if (OFF) {
        setvar(name, "0", FALSE);
        oprintf("%% %s disabled.", name);
        return 1;
    } else if (ON) {
        /* do nothing */
    } else {
        if ((match = numarg(&args)) < 0) return 0;
        if ((out_of = numarg(&args)) < 0) return 0;
        *wmatch = match;
        *wlines = out_of;
    }
    setvar(name, "1", FALSE);
    oprintf("%% %s enabled, searching for %d out of %d lines",
        name, *wmatch, *wlines);
    return 1;
}

static int handle_watchdog_command(args)
    char *args;
{
    return do_watch(args, "watchdog", &wdlines, &wdmatch, watchdog);
}

static int handle_watchname_command(args)
    char *args;
{
    return do_watch(args, "watchname", &wnlines, &wnmatch, watchname);
}


/**********
 * Macros *
 **********/

static int handle_undef_command(args)              /* Undefine a macro. */
    char *args;
{
    return remove_macro(args, 0, 0);
}

static int handle_save_command(args)
    char *args;
{
    if (restrict >= RESTRICT_FILE) {
        eprintf("restricted");
        return 0;
    }
    if (*args) return save_macros(args);
    eprintf("missing filename");
    return 0;
}

static int handle_load_command(args)
    char *args;
{                   
    if (restrict >= RESTRICT_FILE) {
        eprintf("restricted");
        return 0;
    }
    if (*args) return do_file_load(args, FALSE);
    eprintf("missing filename");
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

    for (place = args; *place && *place != '='; place++);
    if (*place) *place++ = '\0';
    pattern = stripstr(args);
    body = stripstr(place);
}

/***********
 * Hilites *
 ***********/

static int handle_hilite_command(args)
    char *args;
{
    if (!*args) {
        setvar("hilite", "1", FALSE);
        oputs("% Hilites enabled.");
        return 0;
    } else {
        split_args(args);
        return add_macro(new_macro(pattern, "", 0, NULL, body,
            hpri, 100, F_HILITE, 0, matching));
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
        oputs("% Gags enabled.");
        return 0;
    } else {
        split_args(args);
        return add_macro(new_macro(pattern, "", 0, NULL, body,
            gpri, 100, F_GAG, 0, matching));
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
    return add_macro(new_macro(pattern, "", 0, NULL, body, pri,
        prob, F_NORM, 0, matching));
}

static int handle_untrig_command(args)
    char *args;
{
    char c;
    attr_t attrs = 0;

    startopt(args, "a:");
    while ((c = nextopt(&args, NULL))) {
        if (c != 'a') return 0;
        if ((attrs |= parse_attrs(&args)) < 0) return 0;
    }
    return remove_macro(args, attrs ? attrs : F_NORM, 0);
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
        return add_hook(pattern, body);
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
    else eprintf("No binding for %s", args);
    return !!macro;
}

static int handle_bind_command(args)
    char *args;
{
    Macro *spec;

    if (!*args) return 0;
    split_args(args);
    spec = new_macro(NULL, print_to_ascii(pattern), 0, NULL, body,
        0, 0, 0, 0, 0);
    return add_macro(spec);
}

