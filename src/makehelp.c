/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: makehelp.c,v 33000.1 1994/04/26 08:56:29 hawkeye Exp $ */


/**************************************************************
 * Fugue help index builder
 *
 * Original algorithm developed by Leo Plotkin.
 * Rewritten by Ken Keys to allow topic aliasing and subtopics;
 * be self-contained; and build index at install time.
 **************************************************************/

#include "config.h"
#include <stdio.h>
#include "port.h"

#define HELP "tf.help"
#define INDEX "tf.help.index"

int main(argc, argv) 
    int argc;
    char **argv;
{
    char line[81];
    FILE *indexfile, *helpfile;
    unsigned charcount = 0;

    if ((helpfile = fopen(HELP, "r")) == NULL) {
        printf("%% File %s cannot be opened for reading.\n", HELP);
        return 1;
    }
    if ((indexfile = fopen(INDEX, "w")) == NULL) {
        printf("%% File %s cannot be opened for writing.\n", INDEX);
        return 1;
    }
    while (fgets(line, 80, helpfile) != NULL) {
        if ((line[0] == '@' || line[0] == '#') && line[1])
            fprintf(indexfile, "%u%s", charcount, line);
        charcount += strlen(line);
    }
    fclose(indexfile);
    fclose(helpfile);
    return 0;
}
