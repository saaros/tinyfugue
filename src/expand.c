/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expand.c,v 35004.31 1997/03/27 01:01:55 hawkeye Exp $ */


/********************************************************************
 * Fugue macro text interpreter
 *
 * Written by Ken Keys
 * Interprets expressions and macro statements.
 * Performs substitutions for positional parameters, variables, macro
 * bodies, and expressions.
 ********************************************************************/

#include "config.h"
#include <math.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "world.h"	/* world_info() */
#include "macro.h"
#include "signals.h"	/* interrupted() */
#include "socket.h"	/* send_line(), sockidle() */
#include "search.h"
#include "output.h"	/* igoto(), status_bar() */
#include "keyboard.h"	/* kb*() */
#include "expand.h"
#include "commands.h"
#include "command.h"
#include "variable.h"


/* keywords: must be sorted and numbered sequentially */
#define BREAK    '\200'
#define DO       '\201'
#define DONE     '\202'
#define ELSE     '\203'
#define ELSEIF   '\204'
#define ENDIF    '\205'
#define IF       '\206'
#define THEN     '\207'
#define WHILE    '\210'

/* note: all 2-char operators must have high bit set */
#define OP_EQUAL    '\300'   /*  ==  */
#define OP_NOTEQ    '\301'   /*  !=  */
#define OP_GTE      '\302'   /*  >=  */
#define OP_LTE      '\303'   /*  <=  */
#define OP_STREQ    '\304'   /*  =~  */
#define OP_STRNEQ   '\305'   /*  !~  */
#define OP_MATCH    '\306'   /*  =/  */
#define OP_NMATCH   '\307'   /*  !/  */
#define OP_ASSIGN   '\310'   /*  :=  */
#define OP_PREINC   '\311'   /*  ++  */
#define OP_PREDEC   '\312'   /*  --  */
#define OP_FUNC     '\313'   /*  name(...)  */

#define TYPE_ID     1
#define TYPE_STR    2
#define TYPE_INT    3
#define TYPE_FLOAT  4

typedef struct Arg {
    CONST char *start, *end;
} Arg;

typedef struct Value {
    int type;
    int len;
    union {
        long ival;
        double fval;
        CONST char *sval;
        struct Value *next;		/* for valpool */
    } u;
} Value;

#define STACKSIZE 128

int user_result = 0;			/* result of last user command */
int read_depth = 0;			/* read() flag */

static CONST char *argtext = NULL;	/* shifted argument text */
static Arg *argv = NULL;		/* shifted argument vector */
static int argc = 0;			/* shifted argument count */
static int recur_count = 0;		/* expansion nesting count */
static int cmdsub_count = 0;		/* cmdsub nesting count */
static CONST char *ip;			/* instruction pointer */
static int condition = 1;		/* checked by /if and /while */
static int evalflag = 1;		/* flag: should we evaluate? */
static int block = 0;			/* type of current block */
static int breaking = 0;		/* flag: are we /break'ing? */
static int stacktop = 0;
static Value *stack[STACKSIZE];
static Value *valpool = NULL;		/* freelist */

static CONST char *keyword_table[] = {
    "BREAK", "DO", "DONE", "ELSE", "ELSEIF", "ENDIF", "IF", "THEN", "WHILE"
};

static int    NDECL(keyword);
static int    FDECL(list,(Stringp dest, int subs));
static int    NDECL(end_statement);
static int    FDECL(statement,(Stringp dest, int subs));
static Value *FDECL(newint,(long i));
static Value *FDECL(newstrid,(CONST char *str, int len, int type));
#ifdef USE_FLOAT
static Value *FDECL(newfloat,(double f));
static double FDECL(valfloat,(Value *val));
static int    FDECL(mathtype,(Value *val));
#endif /* USE_FLOAT */
static long   FDECL(valint,(Value *val));
static int    FDECL(vallen,(Value *val));
static CONST char *FDECL(valstr,(Value *val));
static void   FDECL(freeval,(Value *val));
static void   FDECL(conditional_add,(Stringp s, int c));
static int    FDECL(slashsub,(Stringp dest));
static int    FDECL(macsub,(Stringp dest));
static int    FDECL(varsub,(Stringp dest));
static int    FDECL(backsub,(Stringp dest));
static int    FDECL(exprsub,(Stringp dest));
static int    FDECL(cmdsub,(Stringp dest));
static int    NDECL(expr);
static int    NDECL(comma_expr);
static int    NDECL(assignment_expr);
static int    NDECL(conditional_expr);
static int    NDECL(or_expr);
static int    NDECL(and_expr);
static int    NDECL(relational_expr);
static int    NDECL(additive_expr);
static int    NDECL(multiplicative_expr);
static int    NDECL(unary_expr);
static int    NDECL(primary_expr);
static int    FDECL(pushval,(Value *val));
static int    FDECL(reduce,(int op, int n));
static Value *FDECL(do_function,(int n));
static CONST char *NDECL(error_text);
static void   FDECL(parse_error,(CONST char *type, CONST char *expect));

#define dollarsub(dest) \
    ((*ip == '[') ? exprsub(dest) : (*ip == '(') ? cmdsub(dest) : macsub(dest))

#define newstr(s,l)  (newstrid(s,l,TYPE_STR))
#define newid(s,l)   (newstrid(s,l,TYPE_ID))

#define is_end_of_statement(p) ((p)[0] == '%' && is_statend((p)[1]))
#define is_end_of_cmdsub(p) (cmdsub_count && *(p) == ')')

#ifndef NO_EXPR  /* with NO_EXPR defined, many keybindings can't work! */

/* get Nth operand from stack (counting backwards from top) */
#define opd(N)      (stack[stacktop-(N)])
#define opdfloat(N) valfloat(opd(N))
#define opdint(N)   valint(opd(N))
#define opdstr(N)   valstr(opd(N))
#define opdlen(N)   vallen(opd(N))

typedef struct ExprFunc {
    CONST char *name;		/* name invoked by user */
    unsigned min, max;		/* allowable argument counts */
} ExprFunc;

static ExprFunc functab[] = {
#define funccode(id, name, min, max)  { name, min, max }
#include "funclist.h"
#undef funccode
};

enum func_id {
#define funccode(id, name, min, max)  id
#include "funclist.h"
#undef funccode
};


extern CONST char *current_command;


static int expr()
{
    int ok;
    int stackbot = stacktop;
    int old_eflag = evalflag;
    int old_condition = condition;
    int old_breaking = breaking;

    ok = comma_expr();
    if (ok && stacktop != stackbot + 1) {
        internal_error(__FILE__, __LINE__);
        eputs((stacktop < stackbot + 1) ?
            "% expression stack underflow" : "% dirty expression stack");
        ok = 0;
    }
    while (stacktop > stackbot + ok) freeval(stack[--stacktop]);

    /* in case some short-circuit code left these in a weird state */
    evalflag = old_eflag;
    condition = old_condition;
    breaking = old_breaking;

    return ok;
}

int handle_test_command(args)
    char *args;
{
    CONST char *saved_ip = ip;
    long result = 0;

    ip = args;
    if (expr()) {
        if (*ip) parse_error("expression", "operand");
        else result = opdint(1);
        freeval(stack[--stacktop]);
    }
    ip = saved_ip;
    return result;
}
#else /* NO_EXPR */

static int expr()
{
    eprintf("expressions are not supported.");
    return 0;
}

int handle_test_command(args)
    char *args;
{
    return atol(args);
}

#endif /* NO_EXPR */

int process_macro(body, args, subs)
    CONST char *body, *args;
    int subs;
{
    Stringp buffer;
    Arg *true_argv = NULL;		/* unshifted argument vector */
    int vecsize = 20, error = 0;
    int saved_cmdsub, saved_argc, saved_breaking;
    Arg *saved_argv;
    CONST char *saved_ip;
    CONST char *saved_argtext;
    List scope[1];

    if (++recur_count > max_recur && max_recur) {
        eprintf("too many recursions");
        recur_count--;
        return 0;
    }
    saved_cmdsub = cmdsub_count;
    saved_ip = ip;
    saved_argc = argc;
    saved_argv = argv;
    saved_argtext = argtext;
    saved_breaking = breaking;

    ip = body;
    cmdsub_count = 0;

    newvarscope(scope);

    argtext = args;
    if (args) {
        argc = 0;
        if (!(argv = (Arg *)MALLOC(vecsize * sizeof(Arg)))) {
            eprintf("Not enough memory for argument vector");
            error = TRUE;
        }
        while (!error && *args) {
            if (argc == vecsize)
                argv = (Arg*)XREALLOC((char*)argv, sizeof(Arg)*(vecsize+=10));
            argv[argc].start = stringarg((char **)&args, &argv[argc].end);
            argc++;
        }
        true_argv = argv;
    }

    if (!error) {
        Stringninit(buffer, 96);
        if (!list(buffer, subs)) user_result = 0;
        Stringfree(buffer);
    }

    if (true_argv) FREE(true_argv);
    nukevarscope();

    cmdsub_count = saved_cmdsub;
    ip = saved_ip;
    argc = saved_argc;
    argv = saved_argv;
    argtext = saved_argtext;
    breaking = saved_breaking;
    recur_count--;
    return user_result;
}

static int list(dest, subs)
    Stringp dest;
    int subs;
{
    int oldcondition, oldevalflag, oldblock;
    int is_a_command, is_a_condition;
    int iterations = 0, failed = 0;
    CONST char *start = NULL;
    STATIC_BUFFER(mprefix_deep);
    static CONST char unexpect_msg[] = "unexpected %s in %s block";

#define unexpected(token, blk) \
    eprintf(unexpect_msg, token, blk ? keyword_table[blk - BREAK] : "outer");

    /* Do NOT strip leading space here.  This allows user to type and send
     * lines with leading spaces (but completely empty lines are handled
     * by handle_input_line()).  During expansion, spaces AFTER a "%;"
     * or keyword will be skipped.
     */

#if 1
    if (!*ip || is_end_of_cmdsub(ip)) user_result = 1;/* empty list returns 1 */
#else
    user_result = 1;  /* empty list returns 1 */
#endif

    if (block == WHILE) start = ip;

    do /* while (*ip) */ {
        if (breaking && !block) {
            breaking--;
            return 1;
        }
#if 1
        if (subs >= SUB_NEWLINE)
            while (isspace(*ip) || (ip[0] == '\\' && isspace(ip[1])))
                ++ip;
#endif

        is_a_command = is_a_condition = FALSE;
        if (interrupted()) {
            eprintf("%% macro evaluation interrupted.");
            return 0;
        }
        Stringterm(dest, 0);
        /* Lines begining with one "/" are tf commands.  Lines beginning
         * with multiple "/"s have the first removed, and are sent to server.
         */

        if ((subs > SUB_LITERAL) && (*ip == '/') && (*++ip != '/')) {
            is_a_command = TRUE;
            oldblock = block;
            if (subs >= SUB_KEYWORD) {
                block = keyword();
                if (subs == SUB_KEYWORD)
                    subs += (block != 0);
            }

        } else if ((subs > SUB_LITERAL) &&
            (block == IF || block == ELSEIF || block == WHILE))
        {
            if (*ip == '(') {
                is_a_condition = TRUE;
                oldblock = block;
                ip++; /* skip '(' */
                if (!expr()) return 0;
                if (stack[--stacktop])
                    user_result = valint(stack[stacktop]);
                freeval(stack[stacktop]);
                if (*ip != ')') {
                    parse_error("condition", "')' after if/while condition");
                    return 0;
                }
                while(isspace(*++ip)); /* skip ')' and spaces */
                block = (block == WHILE) ? DO : THEN;
            } else if (*ip) {
                eprintf("warning: statement starting with %s in /%s %s %s",
                    error_text(), keyword_table[block - BREAK],
                    "condition sends text to server,",
                    "which is probably not what was intended.");
            }
        }

        if (is_a_command || is_a_condition) {
            switch(block) {
            case WHILE:
                oldevalflag = evalflag;
                oldcondition = condition;
                if (!list(dest, subs)) failed = 1;
                else if (block == WHILE) {
                    parse_error("macro", "/do");
                    failed = 1;
                } else if (block == DO) {
                    parse_error("macro", "/done");
                    failed = 1;
                }
                evalflag = oldevalflag;
                condition = oldcondition;
                block = oldblock;
                if (failed) return 0;
                continue;
            case DO:
                if (oldblock != WHILE) {
                    unexpected("/do", oldblock);
                    block = oldblock;
                    return 0;
                }
                evalflag = evalflag && condition;
                condition = user_result;
                if (breaking) breaking++;
                continue;
            case BREAK:
                if (!breaking && evalflag && condition) {
                    if ((breaking = atoi(ip)) <= 0) breaking = 1;
                }
                block = oldblock;
                continue;
            case DONE:
                if (oldblock != DO) {
                    unexpected("/done", oldblock);
                    block = oldblock;
                    return 0;
                }
                if (breaking || !condition || !evalflag) {
                    if (breaking) breaking--;
                    evalflag = 0;  /* don't eval any trailing garbage */
                    return end_statement();
                } else if (++iterations > max_iter && max_iter) {
                    eprintf("too many iterations");
                    block = oldblock;
                    return 0;
                } else {
                    ip = start;
                    block = WHILE;
                    continue;
                }
            case IF:
                oldevalflag = evalflag;
                oldcondition = condition;
                if (!list(dest, subs)) {
                    failed = 1;
                } else if (block == IF || block == ELSEIF) {
                    parse_error("macro", "/then");
                    failed = 1;
                } else if (block == THEN || block == ELSE) {
                    parse_error("macro", "/endif");
                    failed = 1;
                }
                evalflag = oldevalflag;
                condition = oldcondition;
                block = oldblock;
                if (failed) return 0;
                continue;
            case THEN:
                if (oldblock != IF && oldblock != ELSEIF) {
                    unexpected("/then", oldblock);
                    block = oldblock;
                    return 0;
                }
                evalflag = evalflag && condition;
                condition = user_result;
                continue;
            case ELSEIF:
                if (oldblock != THEN) {
                    unexpected("/elseif", oldblock);
                    block = oldblock;
                    return 0;
                }
                condition = !condition;
                continue;
            case ELSE:
                if (oldblock != THEN) {
                    unexpected("/else", oldblock);
                    block = oldblock;
                    return 0;
                }
                condition = !condition;
                continue;
            case ENDIF:
                if (oldblock != THEN && oldblock != ELSE) {
                    unexpected("/endif", oldblock);
                    block = oldblock;
                    return 0;
                }
                return end_statement();
            default:
                /* not a control statement */
                ip--;
                block = oldblock;
                break;
            }
        }

        if (!statement(dest, subs)) return 0;

        if (!breaking && evalflag && condition && (dest->len || !snarf)) {
            extern int invis_flag;
            if (subs == SUB_MACRO && (mecho - invis_flag) > 0) {
                int i;
                Stringterm(mprefix_deep, 0);
                for (i = 0; i < recur_count + cmdsub_count; i++)
                    Stringcat(mprefix_deep, mprefix);
                tfprintf(tferr, "%S %S", mprefix_deep, dest);
            }

            if (is_a_command) {
                user_result = handle_command(dest);
            } else {
                if (!do_hook(H_SEND, NULL, "%S", dest)) {
                    user_result = send_line(dest->s, dest->len, TRUE);
                }
            }
        }

        if (is_end_of_cmdsub(ip)) break;
    } while (*ip);
    return 1;
}

static int keyword()
{
    CONST char **result, *end;
    char buf[sizeof("elseif")];

    if (!is_keystart(*ip)) return 0;          /* fast heuristic */

    end = ip + 1;
    while (*end && !isspace(*end) && *end != '%' && *end != ')')
        end++;
    if (end - ip >= sizeof(buf)) return 0;    /* too long, can't be keyword */

    strncpy(buf, ip, end - ip);
    buf[end - ip] = '\0';
    result = (CONST char **)binsearch((GENERIC*)buf, (GENERIC*)keyword_table,
        sizeof(keyword_table)/sizeof(char*), sizeof(char*), cstrstructcmp);
    if (!result) return 0;
    for (ip = end; isspace(*ip); ip++);
    return BREAK + (result - keyword_table);
}

static int end_statement()
{
    while (isspace(*ip)) ip++;
    if (!*ip) return 1;
    if (is_end_of_statement(ip)) {
        ip += 2;
        while (isspace(*ip)) ip++;
        return 1;
    }
    parse_error("macro", "end of statement");
    return 0;
}

static int statement(dest, subs)
    Stringp dest;
    int subs;
{
    CONST char *start;

    while (*ip) {
        if (*ip == '\\' && subs >= SUB_NEWLINE) {
            ++ip;
            if (!backsub(dest)) return 0;
#if 0
        } else if (ip[0] == '\\' && ip[1] == '\n') {
            ++ip;
            Stringadd(dest, *(ip++));
#endif
        } else if (*ip == '/' && subs >= SUB_FULL) {
            ++ip;
            if (!slashsub(dest)) return 0;
        } else if (*ip == '%' && subs >= SUB_NEWLINE) {
            ++ip;
            if (is_end_of_statement(ip-1)) {
                while (dest->len && isspace(dest->s[dest->len-1]))
                    Stringterm(dest, dest->len-1);  /* nuke spaces before %; */
                ++ip;
                while (isspace(*ip)) ip++; /* skip space after %; */
                break;
            } else if (*ip == '%') {
                while (*ip == '%') Stringadd(dest, *ip++);
            } else if (subs >= SUB_FULL) {
                if (!varsub(dest)) return 0;
            } else {
                Stringadd(dest, '%');
            }
        } else if (*ip == '$' && subs >= SUB_FULL) {
            ++ip;
            if (!dollarsub(dest)) return 0;
        } else if (subs >= SUB_FULL && is_end_of_cmdsub(ip)) {
            break;
#if 0
        } else if (*ip == '\n') {
            /* simulate old behavior: \n and spaces were stripped in /load */
            while (isspace(*ip))
                ip++;
#endif
        } else {
            /* is_statmeta() is much faster than all those if statements. */
            for (start = ip++; *ip && !is_statmeta(*ip); ip++);
            Stringncat(dest, start, ip - start);
        }
    }

#if 0
    if (subs >= SUB_NEWLINE && dest->len == 0 && !snarf &&
        !breaking && evalflag && condition)
    {
        eprintf("warning: empty statement sends blank line");
    }
#endif

    return 1;
}

static int slashsub(dest)
    Stringp dest;
{
    if (*ip == '/' && oldslash)
        while (*ip == '/') conditional_add(dest, *ip++);
    else
        conditional_add(dest, '/');
    return 1;
}

static CONST char *error_text()
{
    STATIC_BUFFER(buf);

    if (*ip) {
        CONST char *end = ip + 1;
        if (isalnum(*ip) || is_quote(*ip) || *ip == '/') {
            while (isalnum(*end)) end++;
        }
        Stringcpy(buf, "'");
        Stringncat(buf, ip, end - ip);
        Stringcat(buf, "'");
        return buf->s;
    } else {
        return "end of body";
    }
}

static void parse_error(type, expect)
    CONST char *type, *expect;
{
    eprintf("%s syntax error: expected %s, found %s.",
        type, expect, error_text());
}


#ifndef NO_EXPR

#ifdef USE_FLOAT
static Value *newfloat(f)
    double f;
{
    Value *val;

    if (breaking || !evalflag || !condition) return NULL;
    palloc(val, Value, valpool, u.next);
    val->type = TYPE_FLOAT;
    val->u.fval = f;
    val->len = -1;
    return val;
}

/* get math type of item: float or int */
static int mathtype(val)
    Value *val;
{
    CONST char *str;
    long result;

    if (val->type == TYPE_INT || val->type == TYPE_FLOAT)
        return val->type;
    str = val->u.sval;
    if (val->type == TYPE_ID) {
        str = getnearestvar(str, &result);
        if (result != 0 || !str) return TYPE_INT;
    }
    while (isspace(*str)) ++str;
    while (*str == '-' || *str == '+') ++str;
    if (str[0] == '.' && isdigit(str[1])) return TYPE_FLOAT;
    if (!isdigit(*str)) return TYPE_INT;
    ++str;
    while (isdigit(*str)) ++str;
    if (*str == '.') return TYPE_FLOAT;
    if (ucase(*str) != 'E') return TYPE_INT;
    ++str;
    if (*str == '-' || *str == '+') ++str;
    if (isdigit(*str)) return TYPE_FLOAT;
    return TYPE_INT;
}
#endif /* USE_FLOAT */

static Value *newint(i)
    long i;
{
    Value *val;

    if (breaking || !evalflag || !condition) return NULL;
    palloc(val, Value, valpool, u.next);
    val->type = TYPE_INT;
    val->u.ival = i;
    val->len = -1;
    return val;
}

static Value *newstrid(str, len, type)
    CONST char *str;
    int len, type;
{
    Value *val;
    char *new;

    if (breaking || !evalflag || !condition) return NULL;
    palloc(val, Value, valpool, u.next);
    val->type = type;
    new = strncpy((char *)XMALLOC(len + 1), str, len);
    new[len] = '\0';
    val->u.sval = new;
    val->len = len;
    return val;
}

static void freeval(val)
    Value *val;
{
    if (!val) return;   /* val may have been placeholder for short-circuit */
    if (val->type == TYPE_STR || val->type == TYPE_ID) FREE(val->u.sval);
    pfree(val, valpool, u.next);
}

/* return integer value of item */
static long valint(val)
    Value *val;
{
    CONST char *str;
    long result;

    if (val->type == TYPE_INT) return val->u.ival;
#ifdef USE_FLOAT
    if (val->type == TYPE_FLOAT) return (int)val->u.fval;
#endif
    str = val->u.sval;
    if (val->type == TYPE_ID) {
        str = getnearestvar(str, &result);
        if (result != 0 || !str) return result;
    }
    while (isspace(*str)) ++str;
    if (*str == '-' || *str == '+') return atol(str);
    return (isdigit(*str)) ? parsetime((char **)&str, NULL) : 0;
}

#ifdef USE_FLOAT
/* return floating value of item */
static double valfloat(val)
    Value *val;
{
    CONST char *str;
    double result;
    long i;

    errno = 0;
    if (val->type == TYPE_FLOAT) return val->u.fval;
    if (val->type == TYPE_INT) return (double)val->u.ival;
    str = val->u.sval;
    if (val->type == TYPE_ID) {
        str = getnearestvar(str, &i);
        if (i) return (double)i;
    }
    result = strtod(str, (char**)&str);
    return result;
}
#endif /* USE_FLOAT */

/* return string value of item */
static CONST char *valstr(val)
    Value *val;
{
    CONST char *str;
    static char buffer[16];

    switch (val->type) {
        case TYPE_INT:  sprintf(buffer, "%ld", val->u.ival); return buffer;
        case TYPE_STR:  return val->u.sval;
        case TYPE_ID:   return (str=getnearestvar(val->u.sval,NULL)) ? str : "";
#ifdef USE_FLOAT
        case TYPE_FLOAT: sprintf(buffer, "%g", val->u.fval); return buffer;
#endif
    }
    return NULL; /* impossible */
}

/* return length of string value of item */
static int vallen(val)
    Value *val;
{
    return (val->type == TYPE_STR) ? val->len : strlen(valstr(val));
}

static int pushval(val)
    Value *val;
{
    if (stacktop == STACKSIZE) {
        eprintf("expression stack overflow");
        return 0;
    }
    stack[stacktop++] = val;
    return 1;
}

/* Pop n operands, apply op to them, and push result */
static int reduce(op, n)
    int op;   /* operator */
    int n;    /* number of operands */
{
    Value *val = NULL;
    Var *var;
    char buf[16];
    long i; /* scratch */
#ifdef USE_FLOAT
    double f[3];
    int do_float = FALSE;
#endif

    if (stacktop < n) {
        internal_error(__FILE__, __LINE__);
        eputs("% stack underflow");
        return 0;
    }

    if (!evalflag || !condition || breaking) {
        /* Just maintain the depth of the stack, for parsing purposes;
         * the (nonexistant) value will never be used.
         */
        stacktop -= n - 1;
        return 1;
    }

    if (op == '/' && (block == IF || block == WHILE) &&
        opd(1)->type == TYPE_ID && !getnearestvar(opd(1)->u.sval, NULL))
    {
        /* common error: "/if /test expr /then ..." */
        eprintf("%% warning: possibly missing %s before /%s", "%; or )",
            opd(1)->u.sval);
    }

#ifdef USE_FLOAT
    switch (op) {
    case '>':
    case '<':
    case '=':
    case OP_EQUAL:
    case OP_NOTEQ:
    case OP_GTE:
    case OP_LTE:
    case '+':
    case '-':
    case '*':
    case '/':
    case '!':
        for (i = 1; i <= n; i++) {
            if (mathtype(opd(i)) != TYPE_INT) {
                do_float = TRUE;
                break;
            }
        }

        if (do_float) {
#if 0
            for (i = 1; i <= n; i++)
                f[i] = opdfloat(i);
#else
            switch(n) {
                case 3: f[3] = opdfloat(3);
                case 2: f[2] = opdfloat(2);
                case 1: f[1] = opdfloat(1);
            }
#endif
        }

        break;
    default:
        break;
    }

    if (do_float) {
        errno = 0;
        switch (op) {
        case '>':       val = newint(f[2] > f[1]);                 break;
        case '<':       val = newint(f[2] < f[1]);                 break;
        case '=':       /* fall thru to OP_EQUAL */
        case OP_EQUAL:  val = newint(f[2] == f[1]);                break;
        case OP_NOTEQ:  val = newint(f[2] != f[1]);                break;
        case OP_GTE:    val = newint(f[2] >= f[1]);                break;
        case OP_LTE:    val = newint(f[2] <= f[1]);                break;
        case '+':       val = newfloat(((n>1) ? f[2] : 0) + f[1]); break;
        case '-':       val = newfloat(((n>1) ? f[2] : 0) - f[1]); break;
        case '*':       val = newfloat(f[2] * f[1]);               break;

        case '/':       if (f[1] == 0.0)
                            eprintf("division by zero");
                        else
                            val = newfloat(f[2] / f[1]);
                        break;
        case '!':       val = newint(!f[1]);                       break;
        default:        break;
        }

    } else
#endif /* USE_FLOAT */

    if ((op == OP_ASSIGN || op == OP_PREINC || op == OP_PREDEC) &&
        (opd(n)->type != TYPE_ID))
    {
        eprintf("illegal object of assignment");

    } else {
        CONST char *old_command;
        switch (op) {
        case OP_ASSIGN: var = setnearestvar(opd(2)->u.sval, opdstr(1));
                        val = var ? newstr(var->value,var->len) : newstr("",0);
                        break;
        case OP_PREDEC: /* fall through */
        case OP_PREINC: i = opdint(1) + ((op == OP_PREINC) ? 1 : -1);
                        sprintf(buf, "%ld", i);
                        var = setnearestvar(opd(1)->u.sval, buf);
                        val = var ? newint(i) : newint(0);
                        break;
        case '>':       val = newint(opdint(2) > opdint(1));             break;
        case '<':       val = newint(opdint(2) < opdint(1));             break;
        case '=':       /* fall thru to OP_EQUAL */
        case OP_EQUAL:  val = newint(opdint(2) == opdint(1));            break;
        case OP_NOTEQ:  val = newint(opdint(2) != opdint(1));            break;
        case OP_GTE:    val = newint(opdint(2) >= opdint(1));            break;
        case OP_LTE:    val = newint(opdint(2) <= opdint(1));            break;
        case OP_STREQ:  val = newint(strcmp(opdstr(2), opdstr(1)) == 0); break;
        case OP_STRNEQ: val = newint(strcmp(opdstr(2), opdstr(1)) != 0); break;
        case OP_MATCH:  val = newint(smatch_check(opdstr(1)) &&
                            smatch(opdstr(1),opdstr(2))==0);
                        break;
        case OP_NMATCH: val = newint(smatch_check(opdstr(1)) &&
                            smatch(opdstr(1),opdstr(2))!=0);
                        break;
        case '+':       val = newint(((n>1) ? opdint(2) : 0) + opdint(1));
                        break;
        case '-':       val = newint(((n>1) ? opdint(2) : 0) - opdint(1));
                        break;
        case '*':       val = newint(opdint(2) * opdint(1));             break;
        case '/':       if ((i = opdint(1)) == 0)
                            eprintf("division by zero");
                        else
                            val = newint(opdint(2) / i);
                        break;
        case '!':       val = newint(!opdint(1));                        break;
        case OP_FUNC:   old_command = current_command;
                        val = do_function(n);
                        current_command = old_command;
                        break;
        default:        internal_error(__FILE__, __LINE__);
                        eprintf("%% reduce: internal error: unknown op %c", op);
                        break;
        }
    }

    stacktop -= n;
    while (n) freeval(stack[stacktop + --n]);
    if (val) pushval(val);
    return !!val;
}

static Value *do_function(n)
    int n;    /* number of operands (including function id) */
{
    Value *val;
    Handler *handler;
    ExprFunc *funcrec;
    Macro *macro;
    int oldblock;
    long i, j, len;
#ifdef USE_FLOAT
    double f;
#endif /* USE_FLOAT */
    char c;
    CONST char *id, *str, *ptr;
    extern Stringp keybuf;
    extern int keyboard_pos, no_tty, runall_depth;
    extern TIME_T keyboard_time;
    regexp *re;
    FILE *file;
    STATIC_BUFFER(scratch);

    if (opd(n)->type != TYPE_ID) {
        eprintf("function name must be an identifier.");
        return NULL;
    }
    current_command = id = opd(n--)->u.sval;

    funcrec = (ExprFunc *)binsearch((GENERIC*)id, (GENERIC*)functab,
        sizeof(functab)/sizeof(ExprFunc), sizeof(ExprFunc), strstructcmp);

    if (funcrec) {
        if (n < funcrec->min || n > funcrec->max) {
            eprintf("%s: incorrect number of arguments", id);
            return NULL;
        }
        switch (funcrec - functab) {

        case FN_COLUMNS:
            return newint(columns);

        case FN_LINES:
            return newint(lines);

        case FN_ECHO:
            oputa(new_aline(opdstr(1), 0));
            return newint(1);

        case FN_SEND:
            i = handle_send_function(opdstr(n), (n>1 ? opdstr(n-1) : NULL),
                (n>2 ? opdint(n-2) : 1));
            return newint(i);

        case FN_FWRITE:
            ptr = opdstr(2);
            file = fopen(expand_filename(ptr), "a");
            if (!file) {
                eprintf("%s: %s", opdstr(2), strerror(errno));
                return newint(0);
            }
            fputs(opdstr(1), file);
            fputc('\n', file);
            fclose(file);
            return newint(1);

        case FN_ASCII:
            return newint(unmapchar(*opdstr(1)));

        case FN_CHAR:
            c = mapchar(localize(opdint(1)));
            return newstr(&c, 1);

        case FN_KEYCODE:
            str = opdstr(1);
            ptr = get_keycode(str);
            if (ptr) return newstr(ptr, strlen(ptr));
            eprintf("unknown key name \"%s\"", str);
            return newstr("", 0);

        case FN_MOD:
            return newint(opdint(2) % opdint(1));

        case FN_MORESIZE:
            return newint(moresize);

#ifdef USE_FLOAT
        case FN_SQRT:
            f = opdfloat(1);
#if 0
            if (f < 0.0) {
                eprintf("%s: invalid argument", id);
                return NULL;
            }
#endif
            return newfloat(sqrt(f));

        case FN_TRUNC:
            f = opdfloat(1);
            return newint((int)f);
#endif /* USE_FLOAT */

        case FN_RAND:
            if (n == 0) return newint(RAND());
            i = (n==1) ? 0 : opdint(2);
            if (i < 0) i = 0;
            j = opdint(1) - (n==1);
            return newint((j > i) ? RRAND(i, j) : i);

        case FN_ISATTY:
            return newint(!no_tty);

        case FN_FTIME:
            str = tftime(opdstr(2), opdint(1));
            return str ? newstr(str, strlen(str)) : newstr("", 0);

        case FN_TIME:
            return newint((int)time(NULL));

        case FN_IDLE:
            return newint((int)((n == 0) ?
                (time(NULL) - keyboard_time) :
                sockidle(opdstr(1))));

        case FN_FILENAME:
            str = expand_filename(opdstr(1));
            return newstr(str, strlen(str));

        case FN_GETPID:
            return newint((long)getpid());

        case FN_REGMATCH:
            if (!(re = regcomp((char*)opdstr(2)))) return newint(0);
            str = opdstr(1);
            return newint(regexec_in_scope(re, str));

        case FN_STRCAT:
            for (Stringterm(scratch, 0); n; n--)
                Stringcat(scratch, opdstr(n));
            return newstr(scratch->s, scratch->len);

        case FN_STRREP:
            str = opdstr(2);
            i = opdint(1);
            for (Stringterm(scratch, 0); i > 0; i--)
                Stringcat(scratch, str);
            return newstr(scratch->s, scratch->len);

        case FN_PAD:
            for (Stringterm(scratch, 0); n > 0; n -= 2) {
                str = opdstr(n);
                len = opdlen(n);
                i = (n > 1) ? opdint(n-1) : 0;
                if (i >= 0) {
                    if (i > len) Stringnadd(scratch, ' ', i - len);
                    Stringcat(scratch, str);
                } else {
                    Stringcat(scratch, str);
                    if (-i > len) Stringnadd(scratch, ' ', -i - len);
                }
            }
            return newstr(scratch->s, scratch->len);

        case FN_STRCMP:
            return newint(strcmp(opdstr(2), opdstr(1)));

        case FN_STRNCMP:
            return newint(strncmp(opdstr(3), opdstr(2), opdint(1)));

        case FN_STRLEN:
            return newint(opdlen(1));

        case FN_SUBSTR:
            str = opdstr(n);
            len = opdlen(n);
            i = opdint(n - 1);
            if (i < 0) i = len + i;
            if (i < 0) i = 0;
            if (i > len) i = len;
            j = (n == 3) ? opdint(1) : len - i;
            if (j < 0) j = len - i + j;
            if (j < 0) j = 0;
            return newstr(str + i, j);

        case FN_STRSTR:
            str = opdstr(2);
            ptr = strstr(str, opdstr(1));
            return newint(ptr ? (ptr - str) : -1);

        case FN_STRCHR:
            str = opdstr(2);
            /* strcspn() is defined in regexp/regexp.c if not in libc. */
            i = strcspn(str, opdstr(1));
            return newint(str[i] ? i : -1);

        case FN_STRRCHR:
            str = opdstr(2);
            ptr = opdstr(1);
            for (i = opdlen(2) - 1; i >= 0; i--)
                for (j = 0; ptr[j]; j++)
                    if (str[i] == ptr[j]) return newint(i);
            return newint(-1);

        case FN_TOLOWER:
            for (Stringterm(scratch, 0), str = opdstr(1); *str; str++)
                Stringadd(scratch, lcase(*str));
            return newstr(scratch->s, scratch->len);

        case FN_TOUPPER:
            for (Stringterm(scratch, 0), str = opdstr(1); *str; str++)
                Stringadd(scratch, ucase(*str));
            return newstr(scratch->s, scratch->len);

        case FN_KBHEAD:
            return newstr(keybuf->s, keyboard_pos);

        case FN_KBTAIL:
            return newstr(keybuf->s + keyboard_pos, keybuf->len - keyboard_pos);

        case FN_KBPOINT:
            return newint(keyboard_pos);

        case FN_KBGOTO:
            return newint(igoto(opdint(1)));

        case FN_KBDEL:
            return (newint(do_kbdel(opdint(1))));

        case FN_KBMATCH:
            return newint(do_kbmatch());

        case FN_KBWLEFT:
            return newint(do_kbword(-1));

        case FN_KBWRIGHT:
            return newint(do_kbword(1));

        case FN_KBLEN:
            return newint(keybuf->len);

        case FN_GETOPTS:
            if (!argtext) return newint(0);
            str = opdstr(n);

            if (n>1) {
                CONST char *init = opdstr(n-1);
                for (ptr = str; *ptr; ptr++) {
                    Stringadd(Stringcpy(scratch, "opt_"), *ptr);
                    newlocalvar(scratch->s, init);
                    if (ptr[1] == ':') ptr++;
                }
            }

            startopt(argtext, str);
            while (i = 1, (c = nextopt((char **)&ptr, &i))) {
                if (isalpha(c)) {
                    Stringadd(Stringcpy(scratch, "opt_"), c);
                    if (ptr) {
                        newlocalvar(scratch->s, ptr);
                    } else {
                        smallstr numbuf;
                        sprintf(numbuf, "%ld", i);
                        newlocalvar(scratch->s, numbuf);
                    }
                } else {
                    return newint(0);
                }
            }
            while (argc > 0 && argv[0].end <= ptr) {
                argv++;
                argc--;
            }
            if (argc) {
                argv[0].start = argtext = ptr;
            }
            return newint(1);

        case FN_READ:
            /* This is a hack.  It's a useful feature, but doing it correctly
             * without blocking tf would require making the macro language
             * suspendable, which would have required a major redesign.  The
             * nested main_loop() method was easy to add, but leads to a few
             * quirks, like the odd handling of /dokey newline.
             */
            if (runall_depth) {
                eprintf("can't read() from a process.");
                return newstr("", 0);
            }
            if (read_depth) eprintf("warning: nested read()");
            oldblock = block;  /* condition and evalflag are already correct */
            block = 0;
            read_depth++; status_bar(STAT_READ);
            main_loop();
            read_depth--; status_bar(STAT_READ);
            block = oldblock;
            if (interrupted())
                return NULL;
            val = newstr(keybuf->s, keybuf->len);
            Stringterm(keybuf, keyboard_pos = 0);
            return val;

        case FN_SYSTYPE:
#ifdef PLATFORM_UNIX
            return newstr("unix", 4);
#else
# ifdef PLATFORM_OS2
            return newstr("os/2", 4);
# else
            return newstr("unknown", 7);
# endif
#endif

        default:
            tfprintf(tferr, "%% %s: not supported", id);
            return NULL;

        }

    } else if ((macro = find_macro(id)) || (handler = find_command(id))) {
        if (n > 1) {
            tfprintf(tferr, "%% %s:  command or macro called as function must have 0 or 1 argument", id);
            return NULL;
        }
        j = (macro) ?
            do_macro(macro, opdstr(1)) :
            (*handler)(Stringcpy(scratch, n ? opdstr(1) : "")->s);
        return newint(j);
    }

    tfprintf(tferr, "%% %s: no such function", id);
    return NULL;
}

static int comma_expr()
{
    if (!assignment_expr()) return 0;
    while (*ip == ',') {
        ip++;
        freeval(stack[--stacktop]);  /* throw it away */
        if (!assignment_expr()) return 0;
    }
    return 1;
}

static int assignment_expr()
{
    if (!conditional_expr()) return 0;
    if (ip[0] == ':' && ip[1] == '=') {
        ip += 2;
        if (!assignment_expr()) return 0;
        if (!reduce(OP_ASSIGN, 2)) return 0;
    }
    return 1;
}

static int conditional_expr()
{
    /* This is more like flow-control than expression, so we handle the stack
     * here instead of calling reduce().
     */
    int oldevalflag, oldcondition;

    if (!or_expr()) return 0;
    if (*ip == '?') {
        oldevalflag = evalflag;
        oldcondition = condition;
        evalflag = evalflag && condition && !breaking;
        condition = evalflag && opdint(1);

        while (isspace(*++ip));
        if (*ip == ':') {
            /* reuse condition value as true value */
        } else {
            freeval(stack[--stacktop]);  /* discard condition value */
            if (!comma_expr()) return 0;
            if (*ip != ':') {
                parse_error("expression", "':' after '?...'");
                return 0;
            }
        }
        if (!condition) freeval(stack[--stacktop]);  /* discard first value */
        condition = !condition;
        ip++;
        if (!conditional_expr()) return 0;
        if (!condition) freeval(stack[--stacktop]);  /* discard second value */

        evalflag = oldevalflag;
        condition = oldcondition;
    }
    return 1;
}

static int or_expr()
{
    /* This is more like flow-control than expression, so we handle the stack
     * here instead of calling reduce().
     */
    int oldcondition = condition;
    if (!and_expr()) return 0;
    while (*ip == '|') {
        ip++;
        condition = evalflag && condition && !breaking && !opdint(1);
        if (condition) freeval(stack[--stacktop]);    /* discard left value */
        if (!and_expr()) return 0;
        if (!condition) freeval(stack[--stacktop]);   /* discard right value */
    }
    condition = oldcondition;
    return 1;
}

static int and_expr()
{
    /* This is more like flow-control than expression, so we handle the stack
     * here instead of calling reduce().
     */
    int oldcondition = condition;
    if (!relational_expr()) return 0;
    while (*ip == '&') {
        ip++;
        condition = evalflag && condition && !breaking && opdint(1);
        if (condition) freeval(stack[--stacktop]);    /* discard left value */
        if (!relational_expr()) return 0;
        if (!condition) freeval(stack[--stacktop]);   /* discard right value */
    }
    condition = oldcondition;
    return 1;
}

static int relational_expr()
{
    char op;

    if (!additive_expr()) return 0;
    while (1) {
        if (ip[0] == '=') {
            if      (ip[1] == '~') op = OP_STREQ;
            else if (ip[1] == '/') op = OP_MATCH;
            else if (ip[1] == '=') op = OP_EQUAL;
            else op = '=';
        } else if (ip[0] == '!') {
            if      (ip[1] == '~') op = OP_STRNEQ;
            else if (ip[1] == '/') op = OP_NMATCH;
            else if (ip[1] == '=') op = OP_NOTEQ;
            else break;
        }
        else if (ip[0] == '>') op = (ip[1] == '=') ? OP_GTE : *ip;
        else if (ip[0] == '<') op = (ip[1] == '=') ? OP_LTE : *ip;
        else break;

        ip += 1 + !!(op & 0x80);     /* high bit means it's a 2-char op */
        if (!additive_expr()) return 0;
        if (!reduce(op, 2)) return 0;
    }
    return 1;
}

static int additive_expr()
{
    char op;
    if (!multiplicative_expr()) return 0;
    while (is_additive(*ip)) {
        op = *ip++;
        if (!multiplicative_expr()) return 0;
        if (!reduce(op, 2)) return 0;
    }
    return 1;
}

static int multiplicative_expr()
{
    char op;

    if (!unary_expr()) return 0;
    while (is_mult(*ip)) {
        op = *ip++;
        if (!unary_expr()) return 0;
        if (!reduce(op, 2)) return 0;
    }
    return 1;
}

static int unary_expr()
{
    char op;

    while (isspace(*ip)) ip++;


    if (is_unary(*ip)) {
        op = *ip++;
        if (op != '!' && op == *ip) {
            op = (*ip == '+') ? OP_PREINC : OP_PREDEC;
            ip++;
        }
        if (!unary_expr()) return 0;
        if (!reduce(op, 1)) return 0;
        return 1;

    } else {
        if (!primary_expr()) return 0;

        if (*ip == '(') {
            /* function call expression */
            int n = 1;
            for (++ip; isspace(*ip); ip++);
            if (*ip != ')') {
                while (1) {
                    if (!assignment_expr()) return 0;
                    n++;
                    if (*ip == ')') break;
                    if (*ip != ',') {
                        parse_error("expression",
                            "',' or ')' after function argument");
                        return 0;
                    }
                    ++ip;
                }
            }
            for (++ip; isspace(*ip); ip++);
            if (!reduce(OP_FUNC, n)) return 0;
        }
        return 1;
    }
}

static int primary_expr()
{
    CONST char *end;
    char quote;
    STATIC_BUFFER(static_buffer);
    int result;
    Stringp buffer;  /* gotta be reentrant */

    while (isspace(*ip)) ip++;
    if (isdigit(*ip)) {
#ifdef USE_FLOAT
        for (end = ip+1; isdigit(*end); end++);
        if (*end == '.' || ucase(*end) == 'E') {
            if (!pushval(newfloat(strtod(ip, (char**)&ip)))) return 0;
        } else
#endif /* USE_FLOAT */
        {
            if (!pushval(newint(parsetime((char **)&ip, NULL)))) return 0;
        }
#ifdef USE_FLOAT
    } else if (ip[0] == '.' && isdigit(ip[1])) {
        if (!pushval(newfloat(strtod(ip, (char**)&ip)))) return 0;
#endif /* USE_FLOAT */
    } else if (is_quote(*ip)) {
        Stringterm(static_buffer, 0);
        quote = *ip;
        for (ip++; *ip && *ip != quote; ip++) {
            if (*ip == '\\' && (ip[1] == quote || ip[1] == '\\')) ip++;
            Stringadd(static_buffer, isspace(*ip) ? ' ' : *ip);
        }
        if (!*ip) {
            eprintf("unmatched %c in expression string", quote);
            return 0;
        }
        ip++;
        if (!pushval(newstr(static_buffer->s, static_buffer->len))) return 0;
    } else if (isalpha(*ip) || *ip == '_') {
        for (end = ip + 1; isalnum(*end) || *end == '_'; end++);
        if (!pushval(newid(ip, end - ip))) return 0;
        ip = end;
    } else if (*ip == '$') {
        ++ip;
        Stringinit(buffer);
        result = dollarsub(buffer) && pushval(newstr(buffer->s, buffer->len));
        Stringfree(buffer);
        if (!result) return 0;
    } else if (*ip == '{') {
        if (!varsub(NULL)) return 0;
    } else if (*ip == '%') {
        ++ip;
        if (!varsub(NULL)) return 0;
    } else if (*ip == '(') {
        ++ip;
        if (!comma_expr()) return 0;
        if (*ip != ')') {
            parse_error("expression", "')' after '(...'");
            return 0;
        }
        ++ip;
    } else {
        parse_error("expression", "operand");
        return 0;
    }
    
    while (isspace(*ip)) ip++;
    return 1;
}

static int exprsub(dest)
    Stringp dest;
{
    int result = 0;
    Value *val;

    ip++; /* skip '[' */
    while (isspace(*ip)) ip++;
    if (!expr()) return 0;
    val = stack[--stacktop];
    if (!*ip || is_end_of_statement(ip)) {
        eprintf("unmatched $[");
    } else if (*ip != ']') {
        parse_error("expression", "operator");
    } else {
        if (val) Stringcat(dest, valstr(val));
        ++ip;
        result = 1;
    }
    freeval(val);
    return result;
}

#else /* NO_EXPR */

static int exprsub(dest)
    Stringp dest;
{
    eprintf("expressions are not supported.");
    return 0;
}

#endif /* NO_EXPR */

static int cmdsub(dest)
    Stringp dest;
{
    TFILE *oldout, *olderr, *file;
    Stringp buffer;
    int result, first = 1;
    Aline *aline;

    file = tfopen(NULL, "q");
    cmdsub_count++;
    oldout = tfout;
    olderr = tferr;
    tfout = file;
    /* tferr = file; */

    Stringinit(buffer);
    ip++; /* skip '(' */
    result = list(buffer, SUB_MACRO);
    Stringfree(buffer);

    tferr = olderr;
    tfout = oldout;
    cmdsub_count--;

    if (*ip != ')') {
        eprintf("unmatched (");
        tfclose(file);
        return 0;
    }

    while ((aline = dequeue(file->u.queue))) {
        if (!((aline->attrs & F_GAG) && gag)) {
            if (!first) Stringadd(dest, ' ');
            first = 0;
            Stringncat(dest, aline->str, aline->len);
        }
        free_aline(aline);
    }

    tfclose(file);
    ip++;
    return result;
}

static int macsub(dest)
    Stringp dest;
{
    STATIC_BUFFER(buffer);
    CONST char *body, *s;
    int bracket;

    if (*ip == '$') {
        while (*ip == '$') conditional_add(dest, *ip++);
        return 1;
    }

    Stringterm(buffer, 0);
    if ((bracket = (*ip == '{'))) ip++;
    while (*ip) {
        if (*ip == '\\') {
            ++ip;
            if (!backsub(dest)) return 0;
        } else if (is_end_of_statement(ip) || is_end_of_cmdsub(ip)) {
            break;
        } else if (*ip == '/') {
            ++ip;
            if (!slashsub(buffer)) return 0;
        } else if (*ip == '}') {
            /* note: in case of "%{var-$mac}", we break even if !bracket. */
            /* Users shouldn't use '}' in macro names anyway. */
            break;
        } else if (!bracket && isspace(*ip)) {
            break;
        } else if (*ip == '$') {
            if (ip[1] == '$') {
                while(*++ip == '$') Stringadd(buffer, *ip);
            } else {
                if (!bracket) break;
                else Stringadd(buffer, *ip++);
            }
        } else if (*ip == '%') {
            ++ip;
            if (!varsub(buffer)) return 0;
        } else {
            for (s = ip++; *ip && !ispunct(*ip) && !isspace(*ip); ip++);
            Stringncat(buffer, s, ip - s);
        }
    }
    if (bracket) {
        if (*ip != '}') {
            eprintf("unmatched ${");
            return 0;
        } else ip++;
    } else if (*ip == '$') {
        ip++;
    }

    if (breaking || !evalflag || !condition) return 1;

    if ((body = macro_body(buffer->s))) Stringcat(dest, body);
    else tfprintf(tferr, "%% macro not defined: %S", buffer);
    if (mecho) tfprintf(tferr, "%s $%S --> %s", mprefix, buffer, body);
    return 1;
}

static int backsub(dest)
    Stringp dest;
{
    if (isdigit(*ip)) {
        char c = strtochr(&ip);
        conditional_add(dest, mapchar(c));
    } else if (!backslash) {
        conditional_add(dest, '\\');
    } else if (*ip) {
        conditional_add(dest, *ip++);
    }
    return 1;
}

static int varsub(dest)
    String *dest;	/* if NULL, string result will be pushed onto stack */
{
    CONST char *value, *start;
    int bracket, except, ell = FALSE, pee = FALSE, n = -1;
    int first, last, empty = 0;
    STATIC_BUFFER(selector);
    Stringp buffer;	/* used when dest==NULL and a Stringp is needed */
    int stackflag;
    Value *val = NULL;

    if ((stackflag = !dest)) {
        Stringzero(buffer);
        dest = buffer;
    }

    if (*ip == '%') {
        while (*ip == '%') conditional_add(dest, *ip++);
        if (stackflag) return pushval(newstr(dest->s, dest->len));
        return 1;
    }
    if (!*ip || isspace(*ip)) {
        conditional_add(dest, '%');
        if (stackflag) return pushval(newstr(dest->s, dest->len));
        return 1;
    }

    if ((bracket = (*ip == '{' /*}*/ ))) ip++;

    if (ip[0] == '#' && (!bracket || ip[1] == /*{*/ '}')) {
        ++ip;
        if (!breaking && evalflag && condition) {
            Sprintf(dest, SP_APPEND, "%d", argc);
            if (stackflag)
                val = newstr(dest->s, dest->len);
        }
        empty = FALSE;
    } else if (ip[0] == '?' && (!bracket || ip[1] == /*{*/ '}')) {
        ++ip;
        if (!breaking && evalflag && condition) {
            Sprintf(dest, SP_APPEND, "%d", user_result);
            if (stackflag)
                val = newstr(dest->s, dest->len);
        }
        empty = FALSE;

    } else {
        if ((except = (*ip == '-'))) {
            ++ip;
        }
        start = ip;
        if ((ell = (ucase(*ip) == 'L')) || (pee = (ucase(*ip) == 'P'))) {
            ++ip;
        }
        if (isdigit(*ip)) {
            n = strtoint(&ip);
        } else if (*ip == '*') {
            ++ip;
            n = 0;
        }

        /* This is strange, for backward compatibility.  Some examples:
         * "%{1}x"  == parameter "1" followed by literal "x".
         * "%1x"    == parameter "1" followed by literal "x".
         * "%{1x}"  == bad substitution.
         * "%{L}x"  == parameter "L" followed by literal "x".
         * "%Lx"    == variable "Lx".
         * "%{Lx}"  == variable "Lx".
         * "%{L1}x" == parameter "L1" followed by literal "x".
         * "%L1x"   == parameter "L1" followed by literal "x".
         * "%{L1x}" == variable "L1x".
         */
        if (n < 0 || (bracket && (ell || pee))) {
            /* is non-special, or could be non-special if followed by alnum_ */
            if (isalnum(*ip) || (*ip == '_')) {
                ell = pee = FALSE;
                n = -1;
                do ip++; while (isalnum(*ip) || *ip == '_');
                Stringncpy(selector, start, ip - start);
            }
        }

        if (!breaking && evalflag && condition) {

            if (pee) {
                if (n < 0) n = 1;
                empty = (regsubstr(dest, n) <= 0);
                if (stackflag && !empty)
                    val = newstr(dest->s, dest->len);
                n = -1;
            } else if (ell) {
                if (n < 0) n = 1;
                if (except) first = 0, last = argc - n - 1;
                else first = last = argc - n;
            } else if (n == 0) {
                first = 0, last = argc - 1;
            } else if (n > 0) {
                if (except) first = n, last = argc - 1;
                else first = last = n - 1;
            } else if (cstrcmp(selector->s, "R") == 0) {
                if (argc > 0) {
                    n = 1;
                    first = last = RRAND(0, argc-1);
                } else empty = TRUE;
            } else if (cstrcmp(selector->s, "PL") == 0) {
                empty = (regsubstr(dest, -1) <= 0);
                if (stackflag && !empty)
                    val = newstr(dest->s, dest->len);
            } else if (cstrcmp(selector->s, "PR") == 0) {
                empty = (regsubstr(dest, -2) <= 0);
                if (stackflag && !empty)
                    val = newstr(dest->s, dest->len);
            } else {
                value = getnearestvar(selector->s, NULL);
                if (!(empty = !value || !*value))
                    Stringcat(dest, value);
                if (stackflag && !empty)
                    val = newstr(dest->s, dest->len);
            }

            if (n >= 0) {
                empty = (first > last || first < 0 || last >= argc);
                if (!empty) {
                    if (stackflag) {
                        val = newstr(argv[first].start,
                            argv[last].end - argv[first].start);
                    } else {
                        Stringncat(dest, argv[first].start,
                            argv[last].end - argv[first].start);
                    }
                }
            }

        } /* eval */
    }

    if (*ip == '-') {
        int oldevalflag = evalflag;

        /* if !evalflag, sub calls will parse ip but not write into dest. */
        evalflag = evalflag && empty;

        ++ip;
        while (*ip) {
            if (is_end_of_statement(ip) || is_end_of_cmdsub(ip)) {
                break;
            } else if (bracket && *ip == /*{*/ '}') {
                break;
            } else if (!bracket && isspace(*ip)) {
                break;
            } else if (*ip == '%') {
                ++ip;
                if (!varsub(dest)) return 0;
            } else if (*ip == '$') {
                ++ip;
                if (!dollarsub(dest)) return 0;
            } else if (*ip == '/') {
                ++ip;
                if (!slashsub(dest)) return 0;
            } else if (*ip == '\\') {
                ++ip;
                if (!backsub(dest)) return 0;
            } else {
                for (start = ip++; *ip && isalnum(*ip); ip++);
                if (!breaking && evalflag && condition)
                    Stringncat(dest, start, ip - start);
            }
        }

        evalflag = oldevalflag;
    }

    if (bracket) {
        if (*ip != /*{*/ '}') {
            eprintf("unmatched %%{ or bad substitution" /*}*/);
            return 0;
        } else ip++;
    }

    if (stackflag && empty) {
        /* create a value for the default */
        if (dest->s)
            val = newstr(dest->s, dest->len);
        else
            val = newstr("", 0);
    }

    return stackflag ? pushval(val) : 1;
}

static void conditional_add(s, c)
    Stringp s;
    int c;
{
    if (!breaking && evalflag && condition)
        Stringadd(s, c);
}

int handle_shift_command(args)
    char *args;
{
    int count;
    int error;

    if (!argv) return 0;
    count = (*args) ? atoi(args) : 1;
    if (count < 0) return 0;
    if ((error = (count > argc))) count = argc;
    argc -= count;
    argv += count;
    if (argc) argtext = argv[0].start;
    return !error;
}

#ifdef DMALLOC
void free_expand()
{
    Value *val;
    while (valpool) {
       val = valpool;
       valpool = valpool->u.next;
       FREE(val);
    }
}
#endif

