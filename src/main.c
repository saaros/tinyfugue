/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: main.c,v 33000.3 1994/04/16 05:10:40 hawkeye Exp $ */


/***********************************************
 * Fugue main routine                          *
 *                                             *
 * Initializes many internal global variables, *
 * determines initial world (if any), reads    *
 * configuration file, and calls main loop in  *
 * socket.c                                    *
 ***********************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "macro.h"
#include "output.h"
#include "signals.h"
#include "tty.h"
#include "command.h"
#include "keyboard.h"

char version[] = "TinyFugue version 3.3 beta 4, Copyright (C) 1993, 1994 Ken Keys";

int restrict = 0;

static void FDECL(read_configuration,(char *fname));
int FDECL(main,(int argc, char **argv));

int main(argc, argv)
    int argc;
    char *argv[];
{
    char *opt, *argv0 = argv[0], *configfile = NULL;
    int opterror = FALSE;
    int worldflag = TRUE;
    int autologin = -1, quietlogin = -1;

    while (--argc > 0 && (*++argv)[0] == '-' && !opterror) {
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
            case 'f':
                if (configfile) FREE(configfile);
                configfile = STRDUP(opt);
                while (*opt) opt++;
                break;
            default:
                opterror = TRUE;
                break;
            }
    }
    if (opterror || argc > 2) {
        char usage[256];
        sprintf(usage,
            "Usage: %s %s [-nlq] [<world>]\n       %s %s <host> <port>\n",
            argv0, "[-f<file>]", argv0, "[-f<file>]");
        die(usage);
    }

    SRAND(getpid());		/* seed random generator */
    init_util();		/* util.c     */
    init_signals();		/* signals.c  */
    init_variables();		/* variable.c */
    init_sock();		/* socket.c   */
    init_macros();		/* macro.c    */
    init_histories();		/* history.c  */
    init_tty();			/* tty.c      */
    init_output();		/* output.c   */
    init_keyboard();		/* keyboard.c */
    init_mail();		/* util.c     */
    tog_sigquit();              /* signals.c  */

    oputs(version);
    oputs("Regexp package is Copyright (c) 1986 by University of Toronto.");
    oputs("Type \"/help\" for help.");

    read_configuration(configfile);

    if (worldflag) {
        if (autologin < 0) autologin = login;
        if (quietlogin < 0) quietlogin = quiet;
        if (argc == 0)
            openworld(NULL, NULL, autologin, quietlogin);
        else if (argc == 1)
            openworld(argv[0], NULL, autologin, quietlogin);
        else /* if (argc == 2) */
            openworld(argv[0], argv[1], autologin, quietlogin);
    }

    main_loop();
    return 0;
}

static void read_configuration(fname)
    char *fname;
{
    char *lib;

    if (!(lib = getvar("TFLIBRARY"))) setvar("TFLIBRARY", lib = TFLIBRARY, 0);
    if (!do_file_load(lib, FALSE)) die("Can't read required library.");

    if (fname) {
        if (*fname) do_file_load(fname, FALSE);
        return;
    }

    do_file_load(TFRC, TRUE);

    /* support for old fashioned .tinytalk files */
    if (!(fname = getvar("TINYTALK"))) fname = TINYTALK;
    do_file_load(fname, TRUE);
}

