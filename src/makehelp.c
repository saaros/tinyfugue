/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: makehelp.c,v 35004.4 1997/03/27 01:04:34 hawkeye Exp $ */


/**************************************************************
 * Fugue help index builder
 *
 * Rewritten by Ken Keys to allow topic aliasing and subtopics;
 * be self-contained; and build index at install time.
 **************************************************************/

#include <stdio.h>

int main(argc, argv) 
    int argc;
    char **argv;
{
    char line[81];
    long offset = 0;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        if ((line[0] == '@' || line[0] == '#') && line[1])
            printf("%ld%s", offset, line);
        offset = ftell(stdin);
    }
    return 0;
}
