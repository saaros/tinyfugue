/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tf.h,v 33000.4 1994/04/26 08:56:29 hawkeye Exp $ */

#ifndef TF_H
#define TF_H

#ifndef SYS_TIME_H    /* defined by socket.c which includes <sys/time.h> */
# include <time.h>    /* for time_t */
#endif

#include <errno.h>
extern int errno;           /* not all systems do this in <errno.h> */
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

#define F_COLORMASK  0000017   /* low 4 bits are interpreted as an integer */
#define F_COLOR      0000020   /* flag */
#define F_UNDERLINE  0000040
#define F_REVERSE    0000100
#define F_FLASH      0000200
#define F_DIM        0000400
#define F_BOLD       0001000
#define F_HILITE     0002000
#define F_BELL       0004000

#define F_GAG        0010000
#define F_NOHISTORY  0020000
#define F_SUPERGAG   (F_GAG | F_NOHISTORY)
#define F_NORM       0040000

#define F_INDENT     0100000

#define F_SIMPLE     (F_UNDERLINE | F_REVERSE | F_FLASH | F_DIM | F_BOLD)
#define F_HWRITE     (F_SIMPLE | F_HILITE | F_COLOR | F_COLORMASK | F_BELL)
#define F_ATTR       (F_HWRITE | F_SUPERGAG | F_NORM)


/* Macros for defining and manipulating bit vectors of arbitrary length.
 * We use an array of long because select() does, and these macros will be
 * used with select() on systems without the FD_* macros.
 */

#ifndef NBBY
# define NBBY 8                                   /* bits per byte */
#endif
#ifndef LONGBITS
# define LONGBITS  (sizeof(long) * NBBY)          /* bits per long */
#endif

#define VEC_TYPEDEF(type, size) \
    typedef struct { long bits[(((size) + LONGBITS - 1) / LONGBITS)]; } (type)

#define VEC_SET(n,p)   ((p)->bits[(n)/LONGBITS] |= (1 << ((n) % LONGBITS)))
#define VEC_CLR(n,p)   ((p)->bits[(n)/LONGBITS] &= ~(1 << ((n) % LONGBITS)))
#define VEC_ISSET(n,p) ((p)->bits[(n)/LONGBITS] & (1 << ((n) % LONGBITS)))
#ifndef HAVE_BCOPY   /* assume memcpy implies memset and bcopy implies bzero. */
# define VEC_ZERO(p)   memset((char *)(p)->bits, '\0', sizeof(*(p)))
#else
# define VEC_ZERO(p)   bzero((char *)(p)->bits, sizeof(*(p)))
#endif


/* headers needed everywhere */

#include "malloc.h"
#include "tfio.h"
#include "variable.h"

#endif /* TF_H */
