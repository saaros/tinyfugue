/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: dstring.h,v 35004.11 1999/01/31 00:27:39 hawkeye Exp $ */

#ifndef DSTRING_H
#define DSTRING_H

#define ALLOCSIZE		(32L)

typedef struct String {
    char *s;
    unsigned int len, size;
#ifdef DMALLOC
    int is_static;
#endif
} String, Stringp[1];          /* Stretchybuffer */

/* This saves time, but can't be used in reentrant functions.
 * Pre-ANSI C didn't allow initialization of automatic aggregates,
 * so we can only use this technique for static and nonlocal buffers.
 */
#ifdef DMALLOC
#define STATIC_BUFFER(name)	static Stringp (name) = { { NULL, 0, 0, 1 } }
#else
#define STATIC_BUFFER(name)	static Stringp (name) = { { NULL, 0, 0 } }
#endif

#define Stringzero(str)		((void)((str->s = NULL), str->len = str->size = 0))
#define Stringinit(str)		dSinit(str, ALLOCSIZE, __FILE__, __LINE__)
#define Stringninit(str, n)	dSinit(str, (n), __FILE__, __LINE__)
#define Stringfree(str)		dSfree(str, __FILE__, __LINE__)
#define Stringadd(str, c)	dSadd(str, c, __FILE__, __LINE__)
#define Stringnadd(str, c, n)	dSnadd(str, c, n, __FILE__, __LINE__)
#define Stringterm(str, n)	dSterm(str, n, __FILE__, __LINE__)
#define Stringcpy(dst, src)	dScpy(dst, src, __FILE__, __LINE__)
#define SStringcpy(dst, src)	dSScpy(dst, src, __FILE__, __LINE__)
#define Stringncpy(dst, src, n)	dSncpy(dst, src, n, __FILE__, __LINE__)
#define Stringcat(dst, src)	dScat(dst, src, __FILE__, __LINE__)
#define SStringcat(dst, src)	dSScat(dst, src, __FILE__, __LINE__)
#define Stringncat(dst, src, n)	dSncat(dst, src, n, __FILE__, __LINE__)
#define Stringfncat(dst, src, n) dSfncat(dst, src, n, __FILE__, __LINE__)

#define FL	CONST char *file, int line

extern String *FDECL(dSinit,(Stringp str, unsigned size, FL));
extern void    FDECL(dSfree,(Stringp str, FL));
extern String *FDECL(dSadd, (Stringp str, int c, FL));
extern String *FDECL(dSnadd,(Stringp str, int c, unsigned n, FL));
extern String *FDECL(dSterm,(Stringp str, unsigned n, FL));
extern String *FDECL(dScpy, (Stringp dest, CONST char *src, FL));
extern String *FDECL(dSScpy,(Stringp dest, CONST Stringp src, FL));
extern String *FDECL(dSncpy,(Stringp dest, CONST char *src, unsigned n, FL));
extern String *FDECL(dScat, (Stringp dest, CONST char *src, FL));
extern String *FDECL(dSScat,(Stringp dest, CONST Stringp src, FL));
extern String *FDECL(dSncat,(Stringp dest, CONST char *src, unsigned n, FL));
extern String *FDECL(dSfncat,(Stringp dest, CONST char *src, unsigned n, FL));

#undef FL

#endif /* DSTRING_H */
