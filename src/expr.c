/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expr.c,v 35004.40 1999/01/31 00:27:42 hawkeye Exp $ */


/********************************************************************
 * Fugue expression interpreter
 *
 * Written by Ken Keys
 * Parses and evaluates expressions.
 ********************************************************************/

#include "config.h"
#include <math.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "macro.h"
#include "signals.h"	/* interrupted() */
#include "socket.h"	/* sockidle() */
#include "search.h"
#include "output.h"	/* igoto() */
#include "keyboard.h"	/* do_kb*() */
#include "parse.h"
#include "expand.h"
#include "expr.h"
#include "commands.h"
#include "command.h"
#include "variable.h"
#include "tty.h"	/* no_tty */
#include "history.h"	/* log_count */
#include "world.h"	/* new_world() */


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

#define STACKSIZE 128

int stacktop = 0;
Value *stack[STACKSIZE];

static Value *valpool = NULL;		/* freelist */


#ifndef NO_FLOAT
# define newfloat(f)  newfloat_fl(f, __FILE__, __LINE__)
static Value *FDECL(newfloat_fl,(double f, CONST char *file, int line));
static double FDECL(valfloat,(Value *val));
static int    FDECL(mathtype,(Value *val));
#endif /* NO_FLOAT */
static int    FDECL(vallen,(Value *val));
static long   FDECL(valint,(Value *val));
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
static int    FDECL(reduce,(int op, int n));
static Value *FDECL(function_switch,(int symbol, int n, CONST char *parent));
static Value *FDECL(do_function,(int n));


/* get Nth operand from stack (counting backwards from top) */
#define opd(N)      (stack[stacktop-(N)])
#define opdfloat(N) valfloat(opd(N))
#define opdint(N)   valint(opd(N))
#define opdbool(N)  valbool(opd(N))
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


int expr()
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
    breaking = exiting ? -1 : old_breaking;

    return ok;
}

/* Returns the value of expression.  Caller must freeval() the value. */
Value *expr_value(expression)
    CONST char *expression;
{
    Value *result = NULL;
    CONST char *saved_ip = ip;

    ip = expression;
    if (expr()) {
        if (*ip) parse_error("expression", "operator");
        result = stack[--stacktop];
    }
    ip = saved_ip;
    return result;
}

Value *expr_value_safe(expression)
    CONST char *expression;
{
    Value *result;
    TFILE *old_tfin = tfin, *old_tfout = tfout;
    result = expr_value(expression);
    tfin = old_tfin;    /* in case expression closed tfin */
    tfout = old_tfout;  /* in case expression closed tfout */
    return result;
}


#ifndef NO_FLOAT
static Value *newfloat_fl(f, file, line)
    double f;
    CONST char *file;
    int line;
{
    Value *val;

    if (breaking || !evalflag || !condition) return NULL;
    palloc(val, Value, valpool, u.next, file, line);
    val->count = 1;
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
        if (result >= 0 || !str) return TYPE_INT;
    }
    while (is_space(*str)) ++str;
    while (*str == '-' || *str == '+') ++str;
    if (str[0] == '.' && is_digit(str[1])) return TYPE_FLOAT;
    if (!is_digit(*str)) return TYPE_INT;
    ++str;
    while (is_digit(*str)) ++str;
    if (*str == '.') return TYPE_FLOAT;
    if (ucase(*str) != 'E') return TYPE_INT;
    ++str;
    if (*str == '-' || *str == '+') ++str;
    if (is_digit(*str)) return TYPE_FLOAT;
    return TYPE_INT;
}
#endif /* NO_FLOAT */

Value *newint_fl(i, file, line)
    long i;
    CONST char *file;
    int line;
{
    Value *val;

    if (breaking || !evalflag || !condition) return NULL;
    palloc(val, Value, valpool, u.next, file, line);
    val->count = 1;
    val->type = TYPE_INT;
    val->u.ival = i;
    val->len = -1;
    return val;
}

Value *newstrid(str, len, type, file, line)
    CONST char *str, *file;
    int len, type, line;
{
    Value *val;
    char *new;

    if (breaking || !evalflag || !condition) return NULL;
    palloc(val, Value, valpool, u.next, file, line);
    val->count = 1;
    val->type = type;
    if (len < 0) len = strlen(str);
    new = strncpy((char *)xmalloc(len + 1, file, line), str, len);
    new[len] = '\0';
    val->u.sval = new;
    val->len = len;
    return val;
}

void freeval(val)
    Value *val;
{
    if (!val) return;
    if (--val->count) return;
    if (val->type == TYPE_STR || val->type == TYPE_ID) FREE(val->u.sval);
    pfree(val, valpool, u.next);
}

/* return boolean value of item */
int valbool(val)
    Value *val;
{
    if (!val) return 0;
#ifndef NO_FLOAT
    if (mathtype(val) == TYPE_FLOAT) return !!val->u.fval;
#endif
    return !!valint(val);
}

/* return integer value of item */
static long valint(val)
    Value *val;
{
    CONST char *str;
    long result;

    if (!val) return 0;
    if (val->type == TYPE_INT) return val->u.ival;
#ifndef NO_FLOAT
    if (val->type == TYPE_FLOAT) return (int)val->u.fval;
#endif
    str = val->u.sval;
    if (val->type == TYPE_ID) {
        str = getnearestvar(str, &result);
        if (result >= 0) return result;
        if (!str) return 0;
    }
    while (is_space(*str)) ++str;
    if (*str == '-' || *str == '+') return atol(str);
    if (is_digit(*str)) return parsetime((char **)&str, NULL);
    if (*str && pedantic)
        eprintf("warning: non-numeric string value used in numeric context");
    return 0;
}

#ifndef NO_FLOAT
/* return floating value of item */
static double valfloat(val)
    Value *val;
{
    CONST char *str;
    double result;
    long i;

    if (!val) return 0.0;
    errno = 0;
    if (val->type == TYPE_FLOAT) return val->u.fval;
    if (val->type == TYPE_INT) return (double)val->u.ival;
    str = val->u.sval;
    if (val->type == TYPE_ID) {
        str = getnearestvar(str, &i);
        if (i >= 0) return (double)i;
        if (!str) return 0.0;
    }
    result = strtod(str, (char**)&str);
    return result;
}
#endif /* NO_FLOAT */

/* return string value of item (only valid for lifetime of val!) */
CONST char *valstr(val)
    Value *val;
{
    CONST char *str;
    static char buffer[32];

    if (!val) return "";
    switch (val->type) {
        case TYPE_INT:  sprintf(buffer, "%ld", val->u.ival); return buffer;
        case TYPE_STR:  return val->u.sval;
        case TYPE_ID:   return (str=getnearestvar(val->u.sval,NULL)) ? str : "";
#ifndef NO_FLOAT
        case TYPE_FLOAT:
            /* Note: with more than 15 significant figures, rounding errors
             * become visible. */
            sprintf(buffer, "%.15g", val->u.fval);
            if (!strchr(buffer, '.'))
                strcat(buffer, ".0");
            return buffer;
#endif
    }
    return NULL; /* impossible */
}

/* return length of string value of item */
static int vallen(val)
    Value *val;
{
    if (!val) return 0;
    return (val->type == TYPE_STR) ? val->len : strlen(valstr(val));
}

int pushval(val)
    Value *val;
{
    if (stacktop == STACKSIZE) {
        eprintf("expression stack overflow");
        freeval(val);
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
#ifndef NO_FLOAT
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
        /* common error: "/if (expr /true_branch ..." */
        eprintf("%% warning: possibly missing %s before /%s", "%; or )",
            opd(1)->u.sval);
    }

#ifndef NO_FLOAT
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
        case '/':       val = newfloat(f[2] / f[1]);               break;
        case '!':       val = newint(!f[1]);                       break;
        default:        break;
        }
        if (val->type == TYPE_FLOAT &&
            (val->u.fval == HUGE_VAL || val->u.fval == -HUGE_VAL))
        {
            /* note: only single-char ops can overflow */
            eprintf("%c operator: arithmetic overflow", op);
            freeval(val);
            val = NULL;
        }
    } else
#endif /* NO_FLOAT */

    if ((op == OP_ASSIGN || op == OP_PREINC || op == OP_PREDEC) &&
        (opd(n)->type != TYPE_ID))
    {
        eprintf("illegal object of assignment");

    } else {
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
        case OP_FUNC:   val = do_function(n);                            break;
        default:        internal_error(__FILE__, __LINE__);
                        eprintf("internal error: reduce: bad op %#o", op);
                        break;
        }
    }

    stacktop -= n;
    while (n) freeval(stack[stacktop + --n]);
    return val ? pushval(val) : 0;
}

static Value *function_switch(symbol, n, parent)
    int symbol, n;
    CONST char *parent;
{
    int oldblock;
    long i, j, len;
    char c;
    CONST char *str, *ptr;
    regexp *re;
    FILE *file;
    TFILE *tfile;
    STATIC_BUFFER(scratch);

        switch (symbol) {

        case FN_ADDWORLD:
            /* addworld(name, type, host, port, char, pass, file, use_proxy) */

            if (restriction >= RESTRICT_WORLD) {
                eprintf("restricted");
                return newint(0);
            }

            str = opdstr(n-0);  /* name */
            i = 1;  /* use_proxy */
            if (n > 7) {
                i = enum2int(opdstr(n-7), enum_flag, "arg 8 (use_proxy)");
                if (i < 0) return newint(0);
            }

            return newint(!!new_world(str,  /* name */
                n>4 ? opdstr(n-4) : "",     /* char */
                n>5 ? opdstr(n-5) : "",     /* pass */
                opdstr(n-2), opdstr(n-3),   /* host, port */
                n>6 ? opdstr(n-6) : "",     /* mfile */
                opdstr(n-1),                /* type */
                i ? 0 : WORLD_NOPROXY));    /* flags */

        case FN_COLUMNS:
            return newint(columns);

        case FN_LINES:
            return newint(lines);

        case FN_ECHO:
            i = (n>=3) ? enum2int(opdstr(n-2), enum_flag, "arg 3 (inline)") : 0;
            if (i < 0) return newint(0);
            return newint(handle_echo_func(opdstr(n),
                (n >= 2) ? opdstr(n-1) : "",
                i, (n >= 4) ? opdstr(n-3) : "o"));

        case FN_SUBSTITUTE:
            i = (n>=3) ? enum2int(opdstr(n-2), enum_flag, "arg 3 (inline)") : 0;
            if (i < 0) return newint(0);
            return newint(handle_substitute_func(opdstr(n),
                (n >= 2) ? opdstr(n-1) : "",  i));

        case FN_SEND:
            j = n>2 ? enum2int(opdstr(n-2), enum_flag, "arg 3 (send_nl)") : 1;
            if (j < 0) return newint(0);
            i = handle_send_function(opdstr(n), (n>1 ? opdstr(n-1) : NULL), j);
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

        case FN_TFOPEN:
            return newint(handle_tfopen_func(
                n<2 ? "" : opdstr(2), n<1 ? "q" : opdstr(1)));

        case FN_TFCLOSE:
            str = opdstr(1);
            if (!str[1]) {
                switch(lcase(str[0])) {
                case 'i':  tfin = NULL;  return newint(0);
                case 'o':  tfout = NULL; return newint(0);
                case 'e':  eprintf("tferr can not be closed.");
                           return newint(-1);
                default:   break;
                }
            }
            tfile = find_tfile(str);
            return newint(tfile ? tfclose(tfile) : -1);

        case FN_TFWRITE:
            tfile = (n > 1) ? find_usable_tfile(opdstr(2), S_IWUSR) : tfout;
            if (!tfile) return newint(-1);
            str = opdstr(1);
            tfputs(str, tfile);
            return newint(1);

        case FN_TFREAD:
            tfile = (n > 1) ? find_usable_tfile(opdstr(2), S_IRUSR) : tfin;
            if (!tfile) return newint(-1);
            if (opd(1)->type != TYPE_ID) {
                eprintf("arg %d: illegal object of assignment", n);
                return newint(-1);
            }
            oldblock = block;  /* condition and evalflag are already correct */
            block = 0;
            j = -1;
            if (tfgetS(scratch, tfile)) {
                if (setnearestvar(opd(1)->u.sval, scratch->s))
                    j = scratch->len;
            }
            block = oldblock;
            return newint(j);

        case FN_TFFLUSH:
            tfile = find_usable_tfile(opdstr(n), S_IWUSR);
            if (!tfile) return newint(-1);
            if (n > 1) {
                if ((i = enum2int(opdstr(1), enum_flag, "argument 2")) < 0)
                    return newint(0);
                tfile->autoflush = i;
            } else {
                tfflush(tfile);
            }
            return newint(1);

        case FN_ASCII:
            return newint((0x100 + unmapchar(*opdstr(1))) & 0xFF);

        case FN_CHAR:
            c = mapchar(localize(opdint(1)));
            return newstr(&c, 1);

        case FN_KEYCODE:
            str = opdstr(1);
            ptr = get_keycode(str);
            if (ptr) return newstr(ptr, -1);
            eprintf("unknown key name \"%s\"", str);
            return newstr("", 0);

        case FN_MOD:
            if (opdint(1) == 0) {
                eprintf("division by zero");
                return NULL;
            }
            return newint(opdint(2) % opdint(1));

        case FN_MORESIZE:
            return newint(moresize);

        case FN_MORESCROLL:
            return newint(clear_more(opdint(1)));

#ifndef NO_FLOAT
        case FN_SQRT:
            return newfloat(sqrt(opdfloat(1)));

        case FN_SIN:
            return newfloat(sin(opdfloat(1)));

        case FN_COS:
            return newfloat(cos(opdfloat(1)));

        case FN_TAN:
            return newfloat(tan(opdfloat(1)));

        case FN_ASIN:
            return newfloat(asin(opdfloat(1)));

        case FN_ACOS:
            return newfloat(acos(opdfloat(1)));

        case FN_ATAN:
            return newfloat(atan(opdfloat(1)));

        case FN_EXP:
            return newfloat(exp(opdfloat(1)));

        case FN_LOG:
            return newfloat(log(opdfloat(1)));

        case FN_LOG10:
            return newfloat(log10(opdfloat(1)));

        case FN_POW:
            return newfloat(pow(opdfloat(2), opdfloat(1)));

        case FN_TRUNC:
            return newint((int)opdfloat(1));

        case FN_ABS:
            return (mathtype(opd(1)) == TYPE_INT) ?
                newint(abs(opdint(1))) : newfloat(fabs(opdfloat(1)));
#else
        case FN_ABS:
            return newint(abs(opdint(1)));
#endif /* NO_FLOAT */

        case FN_RAND:
            if (n == 0) return newint(RAND());
            i = (n==1) ? 0 : opdint(2);
            if (i < 0) i = 0;
            j = opdint(1) - (n==1);
            return newint((j > i) ? RRAND(i, j) : i);

        case FN_ISATTY:
            return newint(!no_tty);

        case FN_FTIME:
            Stringterm(scratch, 0);
            tftime(scratch, opdstr(2), opdint(1), 0);
            return newstr(scratch->s, scratch->len);

        case FN_TIME:
            return newint((int)time(NULL));

        case FN_IDLE:
            return newint((int)((n == 0) ?
                (time(NULL) - keyboard_time) :
                sockidle(opdstr(1), SOCK_RECV)));

        case FN_SIDLE:
            return newint((int)(sockidle(n>0 ? opdstr(1) : "", SOCK_SEND)));

        case FN_FILENAME:
            str = expand_filename(opdstr(1));
            return newstr(str, -1);

        case FN_FG_WORLD:
            return ((str=fgname())) ? newstr(str, -1) : newstr("", 0);

        case FN_WORLDINFO:
            ptr = n>=1 ? opdstr(1) : NULL;
            str = world_info(n>=2 ? opdstr(2) : NULL, ptr);
            if (!str) {
                str = "";
                eprintf("illegal field name '%s'", ptr);
            }
            return newstr(str, -1);

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
            if ((i = opdint(n - 1)) > len) {
                i = len;
            } else if (i < 0) {
                i = len + i;
                if (i < 0) i = 0;
            }
            if (n < 3 || (j = opdint(1)) > len - i) {
                j = len - i;
            } else if (j < 0) {
                j = len - i + j;
                if (j < 0) j = 0;
            }
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
            return newint(do_kbmatch(n>0 ? opdint(1) : keyboard_pos));

        case FN_KBWLEFT:
            return newint(do_kbword(n>0 ? opdint(1) : keyboard_pos, -1));

        case FN_KBWRIGHT:
            return newint(do_kbword(n>0 ? opdint(1) : keyboard_pos, 1));

        case FN_KBLEN:
            return newint(keybuf->len);

        case FN_GETOPTS:
            current_command = parent;
            if (!argtext) {
                eprintf("getopts can not be used in a macro called as a function.");
                return newint(0);
            }
            str = opdstr(n);

            if (n>1) {
                CONST char *init = opdstr(n-1);
                for (ptr = str; *ptr; ptr++) {
                    if (!is_alnum(*ptr)) {
                        eprintf("%s: invalid option specifier: %c",
                            functab[symbol].name, *ptr);
                        return newint(0);
                    }
                    Stringadd(Stringcpy(scratch, "opt_"), *ptr);
                    newlocalvar(scratch->s, init);
                    if (ptr[1] == ':' || ptr[1] == '#') ptr++;
                }
            }

            startopt(argtext, str);
            while (i = 1, (c = nextopt((char **)&ptr, &i))) {
                if (is_alpha(c)) {
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
            while (tf_argc > 0 && tf_argv[0].end <= ptr) {
                tf_argv++;
                tf_argc--;
            }
            if (tf_argc) {
                tf_argv[0].start = argtext = ptr;
            }
            return newint(1);

        case FN_READ:
            eprintf("warning: read() will be removed in the near future.  Use tfread() instead.");
            oldblock = block;  /* condition and evalflag are already correct */
            block = 0;
            j = !!tfgetS(scratch, tfin);
            block = oldblock;
            return !j ? newstr("", 0) : newstr(scratch->s, scratch->len);

        case FN_NREAD:
            return newint(read_depth);

        case FN_NACTIVE:
            return newint(nactive(n ? opdstr(1) : NULL));

        case FN_NLOG:
            return newint(log_count);

        case FN_NMAIL:
            return newint(mail_count);

        case FN_SYSTYPE:

#ifdef __CYGWIN32__
            return newstrliteral("cygwin32");
#else
# ifdef PLATFORM_UNIX
            return newstrliteral("unix");
# else
#  ifdef PLATFORM_OS2
            return newstrliteral("os/2");
#  else
            return newstrliteral("unknown");
#  endif
# endif
#endif

        default:
            eprintf("not supported");
            return NULL;
        }
}

static Value *do_function(n)
    int n;    /* number of operands (including function id) */
{
    Value *val;
    ExprFunc *funcrec;
    Macro *macro = NULL;
    Handler *handler = NULL;
    CONST char *id, *old_command;
    STATIC_BUFFER(scratch);

    if (opd(n)->type != TYPE_ID) {
        eprintf("function name must be an identifier.");
        return NULL;
    }
    id = opd(n--)->u.sval;

    funcrec = (ExprFunc *)binsearch((GENERIC*)id, (GENERIC*)functab,
        sizeof(functab)/sizeof(ExprFunc), sizeof(ExprFunc), strstructcmp);

    if (funcrec) {
        if (n < funcrec->min || n > funcrec->max) {
            eprintf("%s: incorrect number of arguments", id);
            return NULL;
        }
        old_command = current_command;
        current_command = id;
        errno = 0;
        val = function_switch(funcrec - functab, n, old_command);
#ifndef NO_FLOAT
        if (errno == EDOM) {
            eprintf("argument outside of domain");
            freeval(val);
            val = NULL;
        } else if (errno == ERANGE) {
            eprintf("result outside of range");
            freeval(val);
            val = NULL;
        }
#endif
        current_command = old_command;
        return val;

    } else if ((macro = find_macro(id)) || (handler = find_command(id))) {
        int saved_argtop, saved_argc;
        Arg *saved_argv;

        if (handler == handle_exit_command ||
            handler == handle_result_command ||
            handler == handle_return_command)
        {
            eprintf("%s: not a function", id);
            return NULL;
        }

        if (handler && n > 1) {
            eprintf("%s: command called as function must have 0 or 1 argument",
                id);
            return NULL;
        }

        if (macro) {
            static int warned = 0;
            int i;

            if (!warned && !invis_flag && n == 1 && strchr(opdstr(1), ' ')) {
                eprintf("%s: warning: argument passing semantics for macros called as functions has changed in 4.0.  See '/help functions'.", id);
                warned = 1;
            }

            /* pass parameters by value, not by [pseudo]reference */
            for (i = 1; i <= n; i++) {
                val = stack[stacktop-i];
                if (val->type == TYPE_ID) {
                    stack[stacktop-i] = newstr(valstr(val), -1);
                    freeval(val);
                }
            }

            saved_argtop = argtop;
            saved_argc = tf_argc;
            saved_argv = tf_argv;
            argtop = stacktop;
            tf_argc = n;
            tf_argv = NULL;

            if (!do_macro(macro, NULL))
                set_user_result(NULL);

            argtop = saved_argtop;
            tf_argc = saved_argc;
            tf_argv = saved_argv;
        } else if (handler) {
            set_user_result((*handler)
                (Stringcpy(scratch, n ? opdstr(1) : "")->s));
        }

        return_user_result();
    }

    eprintf("%s: no such function", id);
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
        condition = evalflag && opdbool(1);

        while (is_space(*++ip));
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
    int i, oldcondition = condition;
    if (!and_expr()) return 0;
    condition = evalflag && oldcondition && !breaking;
    i = 0;
    while (*ip == '|') {
        ip++;
        i = i || opdbool(1);
        condition = condition && !i;
        if (!i) freeval(stack[--stacktop]);  /* discard first value */
        if (!and_expr()) return 0;
        if (i) freeval(stack[--stacktop]);   /* discard second value */
    }
    condition = oldcondition;
    return 1;
}

static int and_expr()
{
    /* This is more like flow-control than expression, so we handle the stack
     * here instead of calling reduce().
     */
    int i, oldcondition = condition;
    if (!relational_expr()) return 0;
    condition = evalflag && condition && !breaking;
    i = 1;
    while (*ip == '&') {
        ip++;
        i = i && opdbool(1);
        condition = condition && i;
        if (i) freeval(stack[--stacktop]);   /* discard first value */
        if (!relational_expr()) return 0;
        if (!i) freeval(stack[--stacktop]);  /* discard second value */
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
            else {
                if (pedantic) eprintf("suggestion: use == instead of =");
                op = '=';
            }
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

    while (is_space(*ip)) ip++;

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
            for (++ip; is_space(*ip); ip++);
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
            for (++ip; is_space(*ip); ip++);
            if (!reduce(OP_FUNC, n)) return 0;
        }
        return 1;
    }
}

static int primary_expr()
{
    CONST char *end;
    STATIC_BUFFER(static_buffer);
    int result;
    double d;
    Stringp buffer;  /* gotta be reentrant */

    while (is_space(*ip)) ip++;
    if (is_digit(*ip)) {
#ifndef NO_FLOAT
        for (end = ip+1; is_digit(*end); end++);
        if (*end == '.' || ucase(*end) == 'E') {
            d = strtod(ip, (char**)&ip);
            if (d == HUGE_VAL || d == -HUGE_VAL) {
                eprintf("float literal: arithmetic overflow");
                return 0;
            }
            if (!pushval(newfloat(d))) return 0;
        } else
#endif /* NO_FLOAT */
        {
            if (!pushval(newint(parsetime((char **)&ip, NULL)))) return 0;
        }
#ifndef NO_FLOAT
    } else if (ip[0] == '.' && is_digit(ip[1])) {
        if (!pushval(newfloat(strtod(ip, (char**)&ip)))) return 0;
#endif /* NO_FLOAT */
    } else if (is_quote(*ip)) {
        if (!stringliteral(static_buffer, (char**)&ip)) {
            eprintf("%S in string literal", static_buffer);
            return 0;
        }
        if (!pushval(newstr(static_buffer->s, static_buffer->len))) return 0;
    } else if (is_alpha(*ip) || *ip == '_') {
        for (end = ip + 1; is_alnum(*end) || *end == '_'; end++);
        if (!pushval(newid(ip, end - ip))) return 0;
        ip = end;
    } else if (*ip == '$') {
        static int warned = 0;
        ++ip;
        Stringinit(buffer);
        if ((!warned || pedantic) && *ip == '[') {
            eprintf("warning: $[...] substitution in expression is legal, but redundant.  Try using (...) instead.");
            warned = 1;
        }
        result = dollarsub(buffer) && pushval(newstr(buffer->s, buffer->len));
        Stringfree(buffer);
        if (!result) return 0;
    } else if (*ip == '{') {
        if (!varsub(NULL, 0)) return 0;
    } else if (*ip == '%') {
        ++ip;
        if (!varsub(NULL, 1)) return 0;
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
    
    while (is_space(*ip)) ip++;
    return 1;
}


#ifdef DMALLOC
void free_expr()
{
    Value *val;
    while (valpool) {
       val = valpool;
       valpool = valpool->u.next;
       FREE(val);
    }
}
#endif

