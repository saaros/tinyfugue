/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: port.h,v 33000.5 1994/04/16 05:11:49 hawkeye Exp $ */

#ifndef PORT_H
#define PORT_H

#ifdef hpux
# ifndef _HPUX_SOURCE
#  define _HPUX_SOURCE    /* Needed for stat() (and maybe others) on hpux 7.0 */
# endif
#endif

#ifndef _ALL_SOURCE
# define _ALL_SOURCE      /* Enables some "extensions" on AIX. */
#endif

#if __STDC__ - 0
# undef  HAVE_PROTOTYPES
# define HAVE_PROTOTYPES
#endif

#ifdef __GNUC__
# undef  HAVE_PROTOTYPES
# define HAVE_PROTOTYPES
#endif

#ifdef HAVE_PROTOTYPES
# define NDECL(f)        f(void)
# define FDECL(f, p)     f p
# ifdef HAVE_STDARG
#  define VDECL(f, p)     f p
# else
#  define VDECL(f, p)     f()
# endif
#else
# define NDECL(f)        f()
# define FDECL(f, p)     f()
# define VDECL(f, p)     f()
#endif

/* standard stuff */
#include <stdio.h>

#if __STDC__ - 0
# define GENERIC void
# define CONST   const
#else
# define GENERIC char
# define CONST
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#else
extern void free();
#endif

#ifdef USE_STRING_H
# include <string.h>
#else
# ifdef USE_STRINGS_H
#  include <strings.h>
# endif
#endif

#ifndef HAVE_STRCHR
# ifdef HAVE_INDEX
#  define strchr(s, c) index((s), (c))
# endif
#endif

#ifndef HAVE_MEMCPY
# ifdef HAVE_BCOPY
#  define memcpy(dst, src, len) bcopy((src), (dst), (len))
# endif
#endif

/* ANSI allows struct assignment, but K&R1 didn't. */
#define structcpy(dst, src) \
    memcpy((GENERIC*)&(dst), (GENERIC*)&(src), sizeof(src))

/*
 * RRAND(lo,hi) returns a random integer in the range [lo,hi].
 * RAND() returns a random integer in the range [0,TF_RAND_MAX].
 * SRAND() seeds the generator.
 * If random() exists, use it, because it is better than rand().
 * If not, we'll have to use rand(); if RAND_MAX isn't defined,
 * we'll have to use the modulus method instead of the division method.
 */

#ifdef HAVE_RANDOM
# define RAND          random
# define SRAND         srandom
# define RRAND(lo,hi)  (RAND() % ((hi)-(lo)+1) + (lo))
#else
# define RAND         rand
# define SRAND        srand
# ifdef RAND_MAX
#  define RRAND(lo,hi)  ((RAND() / (RAND_MAX / ((hi)-(lo)+1) + 1)) + (lo))
# else
#  define RRAND(lo,hi)  (RAND() % ((hi)-(lo)+1) + (lo))
# endif
#endif


/* These shouldn't change anything, they just prevent warnings. */
#ifdef MISSING_DECLS
extern void FDECL(bzero,(void *, int));
extern int  FDECL(kill,(pid_t, int));
extern int  FDECL(ioctl,(int, int, void *));
extern long NDECL(random);
extern int  FDECL(srandom,(int));
#endif

#define TRUE 1
#define FALSE 0

#endif /* PORT_H */
