/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: parse.h,v 35004.3 1997/12/13 22:40:46 hawkeye Exp $ */

#ifndef PARSE_H
#define PARSE_H

/* keywords: must be sorted and numbered sequentially */
#define BREAK	'\200'
#define DO	'\201'
#define DONE	'\202'
#define ELSE	'\203'
#define ELSEIF	'\204'
#define ENDIF	'\205'
#define IF	'\206'
#define THEN	'\207'
#define WHILE	'\210'

#define TYPE_ID     1
#define TYPE_STR    2
#define TYPE_INT    3
#define TYPE_FLOAT  4

typedef struct Value {
    int type;
    int len;                            /* length of u.sval */
    int count;                          /* reference count */
    union {
        long ival;
        double fval;
        CONST char *sval;               /* string value or identifier name */
        struct Value *next;             /* for valpool */
    } u;
} Value;

typedef struct Arg {
    CONST char *start, *end;
} Arg;


extern void        FDECL(parse_error,(CONST char *type, CONST char *expect));
extern int         FDECL(macsub,(Stringp dest));
extern int         FDECL(varsub,(Stringp dest));
extern int         FDECL(exprsub,(Stringp dest));
extern int         FDECL(cmdsub,(Stringp dest));
extern CONST char *FDECL(valstr,(Value *val));
extern int         FDECL(valbool,(Value *val));
extern int         FDECL(pushval,(Value *val));
extern void        FDECL(freeval,(Value *val));

#define dollarsub(dest) \
    ((*ip == '[') ? exprsub(dest) : (*ip == '(') ? cmdsub(dest) : macsub(dest))

#define set_user_result(val)  do { \
      struct Value *v = val;  /* only evaluate (val) once */ \
      if (v != user_result) { freeval(user_result); user_result = v; } \
    } while(0)
#define copy_user_result(val) do { \
      struct Value *v = val;  /* only evaluate (val) once */ \
      if (v != user_result) \
          { freeval(user_result); (user_result = v)->count++; } \
    } while(0)

extern Value *stack[];			/* expression stack */
extern int stacktop;			/* first free position on stack */
extern CONST char *ip;			/* instruction pointer */
extern Arg *tf_argv;			/* shifted argument vector */
extern int tf_argc;			/* shifted argument count */
extern int argtop;			/* top of function argument stack */
extern CONST char *argtext;		/* shifted argument text */
extern int block;			/* type of current expansion block */
extern int condition;			/* checked by /if and /while */
extern int evalflag;			/* flag: should we evaluate? */

#endif /* PARSE_H */
