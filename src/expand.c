/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expand.c,v 35004.119 1999/01/31 00:27:41 hawkeye Exp $ */


/********************************************************************
 * Fugue macro text interpreter
 *
 * Written by Ken Keys
 * Interprets macro statements, performing substitutions for positional
 * parameters, variables, macro bodies, and expressions.
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
#include "socket.h"	/* send_line() */
#include "search.h"
#include "keyboard.h"	/* pending_line, handle_input_line() */
#include "parse.h"
#include "expand.h"
#include "expr.h"
#include "commands.h"
#include "command.h"
#include "variable.h"


Value *user_result = NULL;		/* result of last user command */
int recur_count = 0;			/* expansion nesting count */
CONST char *current_command = NULL;
int breaking = 0;			/* number of levels to /break */
Arg *tf_argv = NULL;			/* shifted argument vector */
int tf_argc = 0;			/* shifted argument count */
int argtop = 0;				/* top of function argument stack */
CONST char *argtext = NULL;		/* shifted argument text */
int block = 0;				/* type of current block */
int condition = 1;			/* checked by /if and /while */
int evalflag = 1;			/* flag: should we evaluate? */
CONST char *ip;				/* instruction pointer */

static Value *default_result = NULL;	/* result of empty list */
static int cmdsub_count = 0;		/* cmdsub nesting count */

static CONST char *keyword_table[] = {
    "BREAK", "DO", "DONE", "ELSE", "ELSEIF", "ENDIF",
    "EXIT", "IF", "RETURN", "THEN", "WHILE"
};

static int    NDECL(keyword_parse);
static int    FDECL(list,(Stringp dest, int subs));
static int    FDECL(statement,(Stringp dest, int subs));
static void   FDECL(conditional_add,(Stringp s, int c));
static int    FDECL(slashsub,(Stringp dest));
static int    FDECL(backsub,(Stringp dest));
static CONST char *NDECL(error_text);

#define is_end_of_statement(p) ((p)[0] == '%' && is_statend((p)[1]))
#define is_end_of_cmdsub(p) (cmdsub_count && *(p) == ')')



void init_expand()
{
    default_result = newint(1);
}


struct Value *handle_eval_command(args)
    char *args;
{
    int c, subflag = SUB_MACRO;

    startopt(args, "s:");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
        case 's':
            if ((subflag = enum2int(args, enum_sub, "-s")) < 0)
                return newint(0);
            break;
        default:
            return newint(0);
        }
    }
    if (!process_macro(args, NULL, subflag, "\bEVAL")) return NULL;
    return_user_result();
}


struct Value *handle_test_command(args)
    char *args;
{
    struct Value *result;

    result = expr_value(args);
    if (result && result->type == TYPE_ID) {
        Value *temp = result;
        result = newstr(valstr(temp), -1);
        freeval(temp);
    }
    return result;
}

struct Value *handle_return_command(args)
    char *args;
{
    struct Value *result;

    if (cmdsub_count) {
        eprintf("may be called only directly from a macro, not in $() command substitution.");
        return NULL;
    }
    result = *args ? handle_test_command(args) : NULL;
    breaking = -1;
    return result;
}

struct Value *handle_result_command(args)
    char *args;
{
    struct Value *result;

    result = handle_return_command(args);
    if (!argtop) oputs(valstr(result));
    return result;
}

/* handle_command
 * Execute a single command line that has already been expanded.
 * note: cmd_line will be written into.
 */
int handle_command(cmd_line)
    String *cmd_line;
{
    CONST char *old_command;
    Handler *handler = NULL;
    Macro *macro = NULL;
    int error = 0, truth = 1;
    char *str, *end;

    str = cmd_line->s + 1;
    if (!*str || is_space(*str)) return 0;
    old_command = current_command;
    current_command = str;
    while (*str && !is_space(*str)) str++;
    if (*str) *str++ = '\0';
    while (is_space(*str)) str++;
    for (end = cmd_line->s + cmd_line->len - 1; is_space(*end); end--);
    *++end = '\0';
    while (*current_command == '!') {
        truth = !truth;
        current_command++;
    }
    if (*current_command == '@') {
        if (!(handler = find_command(current_command+1))) {
            eprintf("not a builtin command");
            error++;
        }
    } else if (!(macro = find_macro(current_command)) &&
        !(handler = find_command(current_command)))
    {
        CONST char *name = current_command;
        current_command = old_command;  /* for eprefix() */
        do_hook(H_NOMACRO, "!%s: no such command or macro", "%s", name);
        error++;
    }

    if (macro) {
        error = !do_macro(macro, str);
    } else if (handler) {
        Value *result = ((*handler)(str));
        set_user_result(result);
    }

    current_command = NULL;
    if (pending_line && !read_depth)  /* "/dokey newline" and not in read() */
        error = !handle_input_line();
    current_command = old_command;
    if (!truth) {
        truth = !valbool(user_result);
        set_user_result(newint(truth));
    }
    return !error;
}

int process_macro(body, args, subs, name)
    CONST char *body, *args, *name;
    int subs;
{
    Stringp buffer;
    Arg *true_argv = NULL;		/* unshifted argument vector */
    int vecsize = 20, error = 0;
    int saved_cmdsub, saved_argc, saved_breaking, saved_argtop;
    Arg *saved_argv;
    CONST char *saved_ip, *saved_argtext, *saved_command;
    List scope[1];

    if (++recur_count > max_recur && max_recur) {
        eprintf("too many recursions");
        recur_count--;
        return 0;
    }
    saved_command = current_command;
    saved_cmdsub = cmdsub_count;
    saved_ip = ip;
    saved_argc = tf_argc;
    saved_argv = tf_argv;
    saved_argtext = argtext;
    saved_breaking = breaking;
    saved_argtop = argtop;

    current_command = name;
    ip = body;
    cmdsub_count = 0;

    newvarscope(scope);

    argtext = args;
    if (args) {
        tf_argc = 0;
        if (!(tf_argv = (Arg *)MALLOC(vecsize * sizeof(Arg)))) {
            eprintf("Not enough memory for argument vector");
            error = TRUE;
        }
        while (!error && *args) {
            if (tf_argc == vecsize)
                tf_argv =
                    (Arg*)XREALLOC((char*)tf_argv, sizeof(Arg)*(vecsize+=10));
            tf_argv[tf_argc].start =
                stringarg((char **)&args, &tf_argv[tf_argc].end);
            tf_argc++;
        }
        true_argv = tf_argv;
        argtop = 0;
    }

    if (!error) {
        Stringninit(buffer, 96);
        if (!list(buffer, subs)) set_user_result(NULL);
        Stringfree(buffer);
    }

    if (true_argv) FREE(true_argv);
    nukevarscope();

    cmdsub_count = saved_cmdsub;
    ip = saved_ip;
    tf_argc = saved_argc;
    tf_argv = saved_argv;
    argtext = saved_argtext;
    breaking = exiting ? -1 : saved_breaking;
    argtop = saved_argtop;
    current_command = saved_command;
    recur_count--;
    return !!user_result;
}

String *do_mprefix()
{
    STATIC_BUFFER(buffer);
    int i;

    Stringterm(buffer, 0);
    for (i = 0; i < recur_count + cmdsub_count; i++)
        Stringcat(buffer, mprefix ? mprefix : "");
    Stringadd(buffer, ' ');
    if (current_command) {
        if (*current_command == '\b') {
            Sprintf(buffer, SP_APPEND, "%s: ", current_command+1);
        } else {
            Sprintf(buffer, SP_APPEND, "/%s: ", current_command);
        }
    }
    return buffer;
}

#define keyword_mprefix() \
{ \
    if (mecho > invis_flag) \
       tfprintf(tferr, "%S/%s", do_mprefix(), keyword_table[block-BREAK]); \
}

static int list(dest, subs)
    Stringp dest;
    int subs;
{
    int oldcondition, oldevalflag, oldblock;
    int is_a_command, is_a_condition, is_special;
    int iterations = 0, failed = 0, result = 0;
    CONST char *blockstart = NULL, *exprstart, *stmtstart;
    static CONST char unexpect_msg[] = "unexpected /%s in %s block";
    TFILE *orig_tfin, *orig_tfout;    /* restored when list() is done */
    TFILE *local_tfin, *local_tfout;  /* restored after pipes within list() */
    TFILE *inpipe = NULL, *outpipe = NULL;

    local_tfin = orig_tfin = tfin;
    local_tfout = orig_tfout = tfout;

#define unexpected(innerblock, outerblock) \
    eprintf(unexpect_msg, keyword_table[innerblock - BREAK], \
        outerblock ? keyword_table[outerblock - BREAK] : "outer");

    /* Do NOT strip leading space here.  This allows user to type and send
     * lines with leading spaces (but completely empty lines are handled
     * by handle_input_line()).  During expansion, spaces AFTER a "%;"
     * or keyword will be skipped.
     */

    if (!*ip || is_end_of_cmdsub(ip)) {   /* empty list returns 1 */
        default_result->count++;
        set_user_result(default_result);
    }

    if (block == WHILE) blockstart = ip;

    do /* while (*ip) */ {
        if (breaking && !block) {
            if (breaking > 0) breaking--;
            result = 1;  goto list_exit;
        }
#if 1
        if (subs >= SUB_NEWLINE)
            while (is_space(*ip) || (ip[0] == '\\' && is_space(ip[1])))
                ++ip;
#endif

        is_special = is_a_command = is_a_condition = FALSE;
        if (interrupted()) {
            eprintf("macro evaluation interrupted.");
            goto list_exit;
        }
        Stringterm(dest, 0);
        /* Lines begining with one "/" are tf commands.  Lines beginning
         * with multiple "/"s have the first removed, and are sent to server.
         */

        if ((subs > SUB_LITERAL) && (*ip == '/') && (*++ip != '/')) {
            is_a_command = TRUE;
            oldblock = block;
            if (subs >= SUB_KEYWORD) {
                stmtstart = ip;
                is_special = block = keyword_parse();
                if (subs == SUB_KEYWORD)
                    subs += (block != 0);
            }

        } else if ((subs > SUB_LITERAL) &&
            (block == IF || block == ELSEIF || block == WHILE))
        {
            if (*ip == '(') {
                is_a_condition = TRUE;
                oldblock = block;
                exprstart = ip;
                ip++; /* skip '(' */
                if (!expr()) goto list_exit;
                if (stack[--stacktop])
                    set_user_result(stack[stacktop]);
                if (*ip != ')') {
                    parse_error("condition", "operator or ')'");
                    goto list_exit;
                }
                if (!breaking && evalflag && condition && mecho > invis_flag) {
                    tfprintf(tferr, "%S%.*s",
                        do_mprefix(), ip - exprstart + 1, exprstart);
                }
                while(is_space(*++ip)); /* skip ')' and spaces */
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
                if (!breaking && evalflag && condition) keyword_mprefix();
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
                if (!breaking && evalflag && condition) keyword_mprefix();
                block = oldblock;
                if (failed) goto list_exit;
                break;
            case DO:
                if (oldblock != WHILE) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
                evalflag = evalflag && condition;
                condition = valbool(user_result);
                if (breaking > 0) breaking++;
                if (!breaking && evalflag && condition) keyword_mprefix();
                continue;
            case BREAK:
                if (!breaking && evalflag && condition) {
                    keyword_mprefix();
                    if ((breaking = atoi(ip)) <= 0) breaking = 1;
                }
                block = oldblock;
                continue;
            case DONE:
                if (oldblock != DO) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
                if (breaking || !condition || !evalflag) {
                    if (breaking > 0) breaking--;
                    evalflag = 0;  /* don't eval any trailing garbage */
                    while (is_space(*ip)) ip++;
                    result = 1;  goto list_exit;
                } else if (++iterations > max_iter && max_iter) {
                    eprintf("too many iterations");
                    block = oldblock;
                    goto list_exit;
                } else {
                    ip = blockstart;
                    block = WHILE;
                    continue;
                }
            case IF:
                oldevalflag = evalflag;
                oldcondition = condition;
                if (!breaking && evalflag && condition) keyword_mprefix();
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
                if (!breaking && evalflag && condition) keyword_mprefix();
                block = oldblock;
                if (failed) goto list_exit;
                break;
            case THEN:
                if (oldblock != IF && oldblock != ELSEIF) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
                evalflag = evalflag && condition;
                if (!breaking && evalflag) {
                    condition = valbool(user_result);
                    if (condition) keyword_mprefix();
                }
                continue;
            case ELSEIF:
            case ELSE:
                if (oldblock != THEN) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
                condition = !condition;
                if (!breaking && evalflag && condition) keyword_mprefix();
                continue;
            case ENDIF:
                if (oldblock != THEN && oldblock != ELSE) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
                while (is_space(*ip)) ip++;
                result = 1;  goto list_exit;
            default:
                /* not a control statement */
                ip = stmtstart - 1;
                is_special = 0;
                block = oldblock;
                if (!statement(dest, subs)) goto list_exit;
                break;
            }

        } else /* !(is_a_command || is_a_condition) */ {
            if (inpipe) {
                eprintf("Piping input to a server command is not allowed.");
                goto list_exit;
            }
            if (!statement(dest, subs))
                goto list_exit;
        }

        if (ip[0] == '%' && ip[1] == '|') { /* this stmnt pipes into next one */
            if (!is_a_command) {
                eprintf("Piping output of a server command is not allowed.");
                goto list_exit;
            } else if (is_special) {
                eprintf("Piping output of a special command is not allowed.");
                goto list_exit;
            }
            tfout = outpipe = tfopen(NULL, "q");
        }

        if (!is_special && !breaking && evalflag && condition &&
            (dest->len || !snarf))
        {
            if ((subs == SUB_MACRO || is_a_command) && (mecho > invis_flag))
                tfprintf(tferr, "%S%s%S", do_mprefix(),
                    is_a_command ? "" : "SEND: ", dest);
            if (is_a_command) {
                handle_command(dest);  /* sets user_result */
            } else {
                if (!do_hook(H_SEND, NULL, "%S", dest)) {
                   set_user_result(newint(send_line(dest->s, dest->len, TRUE)));
                }
            }
        }

        /* save changes to tfin and tfout iff they weren't pipes */
        if (!outpipe) local_tfout = tfout;
        if (!inpipe) local_tfin = tfin;

        if (inpipe) tfclose(inpipe);  /* previous stmnt piped into this one */

        /* set up i/o for next command */
        inpipe = outpipe;
        tfin = inpipe ? inpipe : local_tfin;
        outpipe = NULL;
        tfout = local_tfout;

        if (is_end_of_cmdsub(ip)) {
            break;
        } else if (is_end_of_statement(ip)) {
            ip += 2;
            while (is_space(*ip)) ip++;
        } else if (*ip) {
            parse_error("macro", "end of statement");
            goto list_exit;
        }

    } while (*ip);

    result = 1;

    if (inpipe) {
        eprintf("'%|' must be followed by another command.");
        result = 0;
    }

list_exit:
    tfin = orig_tfin;
    tfout = orig_tfout;
    if (inpipe) tfclose(inpipe);
    return result;
}

CONST char **keyword(id)
    CONST char *id;
{
    return (CONST char **)binsearch((GENERIC*)id, (GENERIC*)keyword_table,
        sizeof(keyword_table)/sizeof(char*), sizeof(char*), cstrstructcmp);
}

static int keyword_parse()
{
    CONST char **result, *end;
    char buf[sizeof("elseif")];  /* "elseif" is the longest keyword. */

    if (!is_keystart(*ip)) return 0;          /* fast heuristic */

    end = ip + 1;
    while (*end && !is_space(*end) && *end != '%' && *end != ')')
        end++;
    if (end - ip >= sizeof(buf)) return 0;    /* too long, can't be keyword */

    strncpy(buf, ip, end - ip);
    buf[end - ip] = '\0';
    if (!(result = keyword(buf)))
        return 0;
    for (ip = end; is_space(*ip); ip++);
    return BREAK + (result - keyword_table);
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
            if (is_end_of_statement(ip)) {
                while (dest->len && is_space(dest->s[dest->len-1]))
                    Stringterm(dest, dest->len-1);  /* nuke spaces before %; */
                break;
            }
            ++ip;
            if (*ip == '%') {
                while (*ip == '%') Stringadd(dest, *ip++);
            } else if (subs >= SUB_FULL) {
                if (!varsub(dest, 0)) return 0;
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
            while (is_space(*ip))
                ip++;
#endif
        } else {
            /* is_statmeta() is much faster than all those if statements. */
            for (start = ip++; *ip && !is_statmeta(*ip); ip++);
            Stringfncat(dest, start, ip - start);
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
        if (is_alnum(*ip) || is_quote(*ip) || *ip == '/') {
            while (is_alnum(*end)) end++;
        }
        Sprintf(buf, 0, "'%.*s'", end - ip, ip);
        return buf->s;
    } else {
        return "end of body";
    }
}

void parse_error(type, expect)
    CONST char *type, *expect;
{
    eprintf("%s syntax error: expected %s, found %s.",
        type, expect, error_text());
}


int exprsub(dest)
    Stringp dest;
{
    int result = 0;
    Value *val;

    ip++; /* skip '[' */
    while (is_space(*ip)) ip++;
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


int cmdsub(dest)
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
            Stringfncat(dest, aline->str, aline->len);
        }
        free_aline(aline);
    }

    tfclose(file);
    ip++;
    return result;
}

int macsub(dest)
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
        } else if (!bracket && is_space(*ip)) {
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
            if (!varsub(buffer, 0)) return 0;
        } else {
            for (s = ip++; *ip && !is_punct(*ip) && !is_space(*ip); ip++);
            Stringfncat(buffer, s, ip - s);
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
    if (mecho > invis_flag)
        tfprintf(tferr, "%S$%S --> %s", do_mprefix(), buffer, body);
    return 1;
}

static int backsub(dest)
    Stringp dest;
{
    if (is_digit(*ip)) {
        char c = strtochr(&ip);
        conditional_add(dest, mapchar(c));
    } else if (!backslash) {
        conditional_add(dest, '\\');
    } else if (*ip) {
        conditional_add(dest, *ip++);
    }
    return 1;
}

int varsub(dest, sub_warn)
    String *dest;	/* if NULL, string value will be pushed onto stack */
    int sub_warn;
{
    CONST char *value, *start, *contents;
    int bracket, except, ell = FALSE, pee = FALSE, star = FALSE, n = -1;
    int first, last, empty = 0;
    STATIC_BUFFER(selector);
    Stringp buffer;	/* used when dest==NULL and a Stringp is needed */
    int stackflag;
    Value *val = NULL;
    int result = 0;
    static int sub_warned = 0;

    if ((stackflag = !dest)) {
        Stringzero(buffer);
        dest = buffer;
    }

    if (*ip == '%') {
        while (*ip == '%') conditional_add(dest, *ip++);
        result = 1;
        goto varsub_exit;
    }
    if (!*ip || is_space(*ip)) {
        conditional_add(dest, '%');
        result = 1;
        goto varsub_exit;
    }

    contents = ip;
    if ((bracket = (*ip == '{'))) ip++;

    if (ip[0] == '#' && (!bracket || ip[1] == '}')) {
        ++ip;
        if (!breaking && evalflag && condition) {
            Sprintf(dest, SP_APPEND, "%d", tf_argc);
        }
        empty = FALSE;
    } else if (ip[0] == '?' && (!bracket || ip[1] == '}')) {
        ++ip;
        if (!breaking && evalflag && condition) {
            Stringcat(dest, valstr(user_result));
        }
        empty = FALSE;

    } else {
        if ((except = (*ip == '-'))) {
            ++ip;
        }
        start = ip;
        if ((ell = (ucase(*ip) == 'L')) || (pee = (ucase(*ip) == 'P')) ||
            (star = (*ip == '*')))
        {
            ++ip;
        }
        if (!star && is_digit(*ip)) {
            n = strtoint(&ip);
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
        Stringterm(selector, 0);
        if ((n < 0 && !star) || (bracket && (ell || pee))) {
            /* is non-special, or could be non-special if followed by alnum_ */
            if (is_alnum(*ip) || (*ip == '_')) {
                ell = pee = FALSE;
                n = -1;
                do ip++; while (is_alnum(*ip) || *ip == '_');
                Stringncpy(selector, start, ip - start);
            }
        }

        if (!breaking && evalflag && condition) {

            if (pee) {
                if (n < 0) n = 1;
                empty = (regsubstr(dest, n) <= 0);
                n = -1;
            } else if (ell) {
                if (n < 0) n = 1;
                if (except) first = 0, last = tf_argc - n - 1;
                else first = last = tf_argc - n;
            } else if (star) {
                first = 0, last = tf_argc - 1;
            } else if (n == 0) {
                static int zero_warned = 0;
                if (!zero_warned && !invis_flag) {
                    eprintf("warning: as of version 4.0, '%%0' is no longer the same as '%%*'.");
                    zero_warned = 1;
                }
                if (current_command && *current_command != '\b')
                    Stringcat(dest, current_command);
            } else if (n > 0) {
                if (except) first = n, last = tf_argc - 1;
                else first = last = n - 1;
            } else if (cstrcmp(selector->s, "R") == 0) {
                if (tf_argc > 0) {
                    n = 1;
                    first = last = RRAND(0, tf_argc-1);
                } else empty = TRUE;
            } else if (cstrcmp(selector->s, "PL") == 0) {
                empty = (regsubstr(dest, -1) <= 0);
            } else if (cstrcmp(selector->s, "PR") == 0) {
                empty = (regsubstr(dest, -2) <= 0);
            } else {
                value = getnearestvar(selector->s, NULL);
                if (!(empty = !value || !*value))
                    Stringcat(dest, value);
            }

            if (star || n > 0) {
                empty = (first > last || first < 0 || last >= tf_argc);
                if (!empty) {
                    if (argtop) {
                        if (stackflag && first == last) {
                            (val = stack[argtop-tf_argc+first])->count++;
                        } else {
                            int i;
                            for (i = first; ; i++) {
                                Stringcat(dest, valstr(stack[argtop-tf_argc+i]));
                                if (i == last) break;
                                Stringadd(dest, ' ');
                            }
                        }
                    } else {

                        if (stackflag) {
                            val = newstr(tf_argv[first].start,
                                tf_argv[last].end - tf_argv[first].start);
                        } else {
                            Stringfncat(dest, tf_argv[first].start,
                                tf_argv[last].end - tf_argv[first].start);
                        }
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
            } else if (bracket && *ip == '}') {
                break;
            } else if (!bracket && is_space(*ip)) {
                break;
            } else if (*ip == '%') {
                ++ip;
                if (!varsub(dest, 0)) goto varsub_exit;
            } else if (*ip == '$') {
                ++ip;
                if (!dollarsub(dest)) goto varsub_exit;
            } else if (*ip == '/') {
                ++ip;
                if (!slashsub(dest)) goto varsub_exit;
            } else if (*ip == '\\') {
                ++ip;
                if (!backsub(dest)) goto varsub_exit;
            } else {
                for (start = ip++; *ip && is_alnum(*ip); ip++);
                if (!breaking && evalflag && condition)
                    Stringfncat(dest, start, ip - start);
            }
        }

        evalflag = oldevalflag;
    }

    if (bracket) {
        if (*ip != '}') {
            if (!*ip) eprintf("unmatched %%{ or bad substitution");
            else eprintf("unmatched %%{ or illegal character '%c'", *ip);
            goto varsub_exit;
        } else ip++;
    }

    result = 1;
    if (sub_warn & (!sub_warned || pedantic)) {
        sub_warned = 1;
        eprintf("warning: \"%%%.*s\" %s  Try using \"{%.*s}\" instead.",
            ip-contents, contents,
            "substitution in expression is legal, but can be confusing.",
            (ip-bracket)-(contents+bracket), contents+bracket);
    }
varsub_exit:
    if (stackflag) {
        if (result) {
            pushval(val ? val : newstr(dest->s ? dest->s : "", dest->len));
        } else {
            if (val) freeval(val);
        }
        Stringfree(dest);
    }
    return result;
}

static void conditional_add(s, c)
    Stringp s;
    int c;
{
    if (!breaking && evalflag && condition)
        Stringadd(s, c);
}

struct Value *handle_shift_command(args)
    char *args;
{
    int count;
    int error;

    count = (*args) ? atoi(args) : 1;
    if (count < 0) return newint(0);
    if ((error = (count > tf_argc))) count = tf_argc;
    tf_argc -= count;
    if (tf_argv) {  /* true if macro was called as command, not as function */
        tf_argv += count;
        if (tf_argc) argtext = tf_argv[0].start;
    }
    return newint(!error);
}

#ifdef DMALLOC
void free_expand()
{
    freeval(user_result);
    freeval(default_result);
}
#endif

