/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: dstring.h,v 33000.0 1994/03/05 09:34:14 hawkeye Exp $ */

#ifndef DSTRING_H
#define DSTRING_H

typedef struct String {
    char *s;
    unsigned int len, size;
} String, Stringp[1];          /* Stretchybuffer */

#ifdef DMALLOC
#define Stringinit(str) dStringinit(str, __FILE__, __LINE__)
#endif

/* This saves time, but can't be used in reentrant functions.
 * Pre-ANSI C didn't allow initialization of automatic aggregates,
 * so we can only use this technique for static and nonlocal buffers.
 */
#define STATIC_BUFFER(name) static Stringp (name) = { { NULL, 0, 0 } }


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

#endif /* DSTRING_H */
