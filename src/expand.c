/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expand.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/********************************************************************
 * Fugue macro text interpreter
 *
 * Written by Ken Keys
 * Interprets expressions and macro statements.
 * Performs substitutions for positional parameters, variables, macro
 * bodies, and expressions.
 ********************************************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "macro.h"
#include "command.h"
#include "signals.h"
#include "socket.h"
#include "search.h"
#include "output.h"	/* newpos() */
#include "keyboard.h"	/* do_kbdel() */
#include "expand.h"
#include "commands.h"

#define MAX_ARGS 256

#define end_of_statement(p) ((p)[0] == '%' && ((p)[1] == ';' || (p)[1] == '\\'))
#define end_of_cmdsub(p) (cmdsub_count && *(p) == ')')

/* keywords: must be sorted, and numbered in incmrements of 1 */
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
#define OP_EQUAL    '\300'
#define OP_NOTEQ    '\301'
#define OP_GTE      '\302'
#define OP_LTE      '\303'
#define OP_STREQ    '\304'
#define OP_STRNEQ   '\305'
#define OP_MATCH    '\306'
#define OP_NMATCH   '\307'
#define OP_ASSIGN   '\310'
#define OP_FUNC     '\311'

#define TYPE_INT  1
#define TYPE_STR  2
#define TYPE_ID   3


typedef struct Arg {
    char *value;
    int spaces;
} Arg;

typedef struct Value {
    int type;
    union {
        int ival;
        char *sval;
    } u;
} Value;

#define STACKSIZE 128

static Arg **argv;                /* shifted argument vector */
static int argc;                  /* shifted argument count */
static int recur_count = 0;       /* expansion nesting count */
static int cmdsub_count = 0;      /* cmdsub nesting count */
static int user_result = 0;       /* user result */
static char *ip;                  /* instruction pointer */
static int condition = 1;         /* checked by /if and /while */
static int evalflag = 1;          /* flag: should we evaluate? */
static int block = 0;             /* type of current block */
static int breaking = 0;          /* flag: are we /break'ing? */
static Value *stack[STACKSIZE];
static int stacktop = 0;

static int    NDECL(keyword);
static int    FDECL(list,(Stringp dest, int subs));
static int    FDECL(statement,(Stringp dest, int subs));
static Value *FDECL(newint,(int i));
static Value *FDECL(newstr,(char *str, int len));
static void   FDECL(freeval,(Value *val));
static int    FDECL(valint,(Value *val));
static char  *FDECL(valstr,(Value *val));
static int    FDECL(slashsub,(Stringp dest));
static int    FDECL(macsub,(Stringp dest));
static int    FDECL(varsub,(Stringp dest));
static int    FDECL(backsub,(Stringp dest));
static int    FDECL(exprsub,(Stringp dest));
static int    FDECL(cmdsub,(Stringp dest));
static Value *NDECL(expr);
static int    NDECL(top_expr);
static int    NDECL(assignment_expr);
static int    NDECL(or_expr);
static int    NDECL(and_expr);
static int    NDECL(relational_expr);
static int    NDECL(additive_expr);
static int    NDECL(multiplicative_expr);
static int    NDECL(unary_expr);
static int    NDECL(function_expr);
static int    NDECL(primary_expr);
static int    FDECL(pushval,(Value *val));
static int    FDECL(reduce,(int op, int n));
static Value *FDECL(do_function,(int n));
static int    FDECL(countargs,(char *name, int req, int actual));
static int    NDECL(parse_error);


static Value *expr()
{
    int ok;
    int stackbot = stacktop;

    ok = top_expr();
    if (ok && stacktop < stackbot + 1) {
        tfputs("% internal error: expression stack underflow", tferr);
        ok = 0;
    } else if (ok && stacktop > stackbot + 1) {
        tfputs("% internal error: dirty expression stack", tferr);
        ok = 0;
    }
    while (stacktop > stackbot + ok) freeval(stack[--stacktop]);
    return (ok) ? stack[--stacktop] : NULL;
}

int handle_test_command(args)
    char *args;
{
    char *saved_ip = ip;
    int result = 0;
    Value *val;

    ip = args;
    if ((val = expr())) {
        if (*ip) parse_error();
        else result = valint(val);
        freeval(val);
    }
    ip = saved_ip;
    return result;
}

int process_macro(body, args, subs)
    char *body, *args;
    int subs;
{
    Stringp buffer;
    Arg **true_argv;                /* unshifted argument vector */
    int true_argc;                  /* unshifted argument count */
    int vecsize = 20, error = 0;
    char *in;
    int saved_cmdsub, saved_argc, saved_breaking;
    Arg **saved_argv;
    char *saved_ip;

    if (++recur_count > max_recur && max_recur) {
        tfputs("% Too many recursions.", tferr);
        recur_count--;
        return 0;
    }
    saved_cmdsub = cmdsub_count;
    saved_ip = ip;
    saved_argc = argc;
    saved_argv = argv;
    saved_breaking = breaking;

    ip = body;
    cmdsub_count = 0;

    newvarscope();

    argc = 0;
    if (args) {
        argv = (Arg **)MALLOC(vecsize * sizeof(Arg *));
        in = args = STRDUP(args);  /* dup so stringarg can write on it */
        while (*in && !error) {
            if (argc == vecsize)
                argv = (Arg**)REALLOC((char*)argv, sizeof(Arg*)*(vecsize+=10));
            argv[argc] = (Arg *) MALLOC(sizeof(Arg));
            if (!(argv[argc]->value = stringarg(&in, &(argv[argc]->spaces))))
                error++;
            argc++;
        }
    } else {
        argv = NULL;
    }

    true_argv = argv;
    true_argc = argc;

    if (!error) {
        Stringinit(buffer);
        if (!list(buffer, subs)) user_result = 0;
        Stringfree(buffer);
    }

    if (args) {
        while (--true_argc >= 0) FREE(true_argv[true_argc]);
        FREE(true_argv);
        FREE(args);
    }

    nukevarscope();

    cmdsub_count = saved_cmdsub;
    ip = saved_ip;
    argc = saved_argc;
    argv = saved_argv;
    breaking = saved_breaking;
    recur_count--;
    return user_result;
}

static int list(dest, subs)
    Stringp dest;
    int subs;
{
    int oneslash = 0, oldcondition, oldevalflag, oldblock, failed = 0;
    char *start = NULL;
    STATIC_BUFFER(mprefix_deep);

    /* Do NOT strip leading space here.  This allows user to type and send
     * lines with leading spaces (but completely empty lines are handled
     * by handle_input_line()).  During expansion, spaces AFTER a "%;"
     * or keyword will be skipped.
     */

    if (!*ip || end_of_cmdsub(ip)) user_result = 1;  /* empty list returns 1 */

    if (block == WHILE) start = ip;

    while(*ip) {
        if (interrupted()) {
            tfputs("% macro evaluation interrupted.", tferr);
            return 0;
        }
        /* Lines begining with one "/" are tf commands.  Lines beginning
         * with multiple "/"s have the first removed, and are sent to server.
         */
        oneslash = (*ip == '/') && (*++ip != '/');
        if (oneslash) {
            oldblock = block;
            block = keyword();
            if (block && subs < SUB_NEWLINE) subs = SUB_NEWLINE;
            switch(block) {
            case WHILE:
                oldevalflag = evalflag;
                oldcondition = condition;
                if (!list(dest, subs)) failed = 1;
                else if (block == WHILE) {
                cmderror("missing /do");
                    failed = 1;
                } else if (block == DO) {
                    cmderror("missing /done");
                    failed = 1;
                }
                evalflag = oldevalflag;
                condition = oldcondition;
                block = oldblock;
                if (failed) return 0;
                continue;
            case DO:
                if (oldblock != WHILE) {
                    cmderror("unexpected /do");
                    block = oldblock;
                    return 0;
                }
                evalflag = evalflag && condition;
                condition = user_result;
                break;
            case BREAK:
                if (!breaking && evalflag && condition) {
                    breaking = 1;
                }
                block = oldblock;
                break;
            case DONE:
                if (oldblock != DO) {
                    cmderror("unexpected /done");
                    block = oldblock;
                    return 0;
                }
                if (breaking || !condition || !evalflag) {
                    breaking = 0;
                    return 1;
                } else {
                    ip = start;
                    block = WHILE;
                    break;
                }
            case IF:
                oldevalflag = evalflag;
                oldcondition = condition;
                if (!list(dest, subs)) failed = 1;
                else if (block == IF || block == ELSEIF) {
                    cmderror("missing /then");
                    failed = 1;
                } else if (block == THEN || block == ELSE) {
                    cmderror("missing /endif");
                    failed = 1;
                }
                evalflag = oldevalflag;
                condition = oldcondition;
                block = oldblock;
                if (failed) return 0;
                continue;
            case THEN:
                if (oldblock != IF && oldblock != ELSEIF) {
                    cmderror("unexpected /then");
                    block = oldblock;
                    return 0;
                }
                evalflag = evalflag && condition;
                condition = user_result;
                break;
            case ELSEIF:
                if (oldblock != THEN) {
                    cmderror("unexpected /elseif");
                    block = oldblock;
                    return 0;
                }
                condition = !condition;
                break;
            case ELSE:
                if (oldblock != THEN) {
                    cmderror("unexpected /else");
                    block = oldblock;
                    return 0;
                }
                condition = !condition;
                break;
            case ENDIF:
                if (oldblock != THEN && oldblock != ELSE) {
                    cmderror("unexpected /endif");
                    block = oldblock;
                    return 0;
                }
                return 1;
            default:
                /* not a control statement */
                ip--;
                break;
            }
            if (block) {
                while (isspace(*ip)) ++ip;
                continue;
            }
            block = oldblock;
        }

        if (!statement(dest, subs)) return 0;

        /* I/O redirection will go here someday.  %>, %<, etc. */

        if (!breaking && evalflag && condition && dest->len) {
            extern int invis_flag;
            if (subs == SUB_MACRO && (mecho + !invis_flag) >= 2) {
                int i;
                Stringterm(mprefix_deep, 0);
                for (i = 0; i < recur_count + cmdsub_count; i++)
                    Stringcat(mprefix_deep, mprefix);
                tfprintf(tferr, "%S %S", mprefix_deep, dest);
            }
            if (oneslash) {
                user_result = handle_command(dest->s + 1);
            } else {
                if (subs == SUB_MACRO
                  || !(do_hook(H_SEND, NULL, "%S", dest) & F_GAG)) {
                    Stringadd(dest, '\n');
                    user_result = send_line(dest->s, dest->len);
                }
            }
        }

        Stringterm(dest, 0);
        if (end_of_cmdsub(ip)) break;
        /* Skip space between "%;" and next statement. */
        while (isspace(*ip)) ip++;
    }
    return 1;
}

static int keyword()
{
    char *end, save;
    int result;
    static char *keyword_table[] = {
        "break", "do", "done", "else", "elseif",
        "endif", "if", "then", "while"
    };

    for (end = ip; isalnum(*end) || *end == '_'; end++);
    save = *end;
    *end = '\0';
    result = binsearch(ip, (GENERIC*)keyword_table,
        sizeof(keyword_table)/sizeof(char*), sizeof(char*), cstrcmp);
    *end = save;
    if (result >= 0) for (ip = end; isspace(*ip); ip++);
    return (result >= 0) ? (BREAK + result) : 0;
}

static int statement(dest, subs)
    Stringp dest;
    int subs;
{
    char *start;

    while (*ip) {
        if (subs >= SUB_NEWLINE && *ip == '\\') {
            ++ip;
            if (!backsub(dest)) return 0;
        } else if (subs >= SUB_FULL && *ip == '/') {
            ++ip;
            if (!slashsub(dest)) return 0;
        } else if (subs >= SUB_NEWLINE && end_of_statement(ip)) {
            ip += 2;
            break;
        } else if (subs >= SUB_FULL && end_of_cmdsub(ip)) {
            break;
        } else if (subs >= SUB_FULL && ip[0] == '$' && ip[1] == '[') {
            ip += 2;
            if (!exprsub(dest)) return 0;
        } else if (subs >= SUB_FULL && ip[0] == '$' && ip[1] == '(') {
            ip += 2;
            if (!cmdsub(dest)) return 0;
        } else if (subs >= SUB_FULL && *ip == '$') {
            ++ip;
            if (!macsub(dest)) return 0;
        } else if (subs >= SUB_NEWLINE && ip[0] == '%' && ip[1] == '%') {
            for (ip++; *ip == '%'; ip++) Stringadd(dest, *ip);
        } else if (subs >= SUB_FULL && *ip == '%') {
            ++ip;
            if (!varsub(dest)) return 0;
        } else {
            /* strchr("\\/$%})", *ip) is more obvious here, but
             * ispunct(*ip) is a hell of a lot faster, increasing the
             * overall speed of statement() by about 10%.
             */
            for (start = ip++; *ip && !ispunct(*ip); ip++);
            Stringncat(dest, start, ip - start);
        }
    }

    return 1;
}

static int slashsub(dest)
    Stringp dest;
{
    if (*ip == '/' && oldslash) while (*ip == '/') Stringadd(dest, *ip++);
    else Stringadd(dest, '/');
    return 1;
}

static Value *newint(i)
    int i;
{
    Value *val;

    val = (Value *)MALLOC(sizeof(Value));
    val->type = TYPE_INT;
    val->u.ival = i;
    return val;
}

static Value *newstr(str, len)
    char *str;
    int len;
{
    Value *val;

    val = (Value *)MALLOC(sizeof(Value));
    val->type = TYPE_STR;
    strncpy(val->u.sval = (char *)MALLOC(len + 1), str, len);
    val->u.sval[len] = '\0';
    return val;
}

static void freeval(val)
    Value *val;
{
    if (val->type == TYPE_STR || val->type == TYPE_ID) FREE(val->u.sval);
    FREE(val);
}

/* return integer value of item */
static int valint(val)
    Value *val;
{
    char *str;
    int result;

    if (val->type == TYPE_INT) return val->u.ival;
    str = val->u.sval;
    if (val->type == TYPE_ID) {
        str = getnearestvar(str, &result);
        if (result != 0 || !str) return result;
    }
    if (*str == '-' || *str == '+') return atoi(str);
    return (isdigit(*str)) ? parsetime(&str, NULL) : 0;
}

/* return string value of item */
static char *valstr(val)
    Value *val;
{
    char *str;
    STATIC_BUFFER(buffer);

    switch (val->type) {
        case TYPE_INT: Sprintf(buffer, 0, "%d", val->u.ival); return buffer->s;
        case TYPE_STR: return val->u.sval;
        case TYPE_ID:  return (str=getnearestvar(val->u.sval,NULL)) ? str : "";
    }
    return NULL; /* impossible */
}

static int pushval(val)
    Value *val;
{
    if (stacktop == STACKSIZE) {
        cmderror("expression stack overflow");
        return 0;
    }
    stack[stacktop++] = val;
    return 1;
}

static int parse_error()
{
    char *end = ip + 1;

    if (isalnum(*ip) || *ip == '"') {
        while (isalnum(*end)) end++;
    }
    if (*ip) *end = '\0';
    tfprintf(tferr, "%% expression parse error before %s", *ip ? ip : "end");
    return 0;
}

/* get Nth operand from stack (counting backwards from top) */
#define opd(N) (stack[stacktop-(N)])

/* Pop n operands, apply op to them, and push result */
static int reduce(op, n)
    int op;   /* operator */
    int n;    /* number of operands */
{
    Value *val = NULL;
    char *str;
    int j;
    STATIC_BUFFER(buf);

    if (stacktop < n) {
        tfputs("% internal error:  stack underflow in reduce()", tferr);
        return 0;
    }
    Stringterm(buf, 0);

    switch (op) {
    case OP_ASSIGN: if (opd(2)->type != TYPE_ID) {
                        tfputs("% illegal left side of assignment.", tferr);
                    } else {
                        str = setnearestvar(opd(2)->u.sval, valstr(opd(1)));
                        val = newstr(str, strlen(str));
                    }
                    break;
    case '|':       val = newint(valint(opd(2))?valint(opd(2)):valint(opd(1)));
                    break;
    case '&':       val = newint(valint(opd(2))?valint(opd(1)):valint(opd(2)));
                    break;
    case '>':       val = newint(valint(opd(2)) > valint(opd(1)));    break;
    case '<':       val = newint(valint(opd(2)) < valint(opd(1)));    break;
    case '=':       /* fall thru to OP_EQUAL */
    case OP_EQUAL:  val = newint(valint(opd(2)) == valint(opd(1)));   break;
    case OP_NOTEQ:  val = newint(valint(opd(2)) != valint(opd(1)));   break;
    case OP_GTE:    val = newint(valint(opd(2)) >= valint(opd(1)));   break;
    case OP_LTE:    val = newint(valint(opd(2)) <= valint(opd(1)));   break;
    case OP_STREQ:  val = newint(strcmp(valstr(opd(2)), valstr(opd(1))) == 0);
                    break;
    case OP_STRNEQ: val = newint(strcmp(valstr(opd(2)), valstr(opd(1))) != 0);
                    break;
    case OP_MATCH:  val = newint(smatch_check(valstr(opd(1))) &&
                        smatch(valstr(opd(1)),valstr(opd(2)))==0);
                    break;
    case OP_NMATCH: val = newint(smatch_check(valstr(opd(1))) &&
                        smatch(valstr(opd(1)),valstr(opd(2)))!=0);
                    break;
    case '+':       if (n==1) val = newint(valint(opd(1)));
                    else val = newint(valint(opd(2)) + valint(opd(1)));
                    break;
    case '-':       if (n==1) val = newint(-valint(opd(1)));
                    else val = newint(valint(opd(2)) - valint(opd(1)));
                    break;
    case '*':       val = newint(valint(opd(2)) * valint(opd(1)));     break;
    case '/':       if (block == IF && opd(1)->type == TYPE_ID)
                        /* catch common error: "/if /test <expr> /then ..." */
                        tfprintf(tferr,
                            "%% warning: possibly missing %%; before /%s",
                            opd(1)->u.sval);
                    if ((j = valint(opd(1))) == 0) {
                        cmderror("division by zero");
                    } else {
                        val = newint(valint(opd(2)) / j);
                    }
                    break;
    case '!':       val = newint(!valint(opd(1)));                     break;
    case OP_FUNC:   val = do_function(n);                           break;
    default:        tfprintf(tferr, "%% internal error: reduce: unknown op %c",
                        op);
                    break;
    }

    stacktop -= n;
    while (n) freeval(stack[stacktop + --n]);
    if (val) pushval(val);

    return val ? 1 : 0;
}

static Value *do_function(n)
    int n;    /* number of operands (including function id) */
{
    int i, j;
    char *id, *str, *ptr;
    extern int keyboard_pos;
    extern Stringp keybuf;
    Handler *handler;
    Macro *macro;
    STATIC_BUFFER(buf);

    if (opd(n)->type != TYPE_ID) {
        tfputs("%% function name must be an identifier.", tferr);
        return NULL;
    }
    id = opd(n--)->u.sval;

    /* This should use a table if it gets any bigger. */

    if (strcmp(id, "rand") == 0) {
        if (n == 0) {
            return newint(RANDOM());
        } else if (n == 1) {
            return newint(RANDOM() % valint(opd(1)));
        } else if (n == 2) {
            i = valint(opd(2));
            return newint(RANDOM() % (valint(opd(1)) - i + 1) + i);
        } else {
            tfputs("rand: too many arguments", tferr);
            return NULL;
        }
    } else if (strcmp(id, "regmatch") == 0) {
        regexp *re;
        if (!countargs(id, 2, n)) return NULL;
        if (!(re = regcomp(valstr(opd(2))))) return newint(0);
        return newint(regexec_and_hold(re, valstr(opd(1)), TRUE));
    } else if (strcmp(id, "strcat") == 0) {
        for (Stringterm(buf, 0); n; Stringcat(buf, valstr(opd(n--))));
        return newstr(buf->s, buf->len);
    } else if (strcmp(id, "strrep") == 0) {
        if (!countargs(id, 2, n)) return NULL;
        if ((i = valint(opd(1))) < 0) i = 0;
        str = valstr(opd(2));
        for (Stringterm(buf, 0); i; i--) Stringcat(buf, str);
        return newstr(buf->s, buf->len);
    } else if (strcmp(id, "strcmp") == 0) {
        if (!countargs(id, 2, n)) return NULL;
        return newint(strcmp(valstr(opd(2)), valstr(opd(1))));
    } else if (strcmp(id, "strlen") == 0) {
        if (!countargs(id, 1, n)) return NULL;
        return newint(strlen(valstr(opd(1))));
    } else if (strcmp(id, "substr") == 0) {
        if (!countargs(id, 3, n)) return NULL;
        str = valstr(opd(3));
        if ((i = valint(opd(2))) < 0) i = 0;
        if ((j = valint(opd(1))) < 0) j = 0;
        if (i >= strlen(str)) return newstr("", 0);
        Stringncpy(buf, str + i, j);
        return newstr(buf->s, buf->len);
    } else if (strcmp(id, "strstr") == 0) {
        if (!countargs(id, 2, n)) return NULL;
        str = valstr(opd(2));
        ptr = STRSTR(str, valstr(opd(1)));
        return newint(ptr ? (ptr - str) : -1);
    } else if (strcmp(id, "strchr") == 0) {
        if (!countargs(id, 2, n)) return NULL;
        str = valstr(opd(2));
        ptr = valstr(opd(1));
        for (i = 0; str[i]; i++)
            for (j = 0; ptr[j]; j++)
                if (str[i] == ptr[j]) return newint(i);
        return newint(-1);
    } else if (strcmp(id, "strrchr") == 0) {
        if (!countargs(id, 2, n)) return NULL;
        str = valstr(opd(2));
        ptr = valstr(opd(1));
        for (i = strlen(str) - 1; i >= 0; i--)
            for (j = 0; ptr[j]; j++)
                if (str[i] == ptr[j]) return newint(i);
        return newint(-1);
    } else if (strcmp(id, "tolower") == 0) {
        if (!countargs(id, 1, n)) return NULL;
        Stringterm(buf, 0);
        for (str = valstr(opd(1)); *str; str++) Stringadd(buf, lcase(*str));
        return newstr(buf->s, buf->len);
    } else if (strcmp(id, "toupper") == 0) {
        if (!countargs(id, 1, n)) return NULL;
        Stringterm(buf, 0);
        for (str = valstr(opd(1)); *str; str++) Stringadd(buf, ucase(*str));
        return newstr(buf->s, buf->len);
    } else if (strcmp(id, "kbhead") == 0) {
        if (!countargs(id, 0, n)) return NULL;
        return newstr(keybuf->s, keyboard_pos);
    } else if (strcmp(id, "kbtail") == 0) {
        if (!countargs(id, 0, n)) return NULL;
        Stringncpy(buf, keybuf->s + keyboard_pos, keybuf->len - keyboard_pos);
        return newstr(buf->s, buf->len);
    } else if (strcmp(id, "kbpoint") == 0) {
        if (!countargs(id, 0, n)) return NULL;
        return newint(keyboard_pos);
    } else if (strcmp(id, "kbgoto") == 0) {
        if (!countargs(id, 1, n)) return NULL;
        newpos(valint(opd(1)));
        return newint(keyboard_pos);
    } else if (strcmp(id, "kbdel") == 0) {
        if (!countargs(id, 1, n)) return NULL;
        if ((i = valint(opd(1))) < 0) i = 0;
        return (newint(do_kbdel(i)));
    } else if ((macro = find_macro(id)) || (handler = find_command(id))) {
        if (n > 1) {
            tfprintf(tferr, "%% %s:  command or macro called as function must take 0 or 1 arguments", id);
            return NULL;
        }
        if (macro) return newint(do_macro(macro, valstr(opd(1))));
        else return newint((*handler)(valstr(opd(1))));
    }
    tfprintf(tferr, "%% %s: no such function", id);
    return NULL;
}

static int countargs(name, req, actual)
    char *name;
    int req, actual;
{
    if (req == actual) return 1;
    tfprintf(tferr, "%% %s: incorrect number of arguments", name);
    return 0;
}

static int top_expr()
{
    if (!assignment_expr()) return 0;
    while (*ip == ',') {
        ip++;
        freeval(stack[--stacktop]);
        if (!assignment_expr()) return 0;
    }
    return 1;
}

static int assignment_expr()
{
    if (!or_expr()) return 0;
    if (ip[0] == ':' && ip[1] == '=') {
        ip += 2;
        if (!assignment_expr()) return 0;
        if (!reduce(OP_ASSIGN, 2)) return 0;
    }
    return 1;
}

static int or_expr()
{
    if (!and_expr()) return 0;
    while (*ip == '|') {
        ip++;
        if (!and_expr()) return 0;
        if (!reduce('|', 2)) return 0;
    }
    return 1;
}

static int and_expr()
{
    if (!relational_expr()) return 0;
    while (*ip == '&') {
        ip++;
        if (!relational_expr()) return 0;
        if (!reduce('&', 2)) return 0;
    }
    return 1;
}

static int relational_expr()
{
    char op;
    if (!additive_expr()) return 0;
    while (*ip) {
        if      (ip[0] == '=' && ip[1] == '~') op = OP_STREQ;
        else if (ip[0] == '!' && ip[1] == '~') op = OP_STRNEQ;
        else if (ip[0] == '=' && ip[1] == '/') op = OP_MATCH;
        else if (ip[0] == '!' && ip[1] == '/') op = OP_NMATCH;
        else if (ip[0] == '>' && ip[1] == '=') op = OP_GTE;
        else if (ip[0] == '<' && ip[1] == '=') op = OP_LTE;
        else if (ip[0] == '=' && ip[1] == '=') op = OP_EQUAL;
        else if (ip[0] == '!' && ip[1] == '=') op = OP_NOTEQ;
        else if (strchr("<>=", *ip)) op = *ip;
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
    while (*ip) {
        if (ip[0] == '+') op = '+';
        else if (ip[0] == '-' && ip[1] != '~') op = '-';
        else break;
        ip++;
        if (!multiplicative_expr()) return 0;
        if (!reduce(op, 2)) return 0;
    }
    return 1;
}

static int multiplicative_expr()
{
    char op;

    if (!unary_expr()) return 0;
    while ((op = *ip) && strchr("*/", op)) {
        ++ip;
        if (!unary_expr()) return 0;
        if (!reduce(op, 2)) return 0;
    }
    return 1;
}

static int unary_expr()
{
    char op;

    while (isspace(*ip)) ip++;
    if ((op = *ip) && strchr("!+-", op = *ip)) {
        ++ip;
        if (!unary_expr()) return 0;
        if (!reduce(op, 1)) return 0;
        return 1;
    } else return function_expr();
}

static int function_expr()
{
    int n = 0;

    if (!primary_expr()) return 0;
    if (*ip == '(') {
        for (++ip; isspace(*ip); ip++);
        if (*ip != ')') {
            while (1) {
                if (!assignment_expr()) return 0;
                n++;
                if (*ip == ')') break;
                if (*ip != ',') return parse_error();
                ++ip;
            }
        }
        for (++ip; isspace(*ip); ip++);
        if (!reduce(OP_FUNC, n + 1)) return 0;
    }
    return 1;
}

static int primary_expr()
{
    char *end, quote;
    STATIC_BUFFER(buffer);

    while (isspace(*ip)) ip++;
    if (*ip == '(') {
        ++ip;
        if (!top_expr()) return 0;
        if (*ip != ')') {
            tfputs("% missing )", tferr);
            return 0;
        }
        ++ip;
    } else if (isdigit(*ip)) {
        if (!pushval(newint(parsetime(&ip, NULL)))) return 0;
    } else if (*ip && strchr("\"'`", *ip)) {
        Stringterm(buffer, 0);
        quote = *ip;
        for (ip++; *ip && *ip != quote; Stringadd(buffer, *ip++))
            if (*ip == '\\' && (ip[1] == quote || ip[1] == '\\')) ip++;
        if (!*ip) {
            tfprintf(tferr, "%S: unmatched %c in expression string",
                error_prefix(), quote);
            return 0;
        }
        ip++;
        pushval(newstr(buffer->s, buffer->len));
    } else if (isalpha(*ip) || *ip == '_') {
        for (end = ip + 1; isalnum(*end) || *end == '_'; end++);
        pushval(newstr(ip, end - ip));      /* This only works because STR */
        stack[stacktop-1]->type = TYPE_ID;  /* and ID have the same structure */
        ip = end;
    } else if (*ip == '%') {
        ++ip;
        Stringterm(buffer, 0);
        if (!varsub(buffer)) return 0;
        if (!pushval(newstr(buffer->s, buffer->len))) return 0;
    } else {
        return parse_error();
    }
    
    while (isspace(*ip)) ip++;
    return 1;
}

static int exprsub(dest)
    Stringp dest;
{
    int result = 0;
    Value *val = NULL;

    while (isspace(*ip)) ip++;
    if (!(val = expr())) return 0;
    if (end_of_statement(ip)) {
        cmderror("unmatched $[");
    } else if (*ip != ']') {
        parse_error();
    } else {
        Stringcat(dest, valstr(val));
        ++ip;
        result = 1;
    }
    freeval(val);
    return result;
}

static int cmdsub(dest)
    Stringp dest;
{
    TFILE *oldout, *olderr, *file;
    extern TFILE *tfout, *tferr;
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
    result = list(buffer, SUB_MACRO);
    Stringfree(buffer);

    tferr = olderr;
    tfout = oldout;
    cmdsub_count--;

    if (*ip != ')') {
        cmderror("unmatched (");
        tfclose(file);
        return 0;
    }

    while ((aline = dequeue(file->u.queue))) {
        if (!((aline->attrs & F_GAG) && gag)) {
            if (!first) Stringadd(dest, ' ');
            first = 0;
            Stringcat(dest, aline->str);
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
    char *body, *start;
    int bracket;

    if (*ip == '$') {
        while (*ip == '$') Stringadd(dest, *ip++);
        return 1;
    }

    Stringterm(buffer, 0);
    if ((bracket = (*ip == '{'))) ip++;
    while (*ip) {
        if (*ip == '\\') {
            ++ip;
            if (!backsub(dest)) return 0;
        } else if (end_of_statement(ip) || end_of_cmdsub(ip)) {
            break;
        } else if (*ip == '/') {
            ++ip;
            if (!slashsub(buffer)) return 0;
        } else if (bracket && *ip == '}') {
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
            for (start = ip++; *ip && !ispunct(*ip); ip++);
            Stringncat(buffer, start, ip - start);
        }
    }
    if (bracket) {
        if (*ip != '}') {
            cmderror("unmatched ${");
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
        Stringadd(dest, strtochr(&ip));
    } else if (!backslash) {
        Stringadd(dest, '\\');
    } else if (*ip) {
        Stringadd(dest, *ip++);
    }
    return 1;
}

static int varsub(dest)
    Stringp dest;
{
    String *buffer;
    char *value, *start;
    int bracket, except, ell = FALSE, pee = FALSE, i, n = -1, needloop = TRUE;
    int first, last, done = 0;
    STATIC_BUFFER(selector);
    STATIC_BUFFER(defalt);

    buffer = selector;
    if (*ip == '%') {
        while (*ip == '%') Stringadd(dest, *ip++);
        return 1;
    }
    if (!*ip || *ip == ' ') {
        Stringadd(dest, '%');
        return 1;
    }

    /* this is too hairy. */

    Stringterm(selector, 0);
    if ((bracket = (*ip == '{'))) ip++;
    if (*ip == '#') {
        Sprintf(dest, SP_APPEND, "%d", argc);
        done = 1;
        ip++;
    } else if (*ip == '?') {
        Sprintf(dest, SP_APPEND, "%d", user_result);
        done = 1;
        ip++;
    } else {
        if ((except = (ip[0] == '-' && (isdigit(ip[1]) || ucase(ip[1])=='L'))))
            ip++;
        if ((ell = (ucase(*ip) == 'L'))) {
            Stringadd(selector, *ip++);
            n = 1;
        } else if ((pee = (ucase(*ip) == 'P'))) {
            Stringadd(selector, *ip++);
            n = 1;
        }
        if (isdigit(*ip)) {
            n = atoi(ip);
            while (isdigit(*ip)) Stringadd(selector, *ip++);
            needloop = (*ip == '-');
        } else if (*ip == '*') {
            n = 0;
            needloop = (*++ip == '-');
        }
    }
    if (!done && needloop) while (*ip) {
        if (end_of_statement(ip) || end_of_cmdsub(ip)) {
            break;
        } else if (bracket && *ip == '}') {
            break;
        } else if (!bracket && *ip == ' ') {
            break;
        } else if (*ip == '-') {
            if (buffer == selector) buffer = Stringterm(defalt, 0);
            else Stringadd(buffer, *ip);
            ip++;
        } else if (buffer == selector && !isalnum(*ip) && *ip != '_') {
            break;
        } else if (buffer == defalt && *ip == '/') {
            ++ip;
            if (!slashsub(buffer)) return 0;
        } else {
            for (start = ip++; *ip && isalnum(*ip); ip++);
            Stringncat(buffer, start, ip - start);
        }
        if (buffer == selector) n = -1, ell = pee = FALSE;
    }
    if (bracket) {
        if (*ip != '}') {
            cmderror("unmatched %{");
            return 0;
        } else ip++;
    }

    if (done || breaking || !evalflag || !condition) return 1;

    if (pee) {
        regsubstr(dest, n);
        n = -1;
    } else if (ell) {
        if (except) first = 0, last = argc - n - 1;
        else first = last = argc - n;
    } else if (n == 0) {
        first = 0, last = argc - 1;
    } else if (n > 0) {
        if (except) first = n, last = argc - 1;
        else first = last = n - 1;
    } else if (cstrcmp(selector->s, "R") == 0) {
        if (argc > 0) n = first = last = RANDOM() % argc;
        else if (buffer == defalt) SStringcat(dest, defalt);
    } else {
        value = getnearestvar(selector->s, NULL);
        if (value && *value) {
            Stringcat(dest, value);
        } else if (buffer == defalt) {
            SStringcat(dest, defalt);
        }
    }

    if (n >= 0) {
        if (first > last || first < 0 || last >= argc) {
            if (buffer == defalt) SStringcat(dest, defalt);
        } else {
            for (i = first; i <= last; i++) {
                Stringcat(dest, argv[i]->value);
                if (i != last) Stringnadd(dest, ' ', argv[i]->spaces);
            }
        }
    }

    return 1;
}

int do_shift(count)
    int count;
{
    int error;

    if (!argv) return !0;
    if ((error = (count > argc))) count = argc;
    argc -= count;
    argv += count;
    return !error;
}
