/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: util.h,v 35004.26 1999/01/31 00:27:57 hawkeye Exp $ */

#ifndef UTIL_H
#define UTIL_H

#include "regexp/regexp.h"

typedef struct Pattern {
    char *str;
    regexp *re;
    int mflag;
} Pattern;

#undef CTRL
/* convert to or from ctrl character */
#define CTRL(c)  (ucase(c) ^ '@')

/* map char to or from "safe" character set */
#define mapchar(c)    ((c) ? (c) & 0xFF : 0x80)
#define unmapchar(c)  ((char)(((c) == (char)0x80) ? 0x0 : (c)))

/* Map character into set allowed by locale */
#define localize(c)  ((is_print(c) || is_cntrl(c)) ? (c) : (c) & 0x7F)

/* Modulo arithmetic: remainder is positive, even if numerator is negative. */
#define nmod(n, d)   (((n) >= 0) ? ((n)%(d)) : ((d) - ((-(n)-1)%(d)) - 1))
#define ndiv(n, d)   (((n) >= 0) ? ((n)/(d)) : (-((-(n)-1)/(d))-1))

/* Note STRNDUP works only if src[len] == '\0', ie. len == strlen(src) */
#define STRNDUP(src, len) \
    (strcpy(xmalloc((len) + 1, __FILE__, __LINE__), (src)))
#define STRDUP(src)  STRNDUP((src), strlen(src))


#define IS_QUOTE	0001
#define IS_STATMETA	0002
#define IS_STATEND	0004
#define IS_KEYSTART	0010
#define IS_UNARY	0020
#define IS_MULT		0040
#define IS_ADDITIVE	0100

extern TIME_T mail_update;
extern int mail_count;
extern char tf_ctype[];

#define is_quote(c)	(tf_ctype[(unsigned char)c] & IS_QUOTE)
#define is_statmeta(c)	(tf_ctype[(unsigned char)c] & IS_STATMETA)
#define is_statend(c)	(tf_ctype[(unsigned char)c] & IS_STATEND)
#define is_keystart(c)	(tf_ctype[(unsigned char)c] & IS_KEYSTART)
#define is_unary(c)	(tf_ctype[(unsigned char)c] & IS_UNARY)
#define is_mult(c)	(tf_ctype[(unsigned char)c] & IS_MULT)
#define is_additive(c)	(tf_ctype[(unsigned char)c] & IS_ADDITIVE)

#ifdef HAVE_gettimeofday
# define gettime(p)	(gettimeofday(p, NULL))
#else
# define gettime(p)	((p)->tv_usec = 0, time(&(p)->tv_sec))
#endif

#ifdef HAVE_strtol
# define strtochr(sp)   ((char)(strtol(*(sp), (char **)sp, 0) % 0x100))
# define strtoint(sp)   ((int)strtol(*(sp), (char **)sp, 10))
# define strtolong(sp)  (strtol(*(sp), (char **)sp, 10))
#else
extern char   FDECL(strtochr,(char **sp));
extern int    FDECL(strtoint,(char **sp));
extern long   FDECL(strtolong,(char **sp));
#endif
extern int    FDECL(enum2int,(CONST char *str, CONST char **vec, CONST char *msg));
extern void   NDECL(init_util1);
extern void   NDECL(init_util2);
extern char  *FDECL(print_to_ascii,(CONST char *str));
extern char  *FDECL(ascii_to_print,(CONST char *str));
extern char  *FDECL(cstrchr,(CONST char *s, int c));
extern char  *FDECL(estrchr,(CONST char *s, int c, int e));
extern int    FDECL(numarg,(char **str));
extern char  *FDECL(stringarg,(char **str, CONST char **end));
extern int    FDECL(stringliteral,(struct String *dest, char **str));
extern void   FDECL(restore_reg_scope,(void *old));
extern int    FDECL(regexec_in_scope,(regexp *re, CONST char *str));
extern void  *FDECL(new_reg_scope,(regexp *re, CONST char *str));
extern int    FDECL(regsubstr,(struct String *dest, int n));
extern int    FDECL(init_pattern,(Pattern *pat, CONST char *str, int mflag));
extern int    FDECL(init_pattern_str,(Pattern *pat, CONST char *str));
extern int    FDECL(init_pattern_mflag,(Pattern *pat, int mflag));
#define copy_pattern(dst, src)  (init_pattern(dst, (src)->str, (src)->mflag))
extern int    FDECL(patmatch,(CONST Pattern *pat, CONST char *str));
extern void   FDECL(free_pattern,(Pattern *pat));
extern int    FDECL(smatch,(CONST char *pat, CONST char *str));
extern int    FDECL(smatch_check,(CONST char *s));
extern char  *FDECL(stripstr,(char *s));
extern void   FDECL(startopt,(CONST char *args, CONST char *opts));
extern char   FDECL(nextopt,(char **arg, long *num));
#ifdef HAVE_tzset
extern int    NDECL(ch_timezone);
#else
# define ch_timezone NULL
#endif
extern int    NDECL(ch_locale);
extern int    NDECL(ch_mailfile);
extern int    NDECL(ch_maildelay);
extern void   NDECL(check_mail);
extern long   FDECL(parsetime,(char **strp, int *istime));
extern TIME_T FDECL(abstime,(long hms));
extern int    FDECL(tftime,(String *dest, CONST char *fmt, long sec,long usec));
extern void   FDECL(internal_error,(CONST char *file, int line));
extern void   FDECL(die,(CONST char *why, int err)) NORET;
#ifdef DMALLOC
extern void   NDECL(free_util);
#endif

#endif /* UTIL_H */
