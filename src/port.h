/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: port.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

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

#ifndef RANDOM             /* True only if Build couldn't find it in libc.a. */
# define RANDOM rand       /* This is guaranteed to exist under ANSI. */
# define SRANDOM srand
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
