/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: main.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


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
#include "expand.h"
#include "keyboard.h"

char version[] = "TinyFugue version 3.2 beta 1, Copyright (C) 1993 Ken Keys";

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

    SRANDOM(getpid());		/* seed random generator */
    init_sock();		/* socket.c   */
    init_util();		/* util.c     */
    init_signals();		/* signal.c   */
    init_variables();		/* variable.c */
    init_macros();		/* macro.c    */
    init_histories();		/* history.c  */
    init_output();		/* output.c   */
    init_keyboard();		/* keyboard.c */
    init_tty();			/* tty.c      */
    init_values();		/* variable.c */
    init_mail();		/* util.c     */

    oputs(version);
    oputs("Regexp package is Copyright (c) 1986 by University of Toronto.");
    oputs("Type \"/help\" for help.");

    read_configuration(configfile);

    if (worldflag) {
        World *world = NULL;
        if (argc == 0)
            world = get_world_header();
        else if (argc == 1) {
            if ((world = find_world(argv[0])) == NULL)
                tfprintf(tferr, "%% The world %s is unknown.",argv[0]);
        } else if (restrict >= RESTRICT_WORLD) {
            tfputs("% Connecting to undefined worlds is restricted.", tferr);
        } else {
            world = new_world(NULL, "", "", argv[0], argv[1], "", "");
            world->flags |= WORLD_TEMP;
        }
        if (autologin < 0) autologin = login;
        if (quietlogin < 0) quietlogin = quiet;
        if (world) opensock(world, autologin, quietlogin);
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

