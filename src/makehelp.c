/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: makehelp.c,v 35004.15 2003/05/27 01:09:23 hawkeye Exp $";


/**************************************************************
 * Fugue help index builder
 *
 * Rewritten by Ken Keys to allow topic aliasing and subtopics;
 * be self-contained; and build index at install time.
 **************************************************************/

#include <stdio.h>

int main(int argc, char **argv)
{
    char line[240+1];
    long offset = 0;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        if ((line[0] == '&' || line[0] == '#') && line[1])
            printf("%ld%s", offset, line);
        offset = ftell(stdin);
    }
    return 0;
}
