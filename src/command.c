/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.c,v 35004.58 1999/01/31 00:27:38 hawkeye Exp $ */


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
#include "output.h"	/* oflush(), dobell() */
#include "macro.h"
#include "keyboard.h"	/* find_key(), find_efunc() */
#include "expand.h"     /* process_macro(), breaking */
#include "search.h"
#include "signals.h"    /* suspend(), shell() */
#include "variable.h"

int exiting = 0;

static char *pattern, *body;
static int quietload = 0;

static void FDECL(split_args,(char *args));

static HANDLER (handle_beep_command);
static HANDLER (handle_bind_command);
static HANDLER (handle_connect_command);
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
  { "BEEP"        , handle_beep_command        },
  { "BIND"        , handle_bind_command        },
  { "CONNECT"     , handle_connect_command     },
  { "DC"          , handle_dc_command          },
  { "DEF"         , handle_def_command         },
  { "DOKEY"       , handle_dokey_command       },
  { "EDIT"        , handle_edit_command        },
  { "EVAL"        , handle_eval_command        },
  { "EXIT"        , handle_exit_command        },
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
  { "LISTSTREAMS" , handle_liststreams_command },
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
  { "RESULT"      , handle_result_command      },
  { "RETURN"      , handle_return_command      },
  { "SAVE"        , handle_save_command        },
  { "SAVEWORLD"   , handle_saveworld_command   },
  { "SET"         , handle_set_command         },
  { "SETENV"      , handle_setenv_command      },
  { "SH"          , handle_sh_command          },
  { "SHIFT"       , handle_shift_command       },
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

int handle_substitute_func(string, attrstr, inline_flag)
    CONST char *string, *attrstr;
    int inline_flag;
{
    Aline *aline;
    attr_t attrs;

    if (!incoming_text) {
        eprintf("not called from trigger");
        return 0;
    }

    attrs = parse_attrs((char **)&attrstr);
    if (attrs < 0) return 0;

    (aline = new_aline(string, incoming_text->attrs))->links++;
    aline->tv.tv_sec = incoming_text->tv.tv_sec;
    aline->tv.tv_usec = incoming_text->tv.tv_usec;
    add_attr(aline->attrs, attrs);

    if (inline_flag) {
        if (handle_inline_attr(aline, attrs) < 0) {
            free_aline(aline);
            return 0;
        }
    }

    free_aline(incoming_text);
    incoming_text = aline;
    return 1;
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
    for (port = args; *port && !is_space(*port); port++);
    if (*port) {
        *port = '\0';
        while (is_space(*++port));
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

    if (restriction >= RESTRICT_SHELL) {
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


int handle_echo_func(string, attrstr, inline_flag, dest)
    CONST char *string, *attrstr, *dest;
    int inline_flag;
{
    attr_t attrs;
    int raw = 0;
    TFILE *file = tfout;
    World *world = NULL;
    Aline *aline = NULL;

    if ((attrs = parse_attrs((char **)&attrstr)) < 0) return (0);
    switch(*dest) {
        case 'r':  raw = 1;       break;
        case 'o':  file = tfout;  break;
        case 'e':  file = tferr;  break;
        case 'w':
            dest++;
            if (!(world = (*dest) ? find_world(dest) : xworld())) {
                eprintf("No world %s", dest);
                return (0);
            }
            break;
        default:
            eprintf("illegal destination '%c'", *dest);
            return (0);
    }
    if (raw) {
        write(STDOUT_FILENO, string, strlen(string));
        return (1);
    }
    (aline = new_aline(string, attrs))->links++;
    if (inline_flag) {
        if (handle_inline_attr(aline, attrs) < 0) {
            free_aline(aline);
            return (0);
        }
    }

    if (world)
        world_output(world, aline);
    else
        tfputa(aline, file);
    free_aline(aline);

    return (1);
}


static struct Value *handle_restrict_command(args)
    char *args;
{
    int level;
    static CONST char *enum_restrict[] =
        { "none", "shell", "file", "world", NULL };

    if (!*args) {
        oprintf("%% restriction level: %s", enum_restrict[restriction]);
        return newint(restriction);
    } else if ((level = enum2int(args, enum_restrict, "/restrict")) < 0) {
        return newint(0);
    } else if (level < restriction) {
        oputs("% Restriction level can not be lowered.");
        return newint(0);
    }
    return newint(restriction = level);
}


/********************
 * Generic handlers *
 ********************/   

int do_file_load(args, tinytalk)
    CONST char *args;
    int tinytalk;
{
    Stringp line, cmd;
    int done = 0, error = 0, new_cmd = 1;
    TFILE *file, *old_file = loadfile;
    int old_loadline = loadline;
    int old_loadstart = loadstart;
    int last_cmd_line = 0;
    CONST char *path, *end;
    STATIC_BUFFER(libfile);

    if (!loadfile)
        exiting = 0;

    file = tfopen(expand_filename(args), "r");
    if (!file && !tinytalk && errno == ENOENT && !is_absolute_path(args)) {
        /* Relative file was not found, so look in TFPATH or TFLIBDIR. */
        path = TFPATH && *TFPATH ? TFPATH : TFLIBDIR;
        do {
            while (isspace(*path)) ++path;
            if (!*path) break;
            for (end = path; *end && !isspace(*end); ++end);
            if (!is_absolute_path(path)) {
                eprintf("warning: %.*s: invalid path value", end - path, path);
            } else {
                Sprintf(libfile, 0, "%.*s/%s", end - path, path, args);
                file = tfopen(expand_filename(libfile->s), "r");
            }
            path = end;
        } while (!file && (path = end) && *path);
    }

    if (!file) {
        if (!tinytalk || errno != ENOENT)
            do_hook(H_LOADFAIL, "!%s: %s", "%s %s", args, strerror(errno));
        return 0;
    }

    do_hook(H_LOAD, quietload ? NULL : "%% Loading commands from %s.",
        "%s", file->name);
    oflush();  /* Load could take awhile, so flush pending output first. */

    Stringninit(line, 80);
    Stringninit(cmd, 192);
    loadstart = loadline = 0;
    loadfile = file;  /* if this were done earlier, error msgs would be wrong */
    while (!done) {
        char *p;
        if (exiting) {
            --exiting;
            break;
        }
        if (interrupted()) {
            eprintf("file load interrupted.");
            error = 1;
            break;
        }
        done = !tfgetS(line, loadfile);
        loadline++;
        if (new_cmd) loadstart = loadline;
        for (p = line->s; is_space(*p); p++);     /* strip leading space */
        if (*p) {
            if (line->s[0] == ';') continue;         /* skip comment lines */
            if (new_cmd && is_space(line->s[0]) && last_cmd_line > 0)
                tfprintf(tferr,
                    "%% %s: line %d: warning: possibly missing trailing \\",
                    loadfile->name, last_cmd_line);
            last_cmd_line = loadline;
            Stringcat(cmd, p);
            if (line->s[line->len - 1] == '\\') {
                if (line->len < 2 || line->s[line->len - 2] != '%') {
                    Stringterm(cmd, cmd->len - 1);
                    new_cmd = 0;
                    continue;
                }
            } else {
                p = line->s + line->len - 1;
                while (p > line->s && is_space(*p)) p--;
                if (*p == '\\')
                    eprintf("warning: whitespace following final '\\'");
            }
        } else {
            last_cmd_line = 0;
        }
        new_cmd = 1;
        if (!cmd->len) continue;
        if (*cmd->s == '/') {
            tinytalk = FALSE;
            /* Never use SUB_FULL here.  Libraries will break. */
            process_macro(cmd->s, NULL, SUB_KEYWORD, "\bLOAD");
        } else if (tinytalk) {
            Macro *addworld = find_macro("addworld");
            if (addworld) do_macro(addworld, cmd->s);
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
    loadline = old_loadline;
    loadstart = old_loadstart;

    if (!loadfile)
        exiting = 0;
    if (!exiting)
        breaking = 0;

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
    else if (cstrcmp(args, "on") == 0) set_var_by_id(VAR_beep, 1, NULL);
    else if (cstrcmp(args, "off") == 0) set_var_by_id(VAR_beep, 0, NULL);
    else if (is_digit(*args) && (n = atoi(args)) < 0) return newint(0);

    dobell(n);
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
    if (restriction >= RESTRICT_FILE) {
        eprintf("restricted");
        return newint(0);
    }
    if (*args) return newint(save_macros(args));
    eprintf("missing filename");
    return newint(0);
}

struct Value *handle_exit_command(args)
    char *args;
{
    if (!loadfile) return 0;
    if ((exiting = atoi(args)) <= 0) exiting = 1;
    breaking = -1;
    return newint(1);
}

static struct Value *handle_load_command(args)
    char *args;
{                   
    int quiet = 0, result = 0;
    char c;

    if (restriction >= RESTRICT_FILE) {
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
        set_var_by_id(VAR_hilite, 1, NULL);
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
        set_var_by_id(VAR_gag, 1, NULL);
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
        prob, 0, 0, matching)));
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
    return newint(remove_macro(args, attrs ? attrs : 0, 0));
}


/*********
 * Hooks *
 *********/

static struct Value *handle_hook_command(args)
    char *args;
{
    if (!*args) oprintf("%% Hooks %sabled", hookflag ? "en" : "dis");
    else if (cstrcmp(args, "off") == 0) set_var_by_id(VAR_hook, 0, NULL);
    else if (cstrcmp(args, "on") == 0) set_var_by_id(VAR_hook, 1, NULL);
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

