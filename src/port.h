/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: port.h,v 35004.16 1997/04/02 23:50:17 hawkeye Exp $ */

#ifndef PORT_H
#define PORT_H

#ifdef __hpux__
# ifndef _HPUX_SOURCE
#  define _HPUX_SOURCE    /* Enables some "extensions" on HPUX. */
# endif
#endif

#if _AIX - 0
# ifndef _ALL_SOURCE
#  define _ALL_SOURCE     /* Enables some "extensions" on AIX. */
# endif
/* # define _BSD 44 */    /* Needed for nonblocking connect on AIX. */
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

#ifdef HAVE_STDARG
# define VDEF(args)  args
#else
# define VDEF(args)  (va_alist)  va_dcl
#endif

#if 0  /* These cause a few problems, but little benefit, so forget it. */
/* These aren't neccessary, but may improve optimization, etc. */
# ifdef __GNUC__
#  define INLINE __inline__
#  if (__GNUC__ == 2 && __GNUC_MINOR__ >= 5) || (__GNUC__ > 2)
#   define GNUC_2_5_OR_LATER
#   define PURE   __attribute__ ((const))
#   define NORET  __attribute__ ((noreturn))
#  endif
# endif
#endif

#ifndef INLINE
# define INLINE
#endif
#ifndef PURE
# define PURE
#endif
#ifndef NORET
# define NORET
#endif

#if 0
# ifdef __GNUC__
#  define format_printf(fmt, var)     __attribute__((format(printf, fmt, var)))
# else
#  define format_printf(fmt, var)     /* empty */
# endif
#else
#  define format_printf(fmt, var)     /* empty */
#endif


/* standard stuff */

#include <stdio.h>

#ifndef SEEK_SET
# define SEEK_SET 0
#endif


#undef GENERIC
#undef CONST

#if __STDC__ - 0
# define GENERIC void
# define CONST   const
#else
# define GENERIC char
# define CONST
#endif

#ifdef UNISTD_H
# include UNISTD_H
#endif

#ifndef STDIN_FILENO
# define STDIN_FILENO        0
# define STDOUT_FILENO       1
# define STDERR_FILENO       2
#endif

#ifdef STDLIB_H
# include STDLIB_H
#else
extern void free();
#endif

#include STRING_H

#ifndef HAVE_strchr
# ifdef HAVE_index
#  define strchr index
#  define strrchr rindex    /* assumed */
# endif
#endif

#ifdef HAVE_memset
  /* We don't use the nonstandard bzero(), but some stupid sys/select.h do */
# define bzero(ptr, size)    memset((ptr), '\0', (size))
#endif

/* We use memcpy(), so we must define it if it doesn't exist. */
#ifndef HAVE_memcpy
# ifdef HAVE_bcopy
#  define memcpy(dst, src, len) bcopy((src), (dst), (len))
# endif
#endif

/* Try the common case insensitive strcmp's before falling back to our own */
#ifdef HAVE_strcasecmp
# define cstrcmp   strcasecmp
#else
# ifdef HAVE_stricmp
#  define cstrcmp   stricmp
# else
#  ifdef HAVE_strcmpi
#   define cstrcmp   strcmpi
#  endif
# endif
#endif
#ifndef cstrcmp
extern int    FDECL(cstrcmp,(CONST char *s, CONST char *t));
#endif

#ifndef HAVE_strstr
  extern char *FDECL(strstr,(CONST char *s1, CONST char *s2));
#endif

#ifndef HAVE_strerror
extern int sys_nerr;
extern char *sys_errlist[];
# define strerror(n) (((n) > 0 && (n) < sys_nerr) ? sys_errlist[(n)] : \
    "unknown error")
#endif

#ifndef HAVE_fileno  /* occurs on at least one pre-POSIX SVr3-like platform */
# ifdef PLATFORM_UNIX
#  define fileno(p)  ((p)->_file)
# else
   /* Who knows what it should be elsewhere; it should already exist. */
# endif
#endif

#ifdef PLATFORM_OS2
# define HAVE_getcwd
# define getcwd _getcwd2   /* handles drive names */
# define chdir _chdir2     /* handles drive names */
#endif


#ifndef LOCALE_H
# undef HAVE_setlocale
#endif
#ifndef HAVE_setlocale
# undef LOCALE_H
#endif


#include <ctype.h>
#ifdef CASE_OK /* are tolower and toupper safe on all characters, per ANSI? */
  /* more efficient */
# define lcase(x)  tolower(x)
# define ucase(x)  toupper(x)
#else
  extern int lcase(x);
  extern int ucase(x);
  /* This expression evaluates its argument more than once:
   *  (isupper(x) ? tolower(x) : (x))
   * This expression has no sequence points:
   *  (dummy=(x), (isupper(dummy) ? tolower(dummy) : (dummy)))
   */
  /* guaranteed to work in nonstandard C */
#endif


/* Standard C allows struct assignment, but K&R1 didn't. */
#define structcpy(dst, src) \
    memcpy((GENERIC*)&(dst), (GENERIC*)&(src), sizeof(src))

/* RRAND(lo,hi) returns a random integer in the range [lo,hi].
 * RAND() returns a random integer in the range [0,TF_RAND_MAX].
 * SRAND() seeds the generator.
 * If random() exists, use it, because it is better than rand().
 * If not, we'll have to use rand(); if RAND_MAX isn't defined,
 * we'll have to use the modulus method instead of the division method.
 * Warning: RRAND() is undefined if hi < lo.
 * Warning: on Solaris 2.x, libucb contains a nonstandard rand() that does
 * not agree with RAND_MAX.  We must not link with -lucb.
 */

#ifdef HAVE_srandom
# include <math.h>
# define RAND()         (int)random()
# define SRAND(seed)    srandom(seed)
# define RRAND(lo,hi)   (RAND() % ((hi)-(lo)+1) + (lo))
#else
# ifdef HAVE_srand
#  define RAND()         rand()
#  define SRAND(seed)    srand(seed)
#  ifdef RAND_MAX
#   define RRAND(lo,hi)  ((hi)==(lo)) ? (hi) : \
                             ((RAND() / (RAND_MAX / ((hi)-(lo)+1) + 1)) + (lo))
#  else
#   define RRAND(lo,hi)  (RAND() % ((hi)-(lo)+1) + (lo))
#  endif
# else
   error "Don't have srand() or srandom()."
# endif
#endif


#ifndef HAVE_strtod
# undef USE_FLOAT
#endif


/* These just prevent warnings during development.  They should not be
 * used in production, since they might conflict with system headers.
 */
#ifdef TF_IRIX_DECLS
extern int  FDECL(kill,(pid_t, int));
extern int  VDECL(ioctl,(int, int, ...));
extern long NDECL(random);
extern int  FDECL(srandom,(unsigned));
#endif
#ifdef TF_AIX_DECLS
extern int  FDECL(strcasecmp,(CONST char *, CONST char *));
extern time_t FDECL(time,(time_t *));
/* extern pid_t FDECL(wait,(int *)); */
extern int FDECL(socket,(int, int, int));
extern int FDECL(getsockopt,(int, int, int, char *, int *));
extern int FDECL(send,(int, CONST char *, int, int));
extern int FDECL(recv,(int, char *, int, int));
extern int  VDECL(ioctl,(int, int, ...));
extern long NDECL(random);
extern int  FDECL(srandom,(unsigned));
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* PORT_H */
