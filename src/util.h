/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: util.h,v 33000.4 1994/04/23 23:30:44 hawkeye Exp $ */

#ifndef UTIL_H
#define UTIL_H

/* Not even all ANSI systems have strerror()!  So, we assume at least */
/* sys_errlist[] is available, and roll our own strerror() */
extern int sys_nerr;
extern char *sys_errlist[];
#define STRERROR(n) (((n) > 0 && (n) < sys_nerr) ? sys_errlist[(n)] : \
    "unknown error")

#ifdef DMALLOC
#define STRNDUP(src, len) \
    (strcpy(dmalloc((len) + 1, __FILE__, __LINE__), (src)))
#else
#define STRNDUP(src, len) (strcpy(dmalloc((len) + 1), (src)))
#endif
#define STRDUP(src)  STRNDUP((src), strlen(src))

extern char lowercase_values[128], uppercase_values[128];
#define lcase(x) (lowercase_values[(int)(x)])
#define ucase(x) (uppercase_values[(int)(x)])
#define STRMATCH(s, t) (!smatch((s), (t)))

#ifdef HAVE_STRTOL
# define strtochr(sp)   (char)(strtol(*(sp), sp, 0) % 128)
# define strtoi(sp)     (int)strtol(*(sp), sp, 10)
#else
extern char   FDECL(strtochr,(char **sp));
extern char   FDECL(strtoi,(char **sp));
#endif
extern void   NDECL(init_util);
extern void   NDECL(init_mail);
extern char  *FDECL(print_to_ascii,(char *str));
extern char  *FDECL(ascii_to_print,(char *str));
extern char  *FDECL(cstrchr,(CONST char *s, int c));
extern char  *FDECL(estrchr,(CONST char *s, int c, int e));
extern int    FDECL(cstrcmp,(CONST char *s, CONST char *t));
extern int    FDECL(cstrncmp,(CONST char *s, CONST char *t, int n));

#ifdef HAVE_STRSTR
# define STRSTR(s1, s2) strstr((s1), (s2))
#else
  extern char *FDECL(STRSTR,(CONST char *s1, CONST char *s2));
#endif

extern int    FDECL(numarg,(char **str));
extern char  *FDECL(stringarg,(char **str, int *spaces));
extern void   NDECL(regrelease);
extern void   FDECL(reghold,(regexp *re, char *str, int temp));
extern int    FDECL(regexec_and_hold,(regexp *re, char *str, int temp));
extern int    FDECL(regsubstr,(String *dest, int n));
extern int    FDECL(init_pattern,(Pattern *pat, char *str, int mflag));
extern int    FDECL(patmatch,(Pattern *pat, char *str, int mflag, int temp));
extern void   FDECL(free_pattern,(Pattern *pat));
extern int    FDECL(smatch,(char *s, char *t));
extern int    FDECL(smatch_check,(CONST char *s));
extern char  *FDECL(stripstr,(char *s));
extern void   FDECL(startopt,(char *args, char *opts));
extern char   FDECL(nextopt,(char **arg, int *num));
#ifdef DMALLOC
extern Aline *FDECL(dnew_aline,(char *str, int attrs, char *file, int line));
#define new_aline(s,a)  dnew_aline((s), (a), __FILE__, __LINE__)
#else
extern Aline *FDECL(new_aline,(char *str, int attrs));
#endif
extern void   FDECL(free_aline,(Aline *aline));
extern void   NDECL(ch_mailfile);
extern void   NDECL(ch_maildelay);
extern void   NDECL(check_mail);
extern int    FDECL(parsetime,(char **strp, int *colon));
extern TIME_T FDECL(abstime,(int hms));
extern char  *FDECL(tftime,(char *fmt, TIME_T t));
extern void   NDECL(cleanup);
extern void   FDECL(die,(CONST char *why));

#endif /* UTIL_H */
