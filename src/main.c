/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: main.c,v 35004.55 1999/01/31 00:27:47 hawkeye Exp $ */


/***********************************************
 * Fugue main routine                          *
 *                                             *
 * Initializes many internal global variables, *
 * determines initial world (if any), reads    *
 * configuration file, and calls main loop in  *
 * socket.c                                    *
 ***********************************************/

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "macro.h"
#include "output.h"
#include "signals.h"
#include "command.h"
#include "keyboard.h"
#include "variable.h"
#include "tty.h"	/* no_tty */
#include "expand.h"
#include "expr.h"
#include "process.h"
#include "search.h"

CONST char sysname[] = UNAME;

/* For customized versions, please add a unique identifer (e.g., your initials)
 * to the version number, and put a brief description of the modifications
 * in the mods[] string.
 */
CONST char version[] = "TinyFugue version 4.0 stable 1";
CONST char mods[] = "";

CONST char copyright[] =
    "Copyright (C) 1993 - 1999 Ken Keys (hawkeye@tcp.com)";

CONST char contrib[] =
#ifdef PLATFORM_OS2
    "OS/2 support written by Andreas Sahlbach (asa@stardiv.de)";
#else
    "";
#endif

int restriction = 0;

static void FDECL(read_configuration,(CONST char *fname));
int FDECL(main,(int argc, char **argv));

int main(argc, argv)
    int argc;
    char *argv[];
{
    char *opt, *argv0 = argv[0];
    char *configfile = NULL, *command = NULL, *libdir = NULL;
    int worldflag = TRUE;
    int autologin = -1, quietlogin = -1, autovisual = TRUE;
    Stringp scratch;

    while (--argc > 0 && (*++argv)[0] == '-') {
        if (!(*argv)[1]) { argc--; argv++; break; }
        for (opt = *argv + 1; *opt; )
            switch (*opt++) {
            case 'l':
                autologin = FALSE;
                break;
            case 'q':
                quietlogin = TRUE;
                break;
            case 'n':
                worldflag = FALSE;
                break;
            case 'v':
                autovisual = FALSE;
                break;
            case 'f':
                configfile = opt;
                goto nextarg;
            case 'c':
                command = opt;
                goto nextarg;
            case 'L':
                libdir = opt;
                goto nextarg;
            default:
                fprintf(stderr, "%s: illegal option -- %c\n", argv0, *--opt);
                goto error;
            }
        nextarg: /* empty statement */;
    }
    if (argc > 2) {
    error:
        fprintf(stderr, "Usage: %s [-L<dir>] [-f[<file>]] [-c<cmd>] [-nlq] [<world>]\n", argv0);
        fprintf(stderr, "       %s [-L<dir>] [-f[<file>]] [-c<cmd>] <host> <port>\n", argv0);
        fputs("Options:\n", stderr);
        fputs("  -L<dir>   use <dir> as library directory (%TFLIBDIR)\n", stderr);
        fputs("  -f        don't load personal config file (.tfrc)\n", stderr);
        fputs("  -f<file>  load <file> instead of config file\n", stderr);
        fputs("  -c<cmd>   execute <cmd> after loading config file\n", stderr);
        fputs("  -n        no automatic first connection\n", stderr);
        fputs("  -l        no automatic login/password\n", stderr);
        fputs("  -q        quiet login\n", stderr);
        fputs("  -v        no automatic visual mode\n", stderr);
        fputs("Arguments:\n", stderr);
        fputs("  <host>    hostname or IP address\n", stderr);
        fputs("  <port>    port number or name\n", stderr);
        fputs("  <world>   connect to <world> defined by addworld()\n", stderr);
        exit(1);
    }

    puts("\n\n\n\n");
    puts(version);
    puts(copyright);
    puts("Type `/help copyright' for more information.");
    if (*contrib) puts(contrib);
    if (*mods) puts(mods);
#ifdef SOCKS
    SOCKSinit(argv0);  /* writes message to stdout */
#endif
    puts("Regexp package is Copyright (c) 1986 by University of Toronto.");
    puts("Type `/help', `/help topics', or `/help intro' for help.");
    puts("Type `/quit' to quit tf.");
    puts("");

    SRAND(getpid());			/* seed random generator */
    init_malloc();			/* malloc.c   */
    init_tfio();			/* tfio.c     */
    init_util1();			/* util.c     */
    init_expand();			/* expand.c   */
    init_signals();			/* signals.c  */
    init_variables();			/* variable.c */
    init_sock();			/* socket.c   */
    init_macros();			/* macro.c    */
    init_histories();			/* history.c  */
    init_output();			/* output.c   */
    init_keyboard();			/* keyboard.c */
    init_util2();			/* util.c     */

    Stringinit(scratch);
    if (libdir) {
        set_var_by_name("TFLIBDIR", libdir, 0);
    }
    if (!ffindglobalvar("TFLIBRARY")) {
        Sprintf(scratch, 0, "%s/stdlib.tf", TFLIBDIR);
        set_var_by_name("TFLIBRARY", scratch->s, 0);
    }
    if (!ffindglobalvar("TFHELP")) {
        Sprintf(scratch, 0, "%s/tf-help", TFLIBDIR);
        set_var_by_name("TFHELP", scratch->s, 0);
    }
    Stringfree(scratch);

    read_configuration(configfile);

    if (command) {
        process_macro(command, NULL, sub, "\bSTART");
    }

    /* If %visual was not explicitly set, set it now. */
    if (getintvar(VAR_visual) < 0 && !no_tty)
        set_var_by_id(VAR_visual, autovisual, NULL);

    if (argc > 0 || worldflag) {
        if (autologin < 0) autologin = login;
        if (quietlogin < 0) quietlogin = quietflag;
        if (argc == 0)
            openworld(NULL, NULL, autologin, quietlogin);
        else if (argc == 1)
            openworld(argv[0], NULL, autologin, quietlogin);
        else /* if (argc == 2) */
            openworld(argv[0], argv[1], autologin, quietlogin);
    } else {
        do_hook(H_WORLD, "---- No world ----", "");
    }

    main_loop();

    kill_procs();
    fix_screen();
    reset_tty();

#ifdef DMALLOC
    free_macros();
    free_worlds();
    free_histories();
    free_output();
    free_vars();
    free_keyboard();
    free_search();
    free_expand();
    free_expr();
    free_help();
    free_util();
    free_reserve();
    debug_mstats("tf");
#endif

    return 0;
}

static void read_configuration(fname)
    CONST char *fname;
{
    if (!do_file_load(getvar("TFLIBRARY"), FALSE))
        die("Can't read required library.", 0);

    if (fname) {
        if (*fname) do_file_load(fname, FALSE);
        return;
    }

    (void)(   /* ignore value of expression */
        do_file_load("~/.tfrc", TRUE) ||
        do_file_load("~/tfrc", TRUE) ||
        do_file_load("./.tfrc", TRUE) ||
        do_file_load("./tfrc", TRUE)
    );

    /* support for old fashioned .tinytalk files */
    do_file_load((fname = getvar("TINYTALK")) ? fname : "~/.tinytalk", TRUE);
}

