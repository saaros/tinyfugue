/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: command.c,v 35004.121 2003/12/10 09:12:02 hawkeye Exp $";


/*****************************************************************
 * Fugue command handlers
 *****************************************************************/

#include "config.h"
#include "port.h"
#include "tf.h"
#include "util.h"
#include "search.h"
#include "tfio.h"
#include "commands.h"
#include "command.h"
#include "world.h"	/* World, find_world() */
#include "socket.h"	/* openworld() */
#include "output.h"	/* oflush(), dobell() */
#include "macro.h"
#include "keyboard.h"	/* find_key(), find_efunc() */
#include "expand.h"     /* macro_run() */
#include "signals.h"    /* suspend(), shell() */
#include "variable.h"

int exiting = 0;

static char *pattern, *body;
static int quietload = 0;

static void split_args(char *args);

static HANDLER (handle_beep_command);
static HANDLER (handle_bind_command);
static HANDLER (handle_connect_command);
static HANDLER (handle_core_command);
static HANDLER (handle_features_command);
static HANDLER (handle_gag_command);
static HANDLER (handle_hilite_command);
static HANDLER (handle_hook_command);
static HANDLER (handle_lcd_command);
static HANDLER (handle_let_command);
static HANDLER (handle_limit_command);
static HANDLER (handle_load_command);
static HANDLER (handle_localecho_command);
static HANDLER (handle_quit_command);
static HANDLER (handle_relimit_command);
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
static HANDLER (handle_unlimit_command);
static HANDLER (handle_version_command);

  /* It is IMPORTANT that the commands be in alphabetical order! */

static BuiltinCmd cmd_table[] =
{
/*   name	    function		    reserved? */
  { "BEEP"        , handle_beep_command        , 0 },
  { "BIND"        , handle_bind_command        , 0 },
  { "CONNECT"     , handle_connect_command     , 0 },
  { "CORE"        , handle_core_command        , 0 },
  { "DC"          , handle_dc_command          , 0 },
  { "DEF"         , handle_def_command         , 0 },
  { "DOKEY"       , handle_dokey_command       , 0 },
  { "EDIT"        , handle_edit_command        , 0 },
  { "EVAL"        , handle_eval_command        , 1 },
  { "EXIT"        , handle_exit_command        , 0 },
  { "EXPORT"      , handle_export_command      , 0 },
  { "FEATURES"    , handle_features_command    , 0 },
  { "FG"          , handle_fg_command          , 0 },
  { "GAG"         , handle_gag_command         , 0 },
  { "HELP"        , handle_help_command        , 0 },
  { "HILITE"      , handle_hilite_command      , 0 },
  { "HISTSIZE"    , handle_histsize_command    , 0 },
  { "HOOK"        , handle_hook_command        , 0 },
  { "INPUT"       , handle_input_command       , 0 },
  { "KILL"        , handle_kill_command        , 0 },
  { "LCD"         , handle_lcd_command         , 0 },
  { "LET"         , handle_let_command         , 0 },
  { "LIMIT"       , handle_limit_command       , 0 },
  { "LIST"        , handle_list_command        , 0 },
  { "LISTSOCKETS" , handle_listsockets_command , 0 },
  { "LISTSTREAMS" , handle_liststreams_command , 0 },
  { "LISTVAR"     , handle_listvar_command     , 0 },
  { "LISTWORLDS"  , handle_listworlds_command  , 0 },
  { "LOAD"        , handle_load_command        , 0 },
  { "LOCALECHO"   , handle_localecho_command   , 0 },
  { "LOG"         , handle_log_command         , 0 },
  { "PROMPT"      , handle_prompt_command      , 0 },
  { "PS"          , handle_ps_command          , 0 },
  { "PURGE"       , handle_purge_command       , 0 },
  { "QUIT"        , handle_quit_command        , 0 },
  { "QUOTE"       , handle_quote_command       , 0 },
  { "RECALL"      , handle_recall_command      , 0 },
  { "RECORDLINE"  , handle_recordline_command  , 0 },
  { "RELIMIT"     , handle_relimit_command     , 0 },
  { "REPEAT"      , handle_repeat_command      , 0 },
  { "RESTRICT"    , handle_restrict_command    , 0 },
  { "SAVE"        , handle_save_command        , 0 },
  { "SAVEWORLD"   , handle_saveworld_command   , 0 },
  { "SET"         , handle_set_command         , 0 },
  { "SETENV"      , handle_setenv_command      , 0 },
  { "SH"          , handle_sh_command          , 0 },
  { "SHIFT"       , handle_shift_command       , 0 },
  { "SUSPEND"     , handle_suspend_command     , 0 },
  { "TRIGGER"     , handle_trigger_command     , 0 },  
  { "TRIGPC"      , handle_trigpc_command      , 0 },
  { "UNBIND"      , handle_unbind_command      , 0 },
  { "UNDEF"       , handle_undef_command       , 0 },
  { "UNDEFN"      , handle_undefn_command      , 0 },
  { "UNLIMIT"     , handle_unlimit_command     , 0 },
  { "UNSET"       , handle_unset_command       , 0 },
  { "UNWORLD"     , handle_unworld_command     , 0 },
  { "VERSION"     , handle_version_command     , 0 },
  { "WATCHDOG"    , handle_watchdog_command    , 0 },
  { "WATCHNAME"   , handle_watchname_command   , 0 },
};

#define NUM_CMDS (sizeof(cmd_table) / sizeof(BuiltinCmd))

STATIC_STRING(limit_none, "% No previous limit.", 0);
STATIC_STRING(limit_no_match, "% No lines matched criteria.", 0);

/*****************************************
 * Find, process and run commands/macros *
 *****************************************/

BuiltinCmd *find_builtin_cmd(const char *name)
{
    return (BuiltinCmd *)binsearch((void*)name, (void*)cmd_table,
        NUM_CMDS, sizeof(BuiltinCmd), cstrstructcmp);
}

static struct Value *handle_trigger_command(String *args, int offset)
{
    World *world = NULL;
    int usedefault = TRUE, is_global = FALSE, result = 0, exec_list_long = 0;
    int opt;
    int hooknum = -1;
    String *old_incoming_text;
    char *ptr;

    if (!borg) return shareval(val_zero);

    startopt(args, "gw:h:nl");
    while ((opt = nextopt(&ptr, NULL, NULL, &offset))) {
        switch (opt) {
            case 'g':
                usedefault = FALSE;
                is_global = TRUE;
                break;
            case 'w':
                usedefault = FALSE;
                if (!(world = named_or_current_world(ptr)))
                    return shareval(val_zero);
                break;
            case 'h':
		if ((hooknum = hookname2int(ptr)) < 0)
		    return shareval(val_zero);
                break;
            case 'n':
                if (exec_list_long == 0)
		    exec_list_long = 1;
                break;
            case 'l':
                exec_list_long = 2;
                break;
            default:
                return shareval(val_zero);
          }
    }

    if (usedefault) {
        is_global = TRUE;
        world = xworld();
    }

    old_incoming_text = incoming_text;
    (incoming_text = Stringodup(args, offset))->links++;

    result = find_and_run_matches(NULL, hooknum, &incoming_text, world,
	is_global, exec_list_long);

    Stringfree(incoming_text);
    incoming_text = old_incoming_text;
    return newint(result);
}

int handle_substitute_func(
    String *src,
    const char *attrstr,
    int inline_flag)
{
    attr_t attrs;
    String *newstr;

    if (!incoming_text) {
        eprintf("not called from trigger");
        return 0;
    }

    if (!parse_attrs(attrstr, &attrs, 0))
	return 0;

    newstr = inline_flag ? decode_attr(src, 0) : Stringdup(src);
    if (!newstr)
	return 0;
    /* Start w/ incoming_text->attrs, adjust with src->attrs and attrstr. */
    newstr->attrs = adj_attr(incoming_text->attrs, src->attrs);
    newstr->attrs = adj_attr(newstr->attrs, attrs);
    newstr->time = incoming_text->time;

    Stringfree(incoming_text);
    (incoming_text = newstr)->links++;
    return 1;
}

/**********
 * Worlds *
 **********/

static struct Value *handle_connect_command(String *args, int offset)
{
    char *host, *port = NULL;
    int opt, flags = 0;

    if (login) flags |= CONN_AUTOLOGIN;
    if (quietflag) flags |= CONN_QUIETLOGIN;

    startopt(args, "lqxfb");
    while ((opt = nextopt(NULL, NULL, NULL, &offset))) {
        switch (opt) {
            case 'l':  flags &= ~CONN_AUTOLOGIN; break;
            case 'q':  flags |= CONN_QUIETLOGIN; break;
            case 'x':  flags |= CONN_SSL; break;
            case 'f':  flags |= CONN_FG; break;
            case 'b':  flags |= CONN_BG; break;
            default:   return shareval(val_zero);
        }
    }
    host = args->data + offset;
    for (port = host; *port && !is_space(*port); port++);
    if (*port) {
        *port = '\0';
        while (is_space(*++port));
    }
    return newint(openworld(host, *port ? port : NULL, flags));
}

static struct Value *handle_localecho_command(String *args, int offset)
{
    if (!(args->len - offset)) return newint(local_echo(-1));
    else if (cstrcmp(args->data + offset, "on") == 0) local_echo(1);
    else if (cstrcmp(args->data + offset, "off") == 0) local_echo(0);
    return shareval(val_one);
}

/*************
 * Variables *
 *************/

static struct Value *handle_set_command(String *args, int offset)
{
    return newint(do_set(args, offset, FALSE, FALSE));
}

static struct Value *handle_setenv_command(String *args, int offset)
{
    return newint(do_set(args, offset, TRUE, FALSE));
}

static struct Value *handle_let_command(String *args, int offset)
{
    return newint(do_set(args, offset, FALSE, TRUE));
}

/********
 * Misc *
 ********/
static struct Value *handle_quit_command(String *args, int offset)
{
    quit_flag = 1;
    return shareval(val_one);
}

static struct Value *handle_sh_command(String *args, int offset)
{
    const char *cmd;
    char c;
    int quiet = 0;

    if (restriction >= RESTRICT_SHELL) {
        eprintf("restricted");
        return shareval(val_zero);
    }

    startopt(args, "q");
    while ((c = nextopt(NULL, NULL, NULL, &offset))) {
        if (c == 'q') quiet++;
        else return shareval(val_zero);
    }

    if (args->len - offset) {
        cmd = args->data + offset;
        if (!quiet)
            do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "command", cmd);
    } else {
        /* Note: on unix, system("") does nothing, but SHELL should always be
         * defined, so it won't happen.  On os/2, SHELL usually isn't defined,
         * and system("") will choose choose the default interpreter; SHELL
         * will be used if defined, of course.
         */
        cmd = getvar("SHELL");
        if (!quiet)
            do_hook(H_SHELL, "%% Executing %s: %s", "%s %s", "shell", cmd);
            /* XXX BUG: SHELL hook might unset %SHELL, then we're screwed. */
    }
    return newint(shell(cmd));
}

static struct Value *handle_suspend_command(String *args, int offset)
{
    return newint(suspend());
}

static struct Value *handle_version_command(String *args, int offset)
{
    oprintf("%% %s.", version);
    oprintf("%% %s.", copyright);
    if (*contrib) oprintf("%% %s", contrib);
    if (*mods)    oprintf("%% %s", mods);
    if (*sysname) oprintf("%% Built for %s", sysname);
    return shareval(val_one);
}

/* for debugging */
static struct Value *handle_core_command(String *args, int offset)
{
    internal_error(__FILE__, __LINE__, "command: /core %s",
	args->data + offset);
    core("/core command", __FILE__, __LINE__, 0);
    return NULL; /* never reached */
}

static struct Value *handle_features_command(String *args, int offset)
{
    struct feature *f;

    if (args->len > offset) {
	for (f = features; f->name; f++) {
	    if (cstrcmp(f->name, args->data + offset) == 0)
		return *f->flag ? shareval(val_one) : shareval(val_zero);
	}
	return shareval(val_zero);
    } else {
	oputline(featurestr);
	return shareval(val_one);
    }
}

static struct Value *handle_lcd_command(String *args, int offset)
{
    char buffer[PATH_MAX + 1], *name;

    if (restriction >= RESTRICT_FILE) {
        eprintf("restricted");
        return shareval(val_zero);
    }

    name = expand_filename(stripstr(args->data + offset));
    if (*name && chdir(name) < 0) {
        operror(name);
        return shareval(val_zero);
    }

#if HAVE_GETCWD
    oprintf("%% Current directory is %s", getcwd(buffer, PATH_MAX));
#else
# if HAVE_GETWD
    oprintf("%% Current directory is %s", getwd(buffer));
# endif
#endif
    return shareval(val_one);
}


int handle_echo_func(
    String *src,
    const char *attrstr,
    int inline_flag,
    const char *dest)
{
    attr_t attrs;
    int raw = 0;
    TFILE *file = tfout;
    World *world = NULL;
    String *newstr;

    if (!parse_attrs(attrstr, &attrs, 0))
        return 0;
    switch(*dest) {
        case 'r':  raw = 1;       break;
        case 'o':  file = tfout;  break;
        case 'e':  file = tferr;  break;
        case 'a':  file = tfalert;  break;
        case 'w':
            dest++;
            if (!(world = named_or_current_world(dest)))
                return 0;
            break;
        default:
            eprintf("illegal destination '%c'", *dest);
            return 0;
    }
    if (raw) {
        write(STDOUT_FILENO, src->data, src->len);
        return 1;
    }

    newstr = inline_flag ? decode_attr(src, 0) : Stringdup(src);
    if (!newstr)
	return 0;
    newstr->links++;
    newstr->attrs = adj_attr(newstr->attrs, attrs);

    if (world)
        world_output(world, newstr);
    else
        tfputline(newstr, file);

    Stringfree(newstr);
    return 1;
}


static struct Value *handle_restrict_command(String *args, int offset)
{
    int level;
    static String enum_restrict[] = {
        STRING_LITERAL("none"), STRING_LITERAL("shell"),
        STRING_LITERAL("file"), STRING_LITERAL("world"),
        STRING_NULL };

    if (!(args->len - offset)) {
        oprintf("%% restriction level: %S", &enum_restrict[restriction]);
        return newint(restriction);
    } else if ((level = enum2int(args->data + offset, 0, enum_restrict,
        "/restrict")) < 0)
    {
        return shareval(val_zero);
    } else if (level < restriction) {
        oputs("% Restriction level can not be lowered.");
        return shareval(val_zero);
    }
    return newint(restriction = level);
}

static struct Value *handle_limit_command(String *args, int offset)
{
    int mflag = matching;
    int got_opts = 0;
    int result, had_filter, has_new_pat;
    char c, *ptr;
    Screen *screen = display_screen;
    int attr_flag = 0, sense = 1;
    Pattern pat;

    startopt(args, "avm:");
    while ((c = nextopt(&ptr, NULL, NULL, &offset))) {
	got_opts++;
        switch (c) {
	case 'a':
	    attr_flag = 1;
	    break;
	case 'v':
	    sense = 0;
	    break;
        case 'm':
            if ((mflag = enum2int(ptr, 0, enum_match, "-m")) < 0)
		return shareval(val_zero);
            break;
	default:
	    return shareval(val_zero);
	}
    }

    if (!got_opts && offset == args->len) {
        result = screen_has_filter(screen);
	goto end;
    }
    if ((has_new_pat = (offset != args->len))) {
	if (!init_pattern(&pat, args->data + offset, mflag)) {
	    result = 0;
	    goto end;
	}
    }
    had_filter = screen_has_filter(screen);
    clear_screen_filter(screen);
    set_screen_filter(screen, has_new_pat ? &pat : NULL, attr_flag, sense);

    if (!(result = redraw_window(screen, 0))) {
	alert(limit_no_match);
	redraw_window(screen, 0); /* XXX ? */
    }
    update_status_field(NULL, STAT_MORE);

end:
    return result ? shareval(val_one) : shareval(val_zero);
}

static struct Value *handle_relimit_command(String *args, int offset)
{
    Screen *screen = display_screen;
    Value *result = val_one;

    if (!enable_screen_filter(screen)) {
	alert(limit_none);
	result = val_zero;
    } else if (!redraw_window(screen, 0)) {
	alert(limit_no_match);
	redraw_window(screen, 0); /* XXX ? */
	result = val_zero;
    }
    update_status_field(NULL, STAT_MORE);
    return shareval(result);
}

static struct Value *handle_unlimit_command(String *args, int offset)
{
    Screen *screen = display_screen;

    if (!screen_has_filter(screen))
	return shareval(val_zero);
    clear_screen_filter(screen);
    if (!screen->paused)
	screen_end(0);
    redraw_window(screen, 0);
    update_status_field(NULL, STAT_MORE);
    return shareval(val_one);
}


/********************
 * Generic handlers *
 ********************/   

/* Returns -1 if file can't be read, 0 for an error within the file, or 1 for
 * success.
 */
int do_file_load(const char *args, int tinytalk)
{
    AUTO_BUFFER(line);
    AUTO_BUFFER(cmd);
    int error = 0, new_cmd = 1;
    TFILE *file, *old_file = loadfile;
    int old_loadline = loadline;
    int old_loadstart = loadstart;
    int last_cmd_line = 0;
    const char *path, *end;
    STATIC_BUFFER(libfile);

    if (!loadfile)
        exiting = 0;

    file = tfopen(expand_filename(args), "r");
    if (!file && !tinytalk && errno == ENOENT && !is_absolute_path(args)) {
        /* Relative file was not found, so look in TFPATH or TFLIBDIR. */
        path = TFPATH && *TFPATH ? TFPATH : TFLIBDIR;
        do {
            while (is_space(*path)) ++path;
            if (!*path) break;
            for (end = path; *end && !is_space(*end); ++end);
            if (!is_absolute_path(path)) {
                eprintf("warning: %.*s: invalid path value", end - path, path);
            } else {
                Sprintf(libfile, "%.*s/%s", end - path, path, args);
                file = tfopen(expand_filename(libfile->data), "r");
            }
            path = end;
        } while (!file && (path = end) && *path);
    }

    if (!file) {
        if (!tinytalk || errno != ENOENT)
            do_hook(H_LOADFAIL, "!%s: %s", "%s %s", args, strerror(errno));
        return -1;
    }

    do_hook(H_LOAD, quietload ? NULL : "%% Loading commands from %s.",
        "%s", file->name);
    oflush();  /* Load could take awhile, so flush pending output first. */

    Stringninit(line, 80);
    Stringninit(cmd, 192);
    loadstart = loadline = 0;
    loadfile = file;  /* if this were done earlier, error msgs would be wrong */
    while (1) {
        int i;
        if (exiting) {
            --exiting;
            break;
        }
        if (interrupted()) {
            eprintf("file load interrupted.");
            error = 1;
            break;
        }
        if (!tfgetS(line, loadfile))
	    break;
        loadline++;
        if (new_cmd) loadstart = loadline;
        if (line->data[0] == ';' || line->data[0] == '#') /* ignore comments */
	    continue;
        for (i = 0; is_space(line->data[i]); i++);   /* skip leading space */
        if (line->data[i]) {
            if (new_cmd && is_space(line->data[0]) && last_cmd_line > 0)
                tfprintf(tferr,
                    "%% %s: line %d: warning: possibly missing trailing \\",
                    loadfile->name, last_cmd_line);
            last_cmd_line = loadline;
            SStringocat(cmd, line, i);
            if (line->data[line->len - 1] == '\\') {
                if (line->len < 2 || line->data[line->len - 2] != '%') {
                    Stringtrunc(cmd, cmd->len - 1);
                    new_cmd = 0;
                    continue;
                }
            } else {
                i = line->len - 1;
                while (i > 0 && is_space(line->data[i])) i--;
                if (line->data[i] == '\\')
                    eprintf("warning: whitespace following final '\\'");
            }
        } else {
            last_cmd_line = 0;
        }
        new_cmd = 1;
        if (!cmd->len) continue;
        if (*cmd->data == '/') {
            tinytalk = FALSE;
            /* Never use SUB_FULL here.  Libraries will break. */
            macro_run(cmd, 0, NULL, 0, SUB_KEYWORD, "\bLOAD");
        } else if (tinytalk) {
	    static int warned = 0;
            Macro *addworld = find_macro("addworld");
            if (addworld && do_macro(addworld, cmd, 0, USED_NAME, NULL) &&
		!user_result->u.ival && !warned)
	    {
		eprintf("(This line was implicitly treated as an /addworld "
		    "because it occured before the first '/' line and did not "
		    "start with a '/', ';', or '#'.)");
		warned = 1;
	    }
        } else {
            eprintf("Invalid command. Aborting.");
            error = 1;
            break;
        }
        Stringtrunc(cmd, 0);
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

    return !error;
}


/**************************
 * Toggles with arguments *
 **************************/

static struct Value *handle_beep_command(String *args, int offset)
{
    int n = 0;

    if (!(args->len - offset)) n = 3;
    else if (cstrcmp(args->data + offset, "on") == 0)
        set_var_by_id(VAR_beep, 1);
    else if (cstrcmp(args->data + offset, "off") == 0)
        set_var_by_id(VAR_beep, 0);
    else if ((n = atoi(args->data + offset)) < 0)
        return shareval(val_zero);

    dobell(n);
    return shareval(val_one);
}


/**********
 * Macros *
 **********/

static struct Value *handle_undef_command(String *args, int offset)
{
    char *name, *next;
    int result = 0;

    next = args->data + offset;
    while (*(name = stringarg(&next, NULL)))
        result += remove_macro_by_name(name);
    return newint(result);
}

static struct Value *handle_save_command(String *args, int offset)
{
    if (restriction >= RESTRICT_FILE) {
        eprintf("restricted");
        return shareval(val_zero);
    }
    if (args->len - offset) return newint(save_macros(args, offset));
    eprintf("missing filename");
    return shareval(val_zero);
}

struct Value *handle_exit_command(String *args, int offset)
{
    if (!loadfile) return shareval(val_zero);
    if ((exiting = atoi(args->data + offset)) <= 0) exiting = 1;
    return shareval(val_one);
}

static struct Value *handle_load_command(String *args, int offset)
{                   
    int quiet = 0, result = 0;
    char c;

    if (restriction >= RESTRICT_FILE) {
        eprintf("restricted");
        return shareval(val_zero);
    }

    startopt(args, "q");
    while ((c = nextopt(NULL, NULL, NULL, &offset))) {
        if (c == 'q') quiet = 1;
        else return shareval(val_zero);
    }

    quietload += quiet;
    if (args->len - offset)
        result = (do_file_load(stripstr(args->data + offset), FALSE) > 0);
    else eprintf("missing filename");
    quietload -= quiet;
    return newint(result);
}

/* Generic utility to split arguments into pattern and body.
 * Note: I can get away with this only because none of the functions
 * that use it are reentrant.  Be careful.
 */
static void split_args(char *args)
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

static struct Value *handle_hilite_command(String *args, int offset)
{
    if (!(args->len - offset)) {
        set_var_by_id(VAR_hilite, 1);
        oputs("% Hilites enabled.");
        return shareval(val_zero);
    } else {
        split_args(args->data + offset);
        return newint(add_macro(new_macro(pattern, "", NULL, NULL, body,
            hpri, 100, F_HILITE, 0, matching)));
    }
}


/********
 * Gags *
 ********/

static struct Value *handle_gag_command(String *args, int offset)
{
    if (!(args->len - offset)) {
        set_var_by_id(VAR_gag, 1);
        oputs("% Gags enabled.");
        return shareval(val_zero);
    } else {
        split_args(args->data + offset);
        return newint(add_macro(new_macro(pattern, "", NULL, NULL, body,
            gpri, 100, F_GAG, 0, matching)));
    }
}


/************
 * Triggers *
 ************/

static struct Value *handle_trigpc_command(String *args, int offset)
{
    int pri, prob;
    char *ptr = args->data + offset;

    if ((pri = numarg(&ptr)) < 0) return shareval(val_zero);
    if ((prob = numarg(&ptr)) < 0) return shareval(val_zero);
    split_args(ptr);
    return newint(add_macro(new_macro(pattern, "", NULL, NULL, body, pri,
        prob, 0, 0, matching)));
}


/*********
 * Hooks *
 *********/

static struct Value *handle_hook_command(String *args, int offset)
{
    if (!(args->len - offset))
        oprintf("%% Hooks %sabled", hookflag ? "en" : "dis");
    else if (cstrcmp(args->data + offset, "off") == 0)
        set_var_by_id(VAR_hook, 0);
    else if (cstrcmp(args->data + offset, "on") == 0)
        set_var_by_id(VAR_hook, 1);
    else {
        split_args(args->data + offset);
        return newint(add_hook(pattern, body));
    }
    return shareval(val_one);
}


/********
 * Keys *
 ********/

static struct Value *handle_unbind_command(String *args, int offset)
{
    Macro *macro;

    if (!(args->len - offset)) return shareval(val_zero);
    if ((macro = find_key(print_to_ascii(args->data + offset)->data)))
        kill_macro(macro);
    else eprintf("No binding for %s", args->data + offset);
    return newint(!!macro);
}

static struct Value *handle_bind_command(String *args, int offset)
{
    Macro *spec;

    if (!(args->len - offset)) return shareval(val_zero);
    split_args(args->data + offset);
    spec = new_macro(NULL, print_to_ascii(pattern)->data, NULL, NULL, body,
        0, 100, 0, FALSE, 0);
    return newint(add_macro(spec));
}

