/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003, 2004 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: util.h,v 35004.46 2004/02/17 06:44:44 hawkeye Exp $ */

#ifndef UTIL_H
#define UTIL_H

#include "pcre-2.08/pcre.h"

typedef struct RegInfo {
    pcre *re;
    pcre_extra *extra;
    String *Str;
    int links;
    int *ovector;
    int ovecsize;
} RegInfo;

typedef struct Pattern {
    char *str;
    RegInfo *ri;
    int mflag;
} Pattern;

struct feature {
    const char *name;
    const int *flag;
};

#undef CTRL
/* convert to or from ctrl character */
#define CTRL(c)  (ucase(c) ^ '@')

/* map char to or from "safe" character set */
#define mapchar(c)    ((c) ? (c) & 0xFF : 0x80)
#define unmapchar(c)  ((char)(((c) == (char)0x80) ? 0x0 : (c)))

/* Map character into set allowed by locale */
#define localize(c)  ((is_print(c) || is_cntrl(c)) ? (c) : (c) & 0x7F)

/* Note STRNDUP works only if src[len] == '\0', ie. len == strlen(src) */
#define STRNDUP(src, len) \
    (strcpy(xmalloc(NULL, (len) + 1, __FILE__, __LINE__), (src)))
#define STRDUP(src)  STRNDUP((src), strlen(src))


#define IS_QUOTE	0001
#define IS_STATMETA	0002
#define IS_STATEND	0004
#define IS_KEYSTART	0010
#define IS_UNARY	0020
#define IS_MULT		0040
#define IS_ADDITIVE	0100

extern struct timeval tvzero;
extern struct timeval mail_update;
extern int mail_count;
extern struct mail_info_s *maillist;
extern char tf_ctype[];
extern Stringp featurestr;
extern struct feature features[];

#define is_quote(c)	(tf_ctype[(unsigned char)c] & IS_QUOTE)
#define is_statmeta(c)	(tf_ctype[(unsigned char)c] & IS_STATMETA)
#define is_statend(c)	(tf_ctype[(unsigned char)c] & IS_STATEND)
#define is_keystart(c)	(tf_ctype[(unsigned char)c] & IS_KEYSTART)
#define is_unary(c)	(tf_ctype[(unsigned char)c] & IS_UNARY)
#define is_mult(c)	(tf_ctype[(unsigned char)c] & IS_MULT)
#define is_additive(c)	(tf_ctype[(unsigned char)c] & IS_ADDITIVE)

#define tvcmp(a, b) \
   (((a)->tv_sec != (b)->tv_sec) ? \
       ((a)->tv_sec - (b)->tv_sec) : \
       ((a)->tv_usec - (b)->tv_usec))

#if HAVE_GETTIMEOFDAY
# define gettime(p)	(gettimeofday(p, NULL))
#else
# define gettime(p)	((p)->tv_usec = 0, time(&(p)->tv_sec))
#endif

#define strtochr(s, ep)   ((char)(strtol((s), (char**)ep, 0) % 0x100))
#define strtoint(s, ep)   ((int)strtol((s), (char**)ep, 10))
#define strtolong(s, ep)  (strtol((s), (char**)ep, 10))
extern int    enum2int(const char *str, long val, String *vec, const char *msg);
extern void   init_util1(void);
extern void   init_util2(void);
extern String*print_to_ascii(const char *str);
extern String*ascii_to_print(const char *str);
extern char  *cstrchr(const char *s, int c);
extern char  *estrchr(const char *s, int c, int e);
extern int    numarg(char **str);
extern char  *stringarg(char **str, const char **end);
extern int    stringliteral(struct String *dest, char **str);
extern void   restore_reg_scope(RegInfo *old);
extern int    regmatch_in_scope(Value *val, const char *pattern, String *Str);
extern int    tf_reg_exec(RegInfo *ri, String *Sstr, const char *str, int offset);
extern RegInfo*new_reg_scope(RegInfo *ri, String *Str);
extern void   tf_reg_free(RegInfo *ri);
extern int    regsubstr(struct String *dest, int n);
extern int    init_pattern(Pattern *pat, const char *str, int mflag);
extern int    init_pattern_str(Pattern *pat, const char *str);
extern int    init_pattern_mflag(Pattern *pat, int mflag, int opt);
#define copy_pattern(dst, src)  (init_pattern(dst, (src)->str, (src)->mflag))
extern int    patmatch(const Pattern *pat, String *Sstr, const char *str);
extern void   free_pattern(Pattern *pat);
extern int    smatch(const char *pat, const char *str);
extern int    smatch_check(const char *s);
extern char  *stripstr(char *s);
extern void   startopt(String *args, const char *opts);
extern char   nextopt(char **arg, void *u, int *type, int *offp);
#if HAVE_TZSET
extern int    ch_timezone(void);
#else
# define ch_timezone NULL
#endif
extern int    ch_locale(void);
extern int    ch_mailfile(void);
extern int    ch_maildelay(void);
extern void   check_mail(void);

extern type_t string_arithmetic_type(const char *str, int typeset);
extern Value *parsenumber(const char *str, char **caller_endp, int typeset,
		Value *val);
extern long   parsetime(const char *str, char **endp, int *istime);
extern void   abstime(struct timeval *tv);
extern void   tftime(String *buf, String *fmt, struct timeval *tv);
extern void   normalize_time(struct timeval *tv);
extern void   tvsub(struct timeval *a, struct timeval *b, struct timeval *c);
extern void   tvadd(struct timeval *a, struct timeval *b, struct timeval *c);
extern void   die(const char *why, int err) NORET;
#if USE_DMALLOC
extern void   free_util(void);
#endif

#endif /* UTIL_H */
