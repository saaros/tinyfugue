/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: parse.h,v 35004.10 1999/01/31 00:27:50 hawkeye Exp $ */

#ifndef PARSE_H
#define PARSE_H

/* keywords: must be sorted and numbered sequentially */
#define BREAK	'\200'
#define DO	'\201'
#define DONE	'\202'
#define ELSE	'\203'
#define ELSEIF	'\204'
#define ENDIF	'\205'
#define EXIT	'\206'
#define IF	'\207'
#define RETURN	'\210'
#define THEN	'\211'
#define WHILE	'\212'

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
extern int         FDECL(varsub,(Stringp dest, int sub_warn));
extern int         FDECL(exprsub,(Stringp dest));
extern int         FDECL(cmdsub,(Stringp dest));
extern CONST char *FDECL(valstr,(Value *val));
extern int         FDECL(valbool,(Value *val));
extern int         FDECL(pushval,(Value *val));
extern void        FDECL(freeval,(Value *val));
extern Value      *FDECL(expr_value,(CONST char *expression));
extern Value      *FDECL(expr_value_safe,(CONST char *expression));

#define dollarsub(dest) \
    ((*ip == '[') ? exprsub(dest) : (*ip == '(') ? cmdsub(dest) : macsub(dest))

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
