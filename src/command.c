/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.c,v 35004.36 1997/11/20 07:29:45 hawkeye Exp $ */


/*****************************************************************
 * Fugue command handlers
 *****************************************************************/

#include "config.h"
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

static char *pattern, *body;
static int quietload = 0;

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
  { "LISTVAR"     , handle_listvar_command     },
  { "LISTWORLDS"  , handle_listworlds_command  },
  { "LOAD"        , handle_load_command        },
  { "LOCALECHO"   , handle_localecho_command   },
  { "LOG"         , handle_log_command         },
  { "PROMPT"      , handle_prompt_command      },
  { "PS"          , handle_ps_command          },
  { "PURGE"       , handle_purge_command       },
  { "QUIT"        , handle_quit_command        },
  { "QUOTE"       , handle_quote_command       },
  { "RECALL"      , handle_recall_command      },
  { "RECORDLINE"  , handle_recordline_command  },
  { "REPEAT"      , handle_repeat_command      },
  { "RESTRICT"    , handle_restrict_command    },
  { "RETURN"      , handle_return_command      },
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

Handler *find_command(name)
    CONST char *name;
{
    Command *cmd;

    cmd = (Command *)binsearch((GENERIC*)name, (GENERIC*)cmd_table,
        NUM_CMDS, sizeof(Command), cstrstructcmp);
    return cmd ? cmd->func : (Handler *)NULL;
}

static struct Value *handle_trigger_command(args)
    char *args;
{
    World *world = NULL;
    int usedefault = TRUE, is_global = FALSE, result = 0, opt;
    long hook = 0;
    Aline *old_incoming_text;

    if (!borg) return newint(0);

    startopt(args, "gw:h:");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
            case 'g':
                usedefault = FALSE;
                is_global = TRUE;
                break;
            case 'w':
                usedefault = FALSE;
                if (!(world = (*args) ? find_world(args) : xworld())) {
                    eprintf("No world %s", args);
                    return newint(0);
                }
                break;
            case 'h':
                hook = parse_hook(&args);
                if (hook < 0) return newint(0);
                break;
            default:
                return newint(0);
          }
    }

    if (usedefault) {
        is_global = TRUE;
        world = xworld();
    }

    old_incoming_text = incoming_text;
    (incoming_text = new_aline(args, 0))->links = 1;

    result = find_and_run_matches(args, hook, &incoming_text, world, is_global);

    free_aline(incoming_text);
    incoming_text = old_incoming_text;
    return newint(result);
}

static struct Value *handle_substitute_command(args)
    char *args;
{
    Aline *old;

    if (!incoming_text) {
        eprintf("not called from trigger");
        return newint(0);
    }
    old = incoming_text;
    (incoming_text = new_aline(args, old->attrs))->links = 1;
    incoming_text->time = old->time;
    free_aline(old);
    return newint(1);
}

/**********
 * Worlds *
 **********/

static struct Value *handle_connect_command(args)
    char *args;
{
    char *port = NULL;
    int autologin = login, quietlogin = quietflag, opt;

    startopt(args, "lq");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
            case 'l':  autologin = FALSE; break;
            case 'q':  quietlogin = TRUE; break;
            default:   return newint(0);
        }
    }
    for (port = args; *port && !isspace(*port); port++);
    if (*port) {
        *port = '\0';
        while (isspace(*++port));
    }
    return newint(openworld(args, *port ? port : NULL, autologin, quietlogin));
}

static struct Value *handle_localecho_command(args)
    char *args;
{
    if (!*args) return newint(local_echo(-1));
    else if (cstrcmp(args, "on") == 0) local_echo(1);
    else if (cstrcmp(args, "off") == 0) local_echo(0);
    return newint(1);
}

/*************
 * Variables *
 *************/

static struct Value *handle_set_command(args)
    char *args;
{
    return newint(do_set(args, FALSE, FALSE));
}

static struct Value *handle_setenv_command(args)
    char *args;
{
    return newint(do_set(args, TRUE, FALSE));
}

static struct Value *handle_let_command(args)
    char *args;
{
    return newint(do_set(args, FALSE, TRUE));
}

/********
 * Misc *
 ********/

static struct Value *handle_eval_command(args)
    char *args;
{
    int c, subflag = SUB_MACRO;

    startopt(args, "s:");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
        case 's':
            if ((subflag = enum2int(args, enum_sub, "/eval -s")) < 0)
                return newint(0);
            break;
        default:
            return newint(0);
        }
    }
    return newint(process_macro(args, NULL, subflag));
}

static struct Value *handle_quit_command(args)
    char *args;
{
    quit_flag = 1;
    return newint(1);
}

static struct Value *handle_sh_command(args)
    char *args;
{
    CONST char *cmd;
    char c;
    int quiet = 0;

    if (restrict >= RESTRICT_SHELL) {
        eprintf("restricted");
        return newint(0);
    }

    startopt(args, "q");
    while ((c = nextopt(&args, NULL))) {
        if (c == 'q') quiet++;
        else return newint(0);
    }

    if (*args) {
        cmd = args;
        if (!quiet)
            do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "command", cmd);
    } else {
        /* Note: on unix, system("") does nothing, but SHELL should always be
         * defined, so it won't happen.  On os/2, SHELL usually isn't defined,
         * and system("") will choose choose the default interpreter; SHELL
         * will be used if defined, of course.
         */
        if ((cmd = getvar("SHELL")) == NULL) cmd = "";
        if (!quiet)
            do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "shell", cmd);
    }
    return newint(shell(cmd));
}

static struct Value *handle_suspend_command(args)
    char *args;
{
    return newint(suspend());
}

static struct Value *handle_version_command(args)
    char *args;
{
    oprintf("%% %s.", version);
    oprintf("%% %s.", copyright);
    if (*contrib) oprintf("%% %s", contrib);
    if (*mods)    oprintf("%% %s", mods);
    if (*sysname) oprintf("%% Built for %s", sysname);
    return newint(1);
}

static struct Value *handle_lcd_command(args)
    char *args;
{
    char buffer[PATH_MAX + 1];

    args = expand_filename(args);
    if (*args && chdir(args) < 0) {
        operror(args);
        return newint(0);
    }

#ifdef HAVE_getcwd
    oprintf("%% Current directory is %s", getcwd(buffer, PATH_MAX));
#else
# ifdef HAVE_getwd
    oprintf("%% Current directory is %s", getwd(buffer));
# endif
#endif
    return newint(1);
}

static struct Value *handle_echo_command(args)
    char *args;
{
    char c;
    attr_t attrs = 0;
    World *world = NULL;
    int retval = 1, raw = FALSE, partials = FALSE;
    TFILE *tfile = tfout;
    Aline *aline;

    startopt(args, "a:ew:pr");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
        case 'a': case 'f':
            if ((attrs |= parse_attrs(&args)) < 0) return newint(0);
            break;
        case 'e':
            tfile = tferr;
            break;
        case 'w':
            if (!(world = (*args) ? find_world(args) : xworld())) {
                eprintf("No world %s", args);
                return newint(0);
            }
            break;
        case 'p':
            partials = TRUE;
            break;
        case 'r':
            raw = TRUE;
            break;
        default:
            return newint(0);
        }
    }

    if (raw) {
        write(STDOUT_FILENO, args, strlen(args));
    } else {
        (aline = new_aline(args, attrs))->links++;
        if (partials)
            if (handle_inline_attr(aline, attrs) < 0)
                retval = 0;
        if (retval) {
            if (world)
                world_output(world, aline);
            else
                tfputa(aline, tfile);
        }
        free_aline(aline);
    }
    return newint(retval);
}

static struct Value *handle_restrict_command(args)
    char *args;
{
    int level;
    static CONST char *enum_restrict[] =
        { "none", "shell", "file", "world", NULL };

    if (!*args) {
        oprintf("%% restriction level: %s", enum_restrict[restrict]);
        return newint(restrict);
    } else if ((level = enum2int(args, enum_restrict, "/restrict")) < 0) {
        return newint(0);
    } else if (level < restrict) {
        oputs("% Restriction level can not be lowered.");
        return newint(0);
    }
    return newint(restrict = level);
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
    STATIC_BUFFER(libfile);

    loadfile = tfopen(expand_filename(args), "r");
    if (!loadfile && !tinytalk && errno == ENOENT && !is_absolute_path(args)) {
        /* Relative file was not found, so look in %TFLIBDIR. */
        if (!TFLIBDIR || !*TFLIBDIR || !is_absolute_path(TFLIBDIR)) {
            eprintf("warning: invalid value for %%TFLIBDIR");
        } else {
            Sprintf(libfile, 0, "%s/%s", TFLIBDIR, args);
            loadfile = tfopen(expand_filename(libfile->s), "r");
        }
    }

    if (!loadfile) {
        if (!tinytalk || errno != ENOENT)
            do_hook(H_LOADFAIL, "%% %s: %s", "%s %s", args, strerror(errno));
        loadfile = old_file;
        return 0;
    }

    do_hook(H_LOAD, quietload ? NULL : "%% Loading commands from %s.",
        "%s", loadfile->name);
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

static struct Value *handle_beep_command(args)
    char *args;
{
    int n = 0;

    if (!*args) n = 3;
    else if (cstrcmp(args, "on") == 0) setivar("beep", 1, FALSE);
    else if (cstrcmp(args, "off") == 0) setivar("beep", 0, FALSE);
    else if (isdigit(*args) && (n = atoi(args)) < 0) return newint(0);

    bell(n);
    return newint(1);
}


/**********
 * Macros *
 **********/

static struct Value *handle_undef_command(args)        /* Undefine a macro. */
    char *args;
{
    char *name;
    int result = 0;

    while (*(name = stringarg(&args, NULL)))
        result += remove_macro(name, 0, 0);
    return newint(result);
}

static struct Value *handle_save_command(args)
    char *args;
{
    if (restrict >= RESTRICT_FILE) {
        eprintf("restricted");
        return newint(0);
    }
    if (*args) return newint(save_macros(args));
    eprintf("missing filename");
    return newint(0);
}

static struct Value *handle_load_command(args)
    char *args;
{                   
    int quiet = 0, result = 0;
    char c;

    if (restrict >= RESTRICT_FILE) {
        eprintf("restricted");
        return newint(0);
    }

    startopt(args, "q");
    while ((c = nextopt(&args, NULL))) {
        if (c == 'q') quiet = 1;
        else return newint(0);
    }

    quietload += quiet;
    if (*args) result = do_file_load(args, FALSE);
    else eprintf("missing filename");
    quietload -= quiet;
    return newint(result);
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

static struct Value *handle_hilite_command(args)
    char *args;
{
    if (!*args) {
        setvar("hilite", "1", FALSE);
        oputs("% Hilites enabled.");
        return newint(0);
    } else {
        split_args(args);
        return newint(add_macro(new_macro(pattern, "", 0, NULL, body,
            hpri, 100, F_HILITE, 0, matching)));
    }
}


/********
 * Gags *
 ********/

static struct Value *handle_gag_command(args)
    char *args;
{
    if (!*args) {
        setvar("gag", "1", FALSE);
        oputs("% Gags enabled.");
        return newint(0);
    } else {
        split_args(args);
        return newint(add_macro(new_macro(pattern, "", 0, NULL, body,
            gpri, 100, F_GAG, 0, matching)));
    }
}


/************
 * Triggers *
 ************/

static struct Value *handle_trigpc_command(args)
    char *args;
{
    int pri, prob;

    if ((pri = numarg(&args)) < 0) return newint(0);
    if ((prob = numarg(&args)) < 0) return newint(0);
    split_args(args);
    return newint(add_macro(new_macro(pattern, "", 0, NULL, body, pri,
        prob, F_NORM, 0, matching)));
}

static struct Value *handle_untrig_command(args)
    char *args;
{
    char c;
    attr_t attrs = 0;

    startopt(args, "a:");
    while ((c = nextopt(&args, NULL))) {
        if (c != 'a') return newint(0);
        if ((attrs |= parse_attrs(&args)) < 0) return newint(0);
    }
    return newint(remove_macro(args, attrs ? attrs : F_NORM, 0));
}


/*********
 * Hooks *
 *********/

static struct Value *handle_hook_command(args)
    char *args;
{
    if (!*args) oprintf("%% Hooks %sabled", hookflag ? "en" : "dis");
    else if (cstrcmp(args, "off") == 0) setvar("hook", "0", FALSE);
    else if (cstrcmp(args, "on") == 0) setvar("hook", "1", FALSE);
    else {
        split_args(args);
        return newint(add_hook(pattern, body));
    }
    return newint(1);
}


static struct Value *handle_unhook_command(args)
    char *args;
{
    return newint(remove_macro(args, 0, 1));
}


/********
 * Keys *
 ********/

static struct Value *handle_unbind_command(args)
    char *args;
{
    Macro *macro;

    if (!*args) return newint(0);
    if ((macro = find_key(print_to_ascii(args)))) kill_macro(macro);
    else eprintf("No binding for %s", args);
    return newint(!!macro);
}

static struct Value *handle_bind_command(args)
    char *args;
{
    Macro *spec;

    if (!*args) return newint(0);
    split_args(args);
    spec = new_macro(NULL, print_to_ascii(pattern), 0, NULL, body,
        0, 0, 0, 0, 0);
    return newint(add_macro(spec));
}

