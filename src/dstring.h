/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: dstring.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef DSTRING_H
#define DSTRING_H

#ifdef USE_STRING_H
# include <string.h>
#endif
#ifdef USE_STRINGS_H
# include <strings.h>
#endif

#ifndef HAVE_STRCHR
# ifdef HAVE_INDEX
#  define strchr(s, c) index((s), (c))
# endif
#endif

#ifndef HAVE_MEMCPY
# ifdef HAVE_BCPY
#  define memcpy(dst, src, len) bcopy((src), (dst), (len))
# endif
#endif

typedef struct String {
    char *s;
    unsigned int len, maxlen;
} String, Stringp[1];          /* Stretchybuffer */

#ifdef DMALLOC
#define Stringinit(str) dStringinit(str, __FILE__, __LINE__)
#endif

/* This saves time, but can't be used in reentrant functions.
 * Pre-ANSI C didn't allow initialization of automatic aggregates,
 * so we can only use this technique for static and nonlocal buffers.
 */
#define STATIC_BUFFER(name) static Stringp (name) = { { NULL, 0, -1 } }


#ifdef DMALLOC
extern String *FDECL(dStringinit,(Stringp str, char *file, int line));
#else
extern String *FDECL(Stringinit,(Stringp str));
#endif
extern void    FDECL(Stringfree,(Stringp str));
extern String *FDECL(Stringadd,(Stringp str, int c));
extern String *FDECL(Stringnadd,(Stringp str, int c, unsigned int n));
extern String *FDECL(Stringterm,(Stringp str, unsigned int len));
extern String *FDECL(Stringcpy,(Stringp dest, char *src));
extern String *FDECL(SStringcpy,(Stringp dest, Stringp src));
extern String *FDECL(Stringncpy,(Stringp dest, char *src, unsigned int len));
extern String *FDECL(Stringcat,(Stringp dest, char *src));
extern String *FDECL(SStringcat,(Stringp dest, Stringp src));
extern String *FDECL(Stringncat,(Stringp dest, char *src, unsigned int len));
extern String *FDECL(newline_package,(Stringp buffer, unsigned int n));

#endif /* DSTRING_H */
