/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: help.c,v 35004.16 1999/01/31 00:27:43 hawkeye Exp $ */

/*
 * Fugue help handling
 *
 * Uses the help index to search the helpfile for a topic.
 *
 * Rewritten by Ken Keys to work with rest of program, and handle
 * topic aliasing, and subtopics.
 */

#include "config.h"
#include <stdio.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "tfio.h"
#include "commands.h"
#include "variable.h"

STATIC_BUFFER(indexfname);

#define HELPLEN  (240+1)	/* maximum length of lines in help file */

struct Value *handle_help_command(args) 
    char *args;
{
    char buf0[HELPLEN], buf1[HELPLEN], buf2[HELPLEN];
    char *input, *major_buffer, *minor_buffer, *spare;
    char *major_topic, *minor_topic, *place;
    CONST char *name;
    TFILE *helpfile, *indexfile;
    long offset = -1;
    attr_t attrs;  /* for carrying attributes to next line */

    Stringterm(indexfname, 0);

    name = expand_filename(getvar("TFHELP"));
    if ((helpfile = tfopen(name, "r")) == NULL) {
        operror(name);
        return newint(0);
    }

#ifndef __CYGWIN32__
    if (helpfile->type == TF_FILE) {
        /* regular file: use index */
        Sprintf(indexfname, 0, "%s.idx", name);
        if ((indexfile = tfopen(indexfname->s, "r")) == NULL) {
            operror(indexfname->s);
            tfclose(helpfile);
            return newint(0);
        }
    } else
#endif
    {
        /* use brute-force search */
        indexfile = helpfile;
    }

    name = (*args) ? args : "summary";

    input = buf0;
    major_buffer = buf1;
    minor_buffer = buf2;

    while (offset < 0 && fgets(input, HELPLEN, indexfile->u.fp) != NULL) {
        minor_buffer[0] = '\0';
        for (place = input; is_digit(*place); place++);
        if (*place == '&') {
            major_buffer[0] = '\0';
            spare = major_buffer;
            major_buffer = input;
        } else if (*place == '#') {
            spare = minor_buffer;
            minor_buffer = input;
        } else {
            continue;
        }
        ++place;
        if (*place)
            place[strlen(place)-1] = '\0';

        if (strcmp(place, name) == 0 ||
            (is_punct(*place) && strcmp(place + 1, name) == 0))
        {
            offset = atol(input);
        }
        input = spare;
    }

    if (indexfile != helpfile)
        tfclose(indexfile);

    if (offset < 0) {
        oprintf("%% Help on subject %s not found.", name);
        tfclose(helpfile);
        return newint(0);
    }

    if (indexfile != helpfile)
        fseek(helpfile->u.fp, offset, SEEK_SET);

    /* find offset, skip lines matching ^[&#], and remember last topic */

    while (fgets(input, HELPLEN, helpfile->u.fp) != NULL) {
        if (*input) input[strlen(input)-1] = '\0';
        if (*input != '&' && *input != '#') break;
        if (*minor_buffer) {
            spare = minor_buffer;
            minor_buffer = input;
            input = spare;
        } else if (*input == '&') {
            spare = major_buffer;
            major_buffer = input;
            input = spare;
        }
    }

    for (major_topic = major_buffer; is_digit(*major_topic); major_topic++);
    major_topic++;
    if (*minor_buffer) {
        for (minor_topic = minor_buffer; is_digit(*minor_topic); minor_topic++);
        minor_topic++;
        tfprintf(tfout, "Help on: %s: %s", major_topic, minor_topic);
    } else {
        tfprintf(tfout, "Help on: %s", major_topic);
    }

    attrs = 0;
    while (*input != '&') {
        if (*input != '#')
            attrs = tfputansi(input, tfout, attrs);
        else if (*minor_buffer)
            break;
        if (fgets(input, HELPLEN, helpfile->u.fp) == NULL) break;
        if (*input) input[strlen(input)-1] = '\0';
    }

    if (*minor_buffer)
        tfprintf(tfout, "For more complete information, see \"%s\".",
            major_topic);

    tfclose(helpfile);
    return newint(1);
}

#ifdef DMALLOC
void free_help()
{
    Stringfree(indexfname);
}
#endif
