/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: main.c,v 35004.25 1997/11/07 05:51:16 hawkeye Exp $ */


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

CONST char sysname[] = UNAME;

/* For customized versions, please add a unique identifer (e.g., your initials)
 * to the version number, and put a brief description of the modifications
 * in the mods[] string.
 */
CONST char version[] = "TinyFugue version 4.0 alpha 2";
CONST char mods[] = "";

CONST char copyright[] =
    "Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys (hawkeye@tcp.com)";

CONST char contrib[] =
#ifdef PLATFORM_OS2
    "OS/2 support written by Andreas Sahlbach (asa@stardiv.de)";
#else
    "";
#endif

int restrict = 0;

static void FDECL(read_configuration,(CONST char *fname));
int FDECL(main,(int argc, char **argv));

int main(argc, argv)
    int argc;
    char *argv[];
{
    char *opt, *argv0 = argv[0], *configfile = NULL;
    int worldflag = TRUE;
    int autologin = -1, quietlogin = -1, autovisual = TRUE;

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
                if (configfile) FREE(configfile);
                configfile = STRDUP(opt);
                while (*opt) opt++;
                break;
            default:
                fprintf(stderr, "%s: illegal option -- %c\n", argv0, *--opt);
                goto error;
            }
    }
    if (argc > 2) {
    error:
        fprintf(stderr, "Usage: %s [-f[<file>]] [-nlq] [<world>]\n", argv0);
        fprintf(stderr, "       %s [-f[<file>]] <host> <port>\n", argv0);
        fputs("Options:\n", stderr);
        fputs("  -f         don't read personal config file\n", stderr);
        fputs("  -f<file>   read <file> instead of config file\n", stderr);
        fputs("  -n         no connection\n", stderr);
        fputs("  -l         no automatic login\n", stderr);
        fputs("  -q         quiet login\n", stderr);
        fputs("  -v         no automatic visual mode\n", stderr);
        fputs("Arguments:\n", stderr);
        fputs("  <host>     hostname or IP address\n", stderr);
        fputs("  <port>     port number or name\n", stderr);
        fputs("  <world>    connect to <world> defined by /addworld\n", stderr);
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

    read_configuration(configfile);
    if (configfile) FREE(configfile);

    /* if %visual was not explicitly set, turn it on */
    if (autovisual && getintvar(VAR_visual) < 0 && !no_tty)
        setvar("visual", "1", FALSE);

    if (worldflag) {
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
    free_help();
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

    do_file_load("~/.tfrc", TRUE) ||
    do_file_load("~/tfrc", TRUE) ||
    do_file_load("./.tfrc", TRUE) ||
    do_file_load("./tfrc", TRUE);

    /* support for old fashioned .tinytalk files */
    do_file_load((fname = getvar("TINYTALK")) ? fname : "~/.tinytalk", TRUE);
}

