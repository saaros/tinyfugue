/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tf.h,v 35004.24 1999/01/31 00:27:55 hawkeye Exp $ */

#ifndef TF_H
#define TF_H

/* headers needed everywhere */

#include <time.h>	/* may conflict with <sys/time.h> on some systems? */
#include <sys/time.h>	/* for struct timeval */
#include "malloc.h"
#include "globals.h"

#ifdef SOCKS
# if (SOCKS == 4)
#  define connect Rconnect
#  define select Rselect
# else /* (SOCKS == 5) */
#  include <socks.h>
# endif
#endif


/*
 * TinyFugue global types and variables.
 */

/* History sizes are now defined at runtime with /histsize and %histsize. */

#define RESTRICT_SHELL  1
#define RESTRICT_FILE   2
#define RESTRICT_WORLD  3

typedef char smallstr[65];     /* Short buffer */

#define Queue List

#define attr_t long

typedef struct Aline {         /* shared line, with attributes */
    char *str;
    unsigned int len;
    int links;
    attr_t attrs;
    short *partials;
    struct timeval tv;
} Aline;

#define BLANK_ALINE { "", 0, 1, 0, NULL, { 0, 0 } }
#define LITERAL_ALINE(str) { (str), sizeof(str)-1, 1, 0, NULL, { 0, 0 } }

#define F_FGCOLORMASK 00000017   /* 4 bits, interpreted as an integer */
#define F_FGCOLOR     00000020   /* flag */
#define F_BGCOLORMASK 00000340   /* 3 bits, interpreted as an integer */
#define F_BGCOLOR     00000400   /* flag */
#define F_UNDERLINE   00001000
#define F_REVERSE     00002000
#define F_FLASH       00004000
#define F_DIM         00010000
#define F_BOLD        00020000
#define F_HILITE      00040000
#define F_BELL        00100000

#define F_GAG         00200000
#define F_NOHISTORY   00400000
#define F_SUPERGAG    (F_GAG | F_NOHISTORY)
#define F_NONE        01000000
#define F_EXCLUSIVE   02000000

#define F_INDENT      04000000

#define F_COLORS      (F_FGCOLOR | F_BGCOLOR | F_FGCOLORMASK | F_BGCOLORMASK)
#define F_SIMPLE      (F_UNDERLINE | F_REVERSE | F_FLASH | F_DIM | F_BOLD)
#define F_HWRITE      (F_SIMPLE | F_HILITE | F_COLORS)
#define F_ATTR        (F_HWRITE | F_SUPERGAG | F_NONE | F_EXCLUSIVE)

#define attr2fgcolor(attr)	((attr) & F_FGCOLORMASK)
#define attr2bgcolor(attr)	((((attr) & F_BGCOLORMASK) >> 5) + 16)
#define color2attr(color)   \
    (((color) < 16) ? \
    (F_FGCOLOR | (color)) : \
    (F_BGCOLOR | (((color) - 16) << 5)))

/* If new contains F_EXCLUSIVE, it replaces attr; otherwise, it combines. */
#define add_attr(attr, new) \
    (attr = (new & F_EXCLUSIVE) ? (new) : (attr | new))


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
    typedef struct { \
        unsigned long bits[(((size) + LONGBITS - 1) / LONGBITS)]; \
    } (type)

#define VEC_SET(n,p)   ((p)->bits[(n)/LONGBITS] |= (1L << ((n) % LONGBITS)))
#define VEC_CLR(n,p)   ((p)->bits[(n)/LONGBITS] &= ~(1L << ((n) % LONGBITS)))
#define VEC_ISSET(n,p) ((p)->bits[(n)/LONGBITS] & (1L << ((n) % LONGBITS)))
#ifdef HAVE_memcpy   /* assume memcpy implies memset and bcopy implies bzero. */
# define VEC_ZERO(p)   memset((char *)(p)->bits, '\0', sizeof(*(p)))
#else
# define VEC_ZERO(p)   bzero((char *)(p)->bits, sizeof(*(p)))
#endif


/* Define enumerated constants */
#define ENUMEXTERN extern
#define bicode(a, b)  a 
#include "enumlist.h"

extern CONST char *enum_flag[];
extern CONST char *enum_sub[];
extern CONST char *enum_color[];

/* hook definitions */

extern int VDECL(do_hook,(int indx, CONST char *fmt, CONST char *argfmt, ...))
    format_printf(2, 4);

enum Hooks {
#define bicode(a, b)  a 
#include "hooklist.h"
#undef bicode
};

#define ALL_HOOKS  (~(~0L << NUM_HOOKS))


/* externs */
extern CONST char version[], sysname[], copyright[], contrib[], mods[];
extern int restriction;


#endif /* TF_H */
