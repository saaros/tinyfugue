/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: help.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

/*
 * Fugue help handling
 *
 * Uses the help index to search the helpfile for a topic.
 *
 * Original algorithm developed by Leo Plotkin.
 * Rewritten by Ken Keys to work with rest of program, and handle
 * topic aliasing, and subtopics.
 */

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "commands.h"

int handle_help_command(args) 
    char *args;
{
    STATIC_BUFFER(indexfname);
    STATIC_BUFFER(buf0);
    STATIC_BUFFER(buf1);
    STATIC_BUFFER(buf2);
    String *input, *major_buffer, *minor_buffer, *spare;
    char *name, *major_topic, *minor_topic, *place;
    TFILE *helpfile, *indexfile;
    long offset = -1;

    Stringterm(indexfname, 0);
    Stringterm(buf0, 0);
    Stringterm(buf1, 0);
    Stringterm(buf2, 0);
    if (!*args) args = "summary";

    if (!(name = tfname(TFHELP, NULL))) return 0;
    if ((helpfile = tfopen(name, "r")) == NULL) {
        operror(name);
        return 0;
    }
    Sprintf(indexfname, 0, "%s.index", name);
    if ((indexfile = tfopen(indexfname->s, "r")) == NULL) {
        operror(indexfname->s);
        tfclose(helpfile);
        return 0;
    }

    input = buf0;
    major_buffer = buf1;
    minor_buffer = buf2;

    while (offset < 0 && tfgetS(input, indexfile) != NULL) {
        Stringterm(minor_buffer, 0);
        for (place = input->s; isdigit(*place); place++);
        if (*place == '@') {
            Stringterm(major_buffer, 0);
            spare = major_buffer;
            major_buffer = input;
        } else {
            spare = minor_buffer;
            minor_buffer = input;
        }
        ++place;
        if ((*place != '-' && cstrcmp(place, args) == 0) ||
          (*place == '-' && strcmp(place, args) == 0) ||
          (*place == '/' && cstrcmp(place + 1, args) == 0)) {
                offset = atol(input->s);
        }
        input = spare;
    }
    tfclose(indexfile);
    if (offset < 0) {
        oprintf("%% Help on subject %s not found.", args);
        tfclose(helpfile);
        return 0;
    }

    /* find offset, skip lines matching ^[@#], and remember last topic */
    tfjump(helpfile, offset);
    while (tfgetS(input, helpfile) != NULL) {
        if (*input->s != '@' && *input->s != '#') break;
        if (minor_buffer->len) {
            spare = minor_buffer;
            minor_buffer = input;
            input = spare;
        } else if (*input->s == '@') {
            spare = major_buffer;
            major_buffer = input;
            input = spare;
        }
    }

    for (major_topic = major_buffer->s; isdigit(*major_topic); major_topic++);
    major_topic++;
    if (minor_buffer->len) {
        for (minor_topic = minor_buffer->s; isdigit(*minor_topic); minor_topic++);
        minor_topic++;
        oprintf("Help on: %s %s", major_topic, minor_topic);
    } else {
        oprintf("Help on: %s", major_topic);
    }

    do {
        if (*input->s == '@') break;
        else if (*input->s != '#') oputs(input->s);
        else if (minor_buffer->len) break;
    } while (tfgetS(input, helpfile) != NULL);

    tfclose(helpfile);
    return 1;
}
