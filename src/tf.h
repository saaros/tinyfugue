/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tf.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef TF_H
#define TF_H

#ifndef SYS_TIME_H    /* defined by socket.c which includes <sys/time.h> */
# include <time.h>    /* for time_t */
#endif

#include <errno.h>
extern int errno;           /* not all systems do this in <errno.h> */
#include <math.h>           /* random() */
#include "regexp/regexp.h"  /* Henry Spencer's regexp package */

/*
 * TinyFugue global types and variables.
 */

#define SAVEGLOBAL    1000     /* global history size */
#define SAVEWORLD     1000     /* world history size */
#define SAVELOCAL      100     /* local history size */
#define SAVEINPUT       50     /* command history buffer size */
#define WATCHLINES       5     /* number for watchdog to change */
#define NAMEMATCHNUM     4     /* ask to gag if this many last lines by same */
#define STRINGMATCHNUM   2     /* ignore if this many of last lines identical */
#define MAXQUIET        25     /* max # of lines to suppress during login */

#define TFRC          "~/.tfrc"
#define TINYTALK      "~/.tinytalk"          /* for backward compatibility */

#define RESTRICT_SHELL  1
#define RESTRICT_FILE   2
#define RESTRICT_WORLD  3

typedef int  NDECL((EditFunc));
typedef int  FDECL((Handler),(char *args));
#define HANDLER(name) int FDECL(name,(char *args))
typedef void NDECL((Toggler));
typedef int  FDECL(Cmp,(CONST GENERIC *, CONST GENERIC *));/* generic compare */

typedef char smallstr[65];     /* Short buffer */

typedef struct Aline {         /* shared line, with attributes */
    char *str;
    unsigned int len;
    short links, attrs;
    short *partials;
    TIME_T time;
} Aline;

typedef struct Pattern {
    char *str;
    regexp *re;
} Pattern;

#define Queue List

#define F_COLOR      000007
#define F_UNDERLINE  000010
#define F_REVERSE    000020
#define F_FLASH      000040
#define F_DIM        000100
#define F_BOLD       000200
#define F_HILITE     000400
#define F_BELL       001000

#define F_GAG        002000
#define F_NOHISTORY  004000
#define F_SUPERGAG   (F_GAG | F_NOHISTORY)
#define F_NORM       010000

#define F_NEWLINE    020000
#define F_INDENT     040000

#define F_HWRITE     001777
#define F_ATTR       017777

#include "malloc.h"
#include "tfio.h"
#include "variable.h"

#endif /* TF_H */
