/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expand.c,v 35004.91 1997/12/14 09:07:56 hawkeye Exp $ */


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
int breaking = 0;			/* flag: are we /break'ing? */
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
    "BREAK", "DO", "DONE", "ELSE", "ELSEIF", "ENDIF", "IF", "THEN", "WHILE"
};

static int    NDECL(keyword);
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


struct Value *handle_test_command(args)
    char *args;
{
    CONST char *saved_ip = ip;
    struct Value *result = NULL;

    ip = args;
    if (expr()) {
        if (*ip) {
            parse_error("expression", "operand");
            result = NULL;
        } else {
            stacktop--;
            if (stack[stacktop]->type == TYPE_ID) {
                result = newstr(valstr(stack[stacktop]), -1);
                freeval(stack[stacktop]);
            } else {
                result = stack[stacktop];
            }
        }
    }
    ip = saved_ip;
    return result;
}

struct Value *handle_return_command(args)
    char *args;
{
    struct Value *result;

    result = *args ? handle_test_command(args) : NULL;
    breaking = -1;
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
    if (!*str || isspace(*str)) return 0;
    old_command = current_command;
    current_command = str;
    while (*str && !isspace(*str)) str++;
    if (*str) *str++ = '\0';
    while (isspace(*str)) str++;
    for (end = cmd_line->s + cmd_line->len - 1; isspace(*end); end--);
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
        do_hook(H_NOMACRO, "%% %s: no such command or macro", "%s",
            current_command);
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

int process_macro(body, args, subs)
    CONST char *body, *args;
    int subs;
{
    Stringp buffer;
    Arg *true_argv = NULL;		/* unshifted argument vector */
    int vecsize = 20, error = 0;
    int saved_cmdsub, saved_argc, saved_breaking, saved_argtop;
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
    saved_argc = tf_argc;
    saved_argv = tf_argv;
    saved_argtext = argtext;
    saved_breaking = breaking;
    saved_argtop = argtop;

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
    recur_count--;
    return !!user_result;
}

String *do_mprefix()
{
    STATIC_BUFFER(buffer);
    int i;

    Stringterm(buffer, 0);
    for (i = 0; i < recur_count + cmdsub_count; i++)
        Stringcat(buffer, mprefix);
    Stringadd(buffer, ' ');
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
    CONST char *blockstart = NULL, *exprstart;
    static CONST char unexpect_msg[] = "unexpected /%s in %s block";
    TFILE *orig_tfin = tfin, *orig_tfout = tfout;
    TFILE *inpipe = NULL, *outpipe = NULL;
    STATIC_BUFFER(scratch);

#define unexpected(innerblock, outerblock) \
    eprintf(unexpect_msg, keyword_table[innerblock - BREAK], \
        outerblock ? keyword_table[outerblock - BREAK] : "outer");

    /* Do NOT strip leading space here.  This allows user to type and send
     * lines with leading spaces (but completely empty lines are handled
     * by handle_input_line()).  During expansion, spaces AFTER a "%;"
     * or keyword will be skipped.
     */

    if (!*ip || is_end_of_cmdsub(ip))
        copy_user_result(default_result);  /* empty list returns 1 */

    if (block == WHILE) blockstart = ip;

    do /* while (*ip) */ {
        if (breaking && !block) {
            breaking--;
            result = 1;  goto list_exit;
        }
#if 1
        if (subs >= SUB_NEWLINE)
            while (isspace(*ip) || (ip[0] == '\\' && isspace(ip[1])))
                ++ip;
#endif

        is_special = is_a_command = is_a_condition = FALSE;
        if (interrupted()) {
            eprintf("%% macro evaluation interrupted.");
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
                is_special = block = keyword();
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
                    copy_user_result(stack[stacktop]);
                freeval(stack[stacktop]);
                if (*ip != ')') {
                    parse_error("condition", "operator or ')'");
                    goto list_exit;
                }
                if (!breaking && evalflag && condition && mecho > invis_flag) {
                    SStringcpy(scratch, do_mprefix());
                    Stringncat(scratch, exprstart, ip - exprstart + 1);
                    tfputs(scratch->s, tferr);
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
                if (breaking) breaking++;
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
                    if (breaking) breaking--;
                    evalflag = 0;  /* don't eval any trailing garbage */
                    while (isspace(*ip)) ip++;
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
                condition = valbool(user_result);
                if (!breaking && evalflag && condition) keyword_mprefix();
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
                while (isspace(*ip)) ip++;
                result = 1;  goto list_exit;
            default:
                /* not a control statement */
                ip--;
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
                eprintf("Piping output of a compound command is not allowed.");
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

        if (inpipe) tfclose(inpipe);  /* previous stmnt piped into this one */
        inpipe = outpipe;
        tfin = inpipe ? inpipe : orig_tfin;
        outpipe = NULL;
        tfout = orig_tfout;

        if (is_end_of_cmdsub(ip)) {
            break;
        } else if (is_end_of_statement(ip)) {
            ip += 2;
            while (isspace(*ip)) ip++;
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
                while (dest->len && isspace(dest->s[dest->len-1]))
                    Stringterm(dest, dest->len-1);  /* nuke spaces before %; */
                break;
            }
            ++ip;
            if (*ip == '%') {
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
        if (isalnum(*ip) || is_quote(*ip) || *ip == '/') {
            while (isalnum(*end)) end++;
        }
        Stringcpy(buf, "'");
        Stringfncat(buf, ip, end - ip);
        Stringcat(buf, "'");
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

int varsub(dest)
    String *dest;	/* if NULL, string result will be pushed onto stack */
{
    CONST char *value, *start;
    int bracket, except, ell = FALSE, pee = FALSE, star = FALSE, n = -1;
    int first, last, empty = 0;
    STATIC_BUFFER(selector);
    Stringp buffer;	/* used when dest==NULL and a Stringp is needed */
    int stackflag;
    Value *val = NULL;
    int result = 0;

    if ((stackflag = !dest)) {
        Stringzero(buffer);
        dest = buffer;
    }

    if (*ip == '%') {
        while (*ip == '%') conditional_add(dest, *ip++);
        result = 1;
        goto varsub_exit;
    }
    if (!*ip || isspace(*ip)) {
        conditional_add(dest, '%');
        result = 1;
        goto varsub_exit;
    }

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
        if (!star && isdigit(*ip)) {
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
                n = -1;
            } else if (ell) {
                if (n < 0) n = 1;
                if (except) first = 0, last = tf_argc - n - 1;
                else first = last = tf_argc - n;
            } else if (star) {
                first = 0, last = tf_argc - 1;
            } else if (n == 0) {
                static int warned = 0;
                if (!warned && !invis_flag) {
                    eprintf("warning: as of version 4.0, '%%0' is no longer the same as '%%*'.");
                    warned = 1;
                }
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
            } else if (!bracket && isspace(*ip)) {
                break;
            } else if (*ip == '%') {
                ++ip;
                if (!varsub(dest)) goto varsub_exit;
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
                for (start = ip++; *ip && isalnum(*ip); ip++);
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

