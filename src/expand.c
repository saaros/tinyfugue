/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: expand.c,v 35004.177 2003/05/27 02:08:19 hawkeye Exp $";


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
#include "tf.h"
#include "util.h"
#include "search.h"
#include "tfio.h"
#include "macro.h"
#include "signals.h"	/* interrupted() */
#include "socket.h"	/* send_line() */
#include "keyboard.h"	/* pending_line, handle_input_line() */
#include "parse.h"
#include "expand.h"
#include "expr.h"
#include "commands.h"
#include "command.h"
#include "variable.h"


Value *user_result = NULL;		/* result of last user command */
int recur_count = 0;			/* expansion nesting count */
const char *current_command = NULL;
char current_opt = '\0';
int breaking = 0;			/* number of levels to /break */
String *argstring = NULL;		/* command argument string */
Arg *tf_argv = NULL;			/* shifted command argument vector */
int tf_argc = 0;			/* shifted command/function arg count */
int argtop = 0;				/* top of function argument stack */
keyword_id_t block = 0;			/* type of current block */
Value *val_zero = NULL;
Value *val_one = NULL;
const char *oplabel[256];

static int cmdsub_count = 0;		/* cmdsub nesting count */

static const char *keyword_table[] = {
    "BREAK", "DO", "DONE", "ELSE", "ELSEIF", "ENDIF",
    "EXIT", "IF", "RESULT", "RETURN", "THEN", "WHILE"
};
#define KEYWORD_LENGTH	6	/* length of longest keyword */

static keyword_id_t keyword_parse(Program *prog);
static int list(Program *prog, int subs);
static int statement(Program *prog, int subs);
static int slashsub(Program *prog, String *dest);
static int backsub(Program *prog, String *dest);
static const char *error_text(Program *prog);
static int macsub(Program *prog, int in_expr);
static int cmdsub(Program *prog, int in_expr);
static int percentsub(Program *prog, int subs, String **destp);

#define is_end_of_statement(p) ((p)[0] == '%' && is_statend((p)[1]))
#define is_end_of_cmdsub(p) (cmdsub_count && *(p) == ')')

void init_expand(void)
{
    val_zero = newint(0);
    val_one = newint(1);

#define defopcode(name, num, optype, argtype, resulttype) \
    oplabel[num] = #name;
#include "opcodes.h"
}

static void prog_free_tail(Program *prog, int start)
{
    int i;

    for (i = start; i < prog->len; i++) {
	switch (op_arg_type(prog->code[i].op)) {
	case OPA_STRP:
	    if (prog->code[i].arg.str)
		Stringfree(prog->code[i].arg.str);
	    break;
	case OPA_VALP:
	    if (prog->code[i].arg.val)
		freeval(prog->code[i].arg.val);
	    break;
	}
    }
    prog->len = start;
}

void prog_free(Program *prog)
{
    prog_free_tail(prog, 0);
    Stringfree(prog->src);
    if (prog->code) FREE(prog->code);
    FREE(prog);
}

#define Sprintfa1(buf, fmt, arg) Sprintf(buf, SP_APPEND, fmt, arg)

static void inst_dump(Program *prog, int i, char prefix)
{
    String *buf;
    opcode_t op;
    Value *val;
    String *str;
    BuiltinCmd *cmd;

    (buf = Stringnew(NULL, 0, 0))->links++;
    Sprintf(buf, 0, "%c%5d: ", prefix, i);
    op = prog->code[i].op;
    if (op >= 0x20 && op < 0x7F) {
	Sprintf(buf, SP_APPEND, "%c        %d", op, prog->code[i].arg.i);
    } else {
	if (!oplabel[opnum(op)]) {
	    Sprintfa1(buf, "0x%04X   ", op);
	} else if (op_type_is(op, CTRL) && (op & OPR_FALSE)) {
	    Sprintfa1(buf, "!%-7s ", oplabel[opnum(op)]);
	} else {
	    Sprintfa1(buf, "%-8s ", oplabel[opnum(op)]);
	}
	switch(op_arg_type(op)) {
	case OPA_INT:
	    Sprintfa1(buf, "%d", prog->code[i].arg.i);
	    break;
	case OPA_CHAR:
	    Sprintfa1(buf, "%c", prog->code[i].arg.c);
	    break;
	case OPA_STRP:
	    str = prog->code[i].arg.str;
	    Sprintfa1(buf, str ? "\"%S\"" : "NULL", str);
	    break;
	case OPA_CMDP:
	    cmd = prog->code[i].arg.cmd;
	    Sprintfa1(buf, "%s", cmd->name);
	    break;
	case OPA_VALP:
	    if (!(val = prog->code[i].arg.val)) {
		Stringcat(buf, "NULL");
		break;
	    }
	    switch (val->type & TYPES_BASIC) {
	    case TYPE_ID:    Sprintfa1(buf, "ID %s", val->name); break;
	    case TYPE_FUNC:  Sprintfa1(buf, "FUNC %s", val->name); break;
	    case TYPE_CMD:   Sprintfa1(buf, "CMD %s", val->name); break;
	    case TYPE_STR:   Sprintfa1(buf, "STR \"%S\"", valstr(val));
			     if (val->type & TYPE_REGEX)
				 Stringcat(buf, " (RE)");
			     if (val->type & TYPE_EXPR)
				 Stringcat(buf, " (EXPR)");
			     break;
	    case TYPE_ENUM:  Sprintfa1(buf, "ENUM \"%S\"", valstr(val)); break;
	    case TYPE_POS:   Sprintfa1(buf, "POS %S", valstr(val)); break;
	    case TYPE_INT:   Sprintfa1(buf, "INT %S", valstr(val)); break;
	    case TYPE_TIME:  Sprintfa1(buf, "TIME %S", valstr(val)); break;
	    case TYPE_FLOAT: Sprintfa1(buf, "FLOAT %S", valstr(val)); break;
	    default:         Sprintfa1(buf, "? %S", valstr(val)); break;
	    }
	    break;
	}
    }
    tfputline(buf, tferr);
    Stringfree(buf);
}

static void prog_dump(Program *prog)
{
    int i;

    for (i = 0; i < prog->len; i++)
	inst_dump(prog, i, 'p');
}


struct Value *handle_eval_command(String *args, int offset)
{
    int c, subflag = SUB_MACRO;
    char *ptr;

    startopt(args, "s:");
    while ((c = nextopt(&ptr, NULL, NULL, &offset))) {
        switch (c) {
        case 's':
            if ((subflag = enum2int(ptr, 0, enum_sub, "-s")) < 0)
                return shareval(val_zero);
            break;
        default:
            return shareval(val_zero);
        }
    }
    if (!macro_run(args, offset, NULL, 0, subflag, "\bEVAL"))
        return shareval(val_zero);
    return_user_result();
}


struct Value *handle_test_command(String *args, int offset)
{
    struct Value *result;

    result = expr_value(args->data + offset);
    return result ? valval(result) : shareval(val_zero);
}

struct Value *handle_return_command(String *args, int offset)
{
    struct Value *result;

    if (cmdsub_count) {
        eprintf("may be called only directly from a macro, not in $() command substitution.");
        return shareval(val_zero);
    }
    result = args->len - offset ? handle_test_command(args, offset) : NULL;
    breaking = -1;
    return result;
}

struct Value *handle_result_command(String *args, int offset)
{
    struct Value *result;
    String *str;

    result = handle_return_command(args, offset);
    if (!argtop) {
        str = valstr(result);
        if (str) oputline(str);
    }
    return result;
}

/* A builtin command was explicitly called; execute it. */
static int execute_builtin(BuiltinCmd *cmd, String *args, int offset)
{
    Value *result;
    current_command = cmd->name;
    result = ((*cmd->func)(args, offset));
    set_user_result(result);
    return 1;
}

/* A command with the same name as a builtin was called; execute the macro if
 * there is one, otherwise execute the builtin. */
static int execute_command(BuiltinCmd *cmd, String *args, int offset)
{
    Value *result;
    current_command = cmd->name;
    if (cmd->macro)
	return do_macro(cmd->macro, args, offset, USED_NAME, NULL);
    result = ((*cmd->func)(args, offset));
    set_user_result(result);
    return 1;
}

/* A command with a name not matching a builtin was called.  Call the macro. */
static int execute_macro(const char *name, String *args, int offset,
    const char *old_command)
{
    Macro *macro;
    current_command = name;
    if (!(macro = find_macro(name))) {
        current_command = old_command;  /* for eprefix() */
        do_hook(H_NOMACRO, "!%s: no such command or macro", "%s", name);
        return 0;
    }
    return do_macro(macro, args, offset, USED_NAME, NULL);
}

static const char *execute_start(String **argsp)
{
    const char *old_command;

    (*argsp = Stringdup(*argsp))->links++;
    old_command = current_command;
    return old_command;
}

static int execute_end(const char *old_command, int truth, String *args)
{
    int error = 0;

    if (pending_line && !read_depth) {  /* "/dokey newline" and not in read() */
	current_command = NULL;
        error += !handle_input_line();
    }
    current_command = old_command;
    if (!truth) {
        truth = !valbool(user_result);
        set_user_result(newint(truth));
    }
    Stringfree(args);
    return !error;
}

/* handle_command
 * Execute a single command line that has already been expanded.
 * cmd_line will not be written into.
 */
int handle_command(String *cmd_line)
{
    const char *old_command, *name;
    BuiltinCmd *cmd = NULL;
    int error = 0, truth = 1;
    int offset, end;

    offset = 0;
    if (!cmd_line->data[offset] || is_space(cmd_line->data[offset]))
        return 0;
    old_command = execute_start(&cmd_line);
    name = cmd_line->data + offset;
    while (cmd_line->data[offset] && !is_space(cmd_line->data[offset]))
        offset++;
    if (cmd_line->data[offset]) cmd_line->data[offset++] = '\0';
    while (is_space(cmd_line->data[offset]))
        offset++;
    for (end = cmd_line->len - 1; is_space(cmd_line->data[end]); end--);
    Stringtrunc(cmd_line, end+1);
    while (*name == '!') {
        truth = !truth;
        name++;
    }
    if (*name == '@') {
	name++;
        if (!(cmd = find_builtin_cmd(name))) {
            eprintf("%s: not a builtin command", name);
            error++;
        } else {
	    error += !execute_builtin(cmd, cmd_line, offset);
	}
    } else {
        if ((cmd = find_builtin_cmd(name))) {
	    error += !execute_command(cmd, cmd_line, offset);
        } else {
	    error += !execute_macro(name, cmd_line, offset, old_command);
	}
    }

    error += !execute_end(old_command, truth, cmd_line);
    return !error;
}

/* stringvec
 * Allocates *<vector> and fills it with locations of start and end of each
 * word in <str>, starting at <offset>.  <vecsize> is a guess of the number of
 * words there will be, or -1.  Returns number of words found, or -1 for error.
 * Freeing *<vector> is the caller's responsibility.
 */
static int stringvec(String *str, int offset, Arg **vector, int vecsize)
{
    int count = 0;
    char *start, *next;
    const char *end;

    if (vecsize <= 0) vecsize = 10;
    if (!(*vector = (Arg *)MALLOC(vecsize * sizeof(Arg)))) {
	eprintf("Not enough memory for %d word vector", vecsize);
	return -1;
    }

    for (next = str->data + offset; *next; count++) {
	if (count == vecsize) {
	    *vector = (Arg*)XREALLOC((char*)*vector, sizeof(Arg)*(vecsize+=10));
	    if (!*vector) {
		FREE(*vector);
		*vector = NULL;
		eprintf("Not enough memory for %d word vector", vecsize);
		return -1;
	    }
	}
	start = stringarg(&next, &end);
	(*vector)[count].start = start - str->data;
	(*vector)[count].end = end - str->data;
    }

    return count;
}

Program *compile_tf(String *src, int srcstart, int subs, int is_expr,
    int optimize)
{
    Program *prog;
    prog = MALLOC(sizeof(Program));
    if (!prog) {
	eprintf("out of memory");
	return NULL;
    }
    prog->len = 0;
    prog->size = 0;
    prog->code = NULL;
    (prog->src = src)->links++;
    prog->srcstart = srcstart;
    prog->mark = src->data + srcstart;
    prog->optimize = optimize;
    ip = src->data + srcstart;
    if (is_expr) {
	if (expr(prog)) {
	    if (!*ip) {
		if (cecho > invis_flag) prog_dump(prog);
		return prog;
	    }
	    parse_error(prog, "expression", "end of expression");
	}
    } else {
	if (list(prog, subs)) {
	    if (!*ip) {
		if (cecho > invis_flag) prog_dump(prog);
		return prog;
	    }
	    parse_error(prog, "macro", "end of statement");
	}
    }
    prog_free(prog);
    return NULL;
}

int macro_run(String *body, int bodystart, String *args, int offset,
    int subs, const char *name)
{
    Program *prog;
    int result;

    if (!(prog = compile_tf(body, bodystart, subs, 0, 0))) return 0;
    result = prog_run(prog, args, offset, name, NULL);
    prog_free(prog);
    return result;
}

static int do_parmsub(String *dest, int first, int last, int *emptyp)
{
    int from_stack = argtop, to_stack = !dest, result = 1;
    int orig_len, i;
    Value *val = NULL;

    if (first < 0 || last < first || last >= tf_argc) {
	*emptyp = 1;
    } else if (from_stack) {
	if (to_stack && first == last) {
	    (val = stack[argtop - tf_argc + first])->count++;
	    *emptyp = (val->type & TYPE_STR) && val->sval->len <= 0;
	} else {
	    if (to_stack)
		(dest = Stringnew(NULL, 0, 0))->links++;
	    orig_len = dest->len;
	    for (i = first; ; i++) {
		SStringcat(dest, valstr(stack[argtop-tf_argc+i]));
		if (i == last) break;
		Stringadd(dest, ' ');
	    }
	    *emptyp = dest->len <= orig_len;
	}
    } else {
	if (to_stack)
	    (dest = Stringnew(NULL, 0, 0))->links++;
	orig_len = dest->len;
	SStringoncat(dest, argstring, tf_argv[first].start,
	    tf_argv[last].end - tf_argv[first].start);
	*emptyp = dest->len <= orig_len;
    }

    if (to_stack) {
	if (!val) val = newSstr(dest && dest->len ? dest : blankline);
	result = pushval(val);
	if (dest) Stringfree(dest);
    }

    return result;
}

static void do_mecho(Program *prog, int i)
{
    /* XXX is invis_flag set correctly at runtime? */
    if (prog->code[i].start && prog->code[i].end) {
	tfprintf(tferr, "%S%.*s", do_mprefix(),
	    prog->code[i].end - prog->code[i].start,
	    prog->code[i].start);
    }
}

Value *prog_interpret(Program *prog, int in_expr)
{
    Value *val, *result = NULL;
    String *str, *tbuf;
    int user_result_is_set = 0;
    int cip, stackbot, no_arg, i;
    int empty;	/* for varsub default */
    opcode_t op;
    int first, last, n;
    String *buf;
    const char *cstr, *old_cmd;
    BuiltinCmd *cmd;
    struct tf_frame {
	TFILE *orig_tfin, *orig_tfout;	/* restored when done */
	TFILE *local_tfin, *local_tfout;/* restored after pipes */
	TFILE *inpipe, *outpipe;	/* pipes between commands */
    } first_frame, *frame;
#if 0
    TFILE **filep;
#define which_tfile_p(c) \
    (c=='i' ? &tfin : c=='o' ? &tfout : c=='e' ? &tferr : NULL)
#endif

    frame = &first_frame;
    frame->local_tfin = frame->orig_tfin = tfin;
    frame->local_tfout = frame->orig_tfout = tfout;
    frame->inpipe = frame->outpipe = NULL;

    (buf = Stringnew(NULL, 0, 0))->links++;
    stackbot = stacktop;

    for (cip = 0; cip < prog->len; cip++) {
	if (breaking) {
	    if (breaking < 0) break;
	    for ( ; breaking && cip < prog->len; cip++) {
		if (prog->code[cip].op == OP_DONE) {
		    breaking--;
		    /* mecho each "/done" as we break out of them */
		    if (mecho > invis_flag) do_mecho(prog, cip);
		}
	    }
	    if (cip >= prog->len) break;
	}

	user_result_is_set = 0;
	op = prog->code[cip].op;
	if (mecho > invis_flag) do_mecho(prog, cip);
	if (iecho > invis_flag) inst_dump(prog, cip, 'x');

	if (op_type_is(op, EXPR)) {
	    if (!reduce(op, prog->code[cip].arg.i))
		goto prog_interpret_exit;
	    continue;
	}

#define setup_next_io() \
    do { \
	if (!frame->inpipe) { \
	    frame->local_tfin = tfin; /* save any change */ \
	} else { \
	    tfclose(frame->inpipe); /* close inpipe */ \
	} \
	if (!frame->outpipe) { \
	    frame->local_tfout = tfout; /* save any change */ \
	    tfin = frame->local_tfin; /* no pipe into next cmd */ \
	    frame->inpipe = NULL; /* no pipe into next cmd */ \
	} else { \
	    tfin = frame->inpipe = frame->outpipe; /* pipe into next cmd */ \
	    frame->outpipe = NULL; \
	    tfout = frame->local_tfout; /* no pipe out of next cmd */ \
	} \
    } while (0)

	switch (op) {
	case OP_PIPE:
	    tfout = frame->outpipe = tfopen(NULL, "q");
	    break;

	case OP_EXECUTE:
	    str = prog->code[cip].arg.str;
	    if ((no_arg = !str)) str = buf;
	    handle_command(str);
	    if (no_arg) Stringtrunc(buf, 0);
	    user_result_is_set = 1;
	    setup_next_io();
	    break;

	case OP_ARG:
	    str = prog->code[cip].arg.str;
	    if ((no_arg = !str)) str = buf;
	    /* no_arg and str will be used by OP_BUILTIN or OP_COMMAND */
	    break;

	case OP_BUILTIN:
	case OP_BUILTIN | OPR_FALSE:
	    /* no_arg and str were set by OP_ARG */
	    cmd = prog->code[cip].arg.cmd;
	    old_cmd = execute_start(&str);    /*XXX optimize: needn't dup buf */
	    execute_builtin(cmd, str, 0);
	    execute_end(old_cmd, !(op & OPR_FALSE), str);
	    if (no_arg) Stringtrunc(buf, 0);
	    user_result_is_set = 1;
	    setup_next_io();
	    break;

	case OP_COMMAND:
	case OP_COMMAND | OPR_FALSE:
	    /* no_arg and str were set by OP_ARG */
	    cmd = prog->code[cip].arg.cmd;
	    old_cmd = execute_start(&str);    /*XXX optimize: needn't dup buf */
	    execute_command(cmd, str, 0);
	    execute_end(old_cmd, !(op & OPR_FALSE), str);
	    if (no_arg) Stringtrunc(buf, 0);
	    user_result_is_set = 1;
	    setup_next_io();
	    break;

	case OP_MACRO:
	case OP_MACRO | OPR_FALSE:
	    str = prog->code[cip].arg.str;
	    if ((no_arg = !str)) str = buf;
	    old_cmd = execute_start(&str);    /*XXX optimize: needn't dup buf */
	    for (i = 0; str->data[i] && !is_space(str->data[i]); i++);
	    if (str->data[i]) {
		str->data[i] = '\0';
		do { ++i; } while (is_space(str->data[i]));
	    }
	    execute_macro(str->data, str, i, old_cmd);
	    execute_end(old_cmd, !(op & OPR_FALSE), str);
	    if (no_arg) Stringtrunc(buf, 0);
	    user_result_is_set = 1;
	    setup_next_io();
	    break;

	case OP_SEND:
	    str = prog->code[cip].arg.str;
	    if ((no_arg = !str)) str = buf;
	    if (str->len || !snarf) {
#if 0
		if (/*(subs == SUB_MACRO) &&*//*XXX*/ (mecho > invis_flag))
		    tfprintf(tferr, "%S%s%S", do_mprefix(), "SEND: ", str);
#endif
		if (!do_hook(H_SEND, NULL, "%S", str)) {
		    set_user_result(newint(send_line(str->data, str->len,
			TRUE)));
		    user_result_is_set = 1; /* XXX ? */
		}
	    }
	    if (no_arg) Stringtrunc(buf, 0);
	    setup_next_io();
	    break;

	case OP_APPEND:
	    str = prog->code[cip].arg.str;
	    if (!str) {
		SStringcat(buf, opdstr(1));
		freeval(stack[--stacktop]);
	    } else {
		SStringcat(buf, str);
	    }
	    break;
	case OP_JUMP:
	    cip = prog->code[cip].arg.i - 1;
	    break;
	case OP_JZ:
	    if (!opdint(1))
		cip = prog->code[cip].arg.i - 1;
	    freeval(stack[--stacktop]);
	    break;
	case OP_JNZ:
	    if (opdint(1))
		cip = prog->code[cip].arg.i - 1;
	    freeval(stack[--stacktop]);
	    break;
	case OP_JRZ:
	    if (!valint(user_result))
		cip = prog->code[cip].arg.i - 1;
	    break;
	case OP_JRNZ:
	    if (valint(user_result))
		cip = prog->code[cip].arg.i - 1;
	    break;
	case OP_JNEMPTY:
	    /* empty was set by one of the varsub operators */
	    if (!empty)
		cip = prog->code[cip].arg.i - 1;
	    break;
	case OP_RETURN:
	case OP_RESULT:
	    if ((val = prog->code[cip].arg.val))
		val->count++;
	    else
		val = stack[--stacktop];
	    set_user_result(valval(val));
	    if (op == OP_RESULT && !argtop) {
		str = valstr(val);
		oputline(str ? str : blankline);
	    }
	    cip = prog->len - 1;
	    setup_next_io();
	    break;
	case OP_TEST:
	    if ((val = prog->code[cip].arg.val))
		val->count++;
	    else
		val = stack[--stacktop];
	    set_user_result(valval(val));
	    setup_next_io();
	    break;
	case OP_PUSHBUF:
	    if (!pushval(newSstr(buf))) /* XXX optimize */
		goto prog_interpret_exit;
	    (buf = Stringnew(NULL, 0, 0))->links++;
	    break;
	case OP_POPBUF:
	    Stringfree(buf);
	    buf = valstr(stack[--stacktop]); /* XXX optimize */
	    freeval(stack[stacktop]);
	    break;
	case OP_CMDSUB:
	    if (!pushval(newptr(frame))) /* XXX optimize */
		goto prog_interpret_exit;
	    frame = XMALLOC(sizeof(*frame));
	    frame->local_tfout = tfout = tfopen(NULL, "q");
	    frame->local_tfin = tfin;
	    frame->inpipe = frame->outpipe = NULL;
	    break;
#if 0
	case OP_POPFILE:
	    filep = which_tfile_p(prog->code[cip].arg.c);
	    tfclose(*filep);
	    *filep = (TFILE*)valptr(stack[--stacktop]); /* XXX optimize */
	    freeval(stack[stacktop]);
	    break;
#endif
	case OP_ACMDSUB:
	case OP_PCMDSUB:
	    if (op_is_push(op))
		/* If we init tbuf to length 0, and queue is empty, tbuf->data
		 * would stay NULL, breaking some uses of it (eg OP_STRNEQ) */
		(tbuf = Stringnew(NULL, 1, 0))->links++;
	    else
		tbuf = buf;
	    first = 1;
	    while ((str = dequeue((tfout)->u.queue))) {
		if (!((str->attrs & F_GAG) && gag)) {
		    if (!first) {
			Stringadd(tbuf, ' ');
			if (tbuf->charattrs) tbuf->charattrs[tbuf->len] = 0;
		    }
		    first = 0;
		    SStringcat(tbuf, str);
		}
		Stringfree(str);
	    }
	    tfclose(tfout);
	    FREE(frame);
	    frame = valptr(stack[--stacktop]); /* XXX optimize */
	    tfout = frame->outpipe ? frame->outpipe : frame->local_tfout;
	    tfin = frame->inpipe ? frame->inpipe : frame->local_tfin;
	    freeval(stack[stacktop]);
	    if (op_is_push(op)) {
		if (!pushval(newSstr(tbuf)))
		    goto prog_interpret_exit;
		Stringfree(tbuf);
	    }
	    break;
	case OP_PBUF:
	    str = buf;
	    buf = valstr(stack[--stacktop]); /* XXX optimize */
	    freeval(stack[stacktop]);
	    if (!pushval(newSstr(str)))
		goto prog_interpret_exit;
	    Stringfree(str);
	    break;
	case OP_AMAC:
	case OP_PMAC:
	    if ((str = prog->code[cip].arg.str)) {
		str->links++;
	    } else {
		str = buf;
		buf = valstr(stack[--stacktop]); /* XXX optimize */
		freeval(stack[stacktop]);
	    }
	    if (!(cstr = macro_body(str->data))) {
		tfprintf(tferr, "%% macro not defined: %S", str);
	    }
	    if (op_is_push(op)) {
		if (!pushval(newstr(cstr ? cstr : "", -1)))
		    goto prog_interpret_exit;
	    } else if (cstr) {
		Stringcat(buf, cstr);
	    }
	    if (mecho > invis_flag)
		tfprintf(tferr, "%S$%S --> %s", do_mprefix(), str, cstr);
	    Stringfree(str);
	    break;
	case OP_AVAR:
	case OP_PVAR:
	    str = prog->code[cip].arg.str;
	    val = getnearestvarval(str->data);
	    empty = 1;
	    if (op_is_push(op)) {
		if (val) {
		    val->count++;
		    if (!pushval(val))
			goto prog_interpret_exit;
		    empty = (val->type & TYPE_STR && !val->sval->len);
		} else {
		    if (!pushval(newstr("", -1)))
			goto prog_interpret_exit;
		    empty = 1;
		}
	    } else if (val) {
		str = valstr(val);
		empty = !str->len;
		SStringcat(buf, str);
	    }
	    break;
	case OP_AREG:
	    empty = (regsubstr(buf, prog->code[cip].arg.i) <= 0);
	    break;
	case OP_PREG:
	    str = Stringnew(NULL, 0, 0);
	    empty = (regsubstr(str, prog->code[cip].arg.i) <= 0);
	    if (empty) Stringcat(str, "");
	    if (!pushval(newSstr(str)))
		goto prog_interpret_exit;
	    break;
	case OP_APARM:
	case OP_PPARM:
	    first = prog->code[cip].arg.i;
	    if (first < 0) {
		first = -first;
		last = tf_argc - 1;
	    } else {
		last = first = first - 1;
	    }
	    if (!do_parmsub(op_is_push(op) ? NULL : buf, first, last, &empty))
		goto prog_interpret_exit;
	    break;
	case OP_ALPARM:
	case OP_PLPARM:
	    first = prog->code[cip].arg.i;
	    if (first < 0) {
		last = tf_argc + first - 1;
		first = 0;
	    } else {
		last = first = tf_argc - first;
	    }
	    if (!do_parmsub(op_is_push(op) ? NULL : buf, first, last, &empty))
		goto prog_interpret_exit;
	    break;
	case OP_ASPECIAL:	/* append */
	    empty = 0;
	    switch (prog->code[cip].arg.c) {
	    case '#':
		Sprintf(buf, SP_APPEND, "%d", tf_argc);
		break;
	    case '?':
		SStringcat(buf, valstr(user_result));
		break;
	    case '0':
		if (!(empty = !(current_command && *current_command != '\b')))
		    Stringcat(buf, current_command);
		break;
	    case '*':
		if (!do_parmsub(buf, 0, tf_argc - 1, &empty))
		    goto prog_interpret_exit;
		break;
	    case 'R':
		n = tf_argc ? RRAND(0, tf_argc - 1) : -1;
		if (!do_parmsub(buf, n, n, &empty))
		    goto prog_interpret_exit;
		break;
	    }
	    break;
	case OP_PSPECIAL:	/* push */
	    empty = 0;
	    switch (prog->code[cip].arg.c) {
	    case '#':
		if (!pushval(newint(tf_argc)))
		    goto prog_interpret_exit;
		break;
	    case '?':
		if (!pushval(user_result))
		    goto prog_interpret_exit;
		user_result->count++;
		break;
	    case '0':
		if ((empty = (current_command && *current_command != '\b')))
		    val = newSstr(blankline);
		else
		    val = newstr(current_command, -1);
		if (!pushval(val))
		    goto prog_interpret_exit;
	    case '*':
		if (!do_parmsub(NULL, 0, tf_argc - 1, &empty))
		    goto prog_interpret_exit;
		break;
	    case 'R':
		n = tf_argc ? RRAND(0, tf_argc - 1) : -1;
		if (!do_parmsub(NULL, n, n, &empty))
		    goto prog_interpret_exit;
		break;
	    }
	    break;
	case OP_DUP:	/* duplicate the ARGth item from the top of the stack */
	    (val = opd(prog->code[cip].arg.i))->count++;
	    if (!pushval(val)) goto prog_interpret_exit;
	    break;
	case OP_POP:	/* argument is ignored */
	    freeval(stack[--stacktop]);
	    break;
	case OP_PUSH:	/* push (Value*)ARG onto stack */
	    (val = (prog->code[cip].arg.val))->count++;
	    if (!pushval(val)) goto prog_interpret_exit;
	    break;
	case OP_ENDIF:
	case OP_DONE:
	case OP_NOP:
	    /* no op: place holders for mecho pointers */
	    break;
	default:
	    internal_error(__FILE__, __LINE__, "invalid opcode 0x%04X at %d",
		op, cip);
	    break;
	}
    }

    if (stacktop == stackbot + in_expr) {
	result = in_expr ? stack[--stacktop] : user_result;
    } else {
	eprintf("expression stack underflow or dirty (%+d)",
	    stacktop - (stackbot + in_expr));
    }

prog_interpret_exit:
    if (frame != &first_frame) {
	internal_error(__FILE__, __LINE__, "invalid frame");
	frame = &first_frame;
    }
    tfin = frame->orig_tfin;
    tfout = frame->orig_tfout;

    while (stacktop > stackbot)
	freeval(stack[--stacktop]);
    Stringfree(buf);
    return result;
}

int prog_run(Program *prog, String *args, int offset, const char *name,
    String *kbnumlocal)
{
    Arg *true_argv = NULL;		/* unshifted argument vector */
    int saved_cmdsub, saved_argc, saved_breaking, saved_argtop;
    Arg *saved_argv;
    String *saved_argstring;
    const char *saved_command;
    TFILE *saved_tfin, *saved_tfout, *saved_tferr;
    List scope[1];

    if (++recur_count > max_recur && max_recur) {
        eprintf("too many recursions");
        recur_count--;
        return 0;
    }
    saved_command = current_command;
    saved_cmdsub = cmdsub_count;
    saved_argstring = argstring;
    saved_argc = tf_argc;
    saved_argv = tf_argv;
    saved_breaking = breaking;
    saved_argtop = argtop;
    saved_tfin = tfin;
    saved_tfout = tfout;
    saved_tferr = tferr;

    if (name) current_command = name;
    cmdsub_count = 0;

    pushvarscope(scope);
    if (kbnumlocal)
	setlocalvar("kbnum", TYPE_STR, kbnumlocal);

    if (args) {
        argstring = args;
	tf_argc = stringvec(args, offset, &tf_argv, 20);
        true_argv = tf_argv;
        argtop = 0;
    }
       /* else, leave argstring, tf_argv, and tv_argc alone, so /eval body
        * inherits positional parameters */

    if (tf_argc >= 0) {
        if (!prog_interpret(prog, !name)) set_user_result(NULL);
    }

    if (true_argv) FREE(true_argv);
    popvarscope();

    tfin = saved_tfin;
    tfout = saved_tfout;
    tferr = saved_tferr;
    cmdsub_count = saved_cmdsub;
    tf_argc = saved_argc;
    tf_argv = saved_argv;
    argstring = saved_argstring;
    breaking = exiting ? -1 : saved_breaking;
    argtop = saved_argtop;
    current_command = saved_command;
    recur_count--;
    return !!user_result;
}

String *do_mprefix(void)
{
    STATIC_BUFFER(buffer);
    int i;

    Stringtrunc(buffer, 0);
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

static void code_mark(Program *prog, const char *mark)
{
    int i;

    prog->mark = mark;
    for (i = prog->len - 1; i >= 0; i--) {
	if (prog->code[i].start && !prog->code[i].end) {
	    prog->code[i].end = mark;
	    break;
	}
    }
}

static void vcode_add(Program *prog, opcode_t op, int use_mark, va_list ap)
{
    Instruction *inst;

    /* The -1 in the condition, and zeroing of instructions, are to allow
     * the use of comefrom() on code that hasn't been emitted yet. */
    if (prog->len >= prog->size - 1) {
	prog->size += 20;
	prog->code = XREALLOC(prog->code, prog->size * sizeof(Instruction));
	memset(&prog->code[prog->size - 20], 0, 20 * sizeof(Instruction));
    }

    inst = &prog->code[prog->len];
    prog->len++;
    inst->start = inst->end = NULL;
    inst->op = op;

    switch (op_arg_type(op)) {
	case OPA_INT:  inst->arg.i    = va_arg(ap, int);		break;
	case OPA_CHAR: inst->arg.c    = (char)va_arg(ap, int);		break;
	case OPA_STRP: inst->arg.str  = va_arg(ap, String *);		break;
	case OPA_VALP: inst->arg.val  = va_arg(ap, Value *);		break;
	case OPA_CMDP: inst->arg.cmd  = va_arg(ap, BuiltinCmd *);	break;
	default:       inst->arg.i    = 0;
    }

    if (prog->mark && use_mark) {
	inst->start = prog->mark;
	prog->mark = NULL;
    }

    if (!prog->optimize)
	return;

#define inst_is_const(inst) \
    ((inst)->op == OP_PUSH && (inst)->arg.val->type != TYPE_ID)

    while (1) {
	if (inst->comefroms)
	    return;

	switch (op_type(inst->op)) {
	case OPT_EXPR:
	    /* An expression operator with no side effects and compile-time
	     * constant operands can be reduced at compile time.
	     */
	    if (!op_has_sideeffect(inst->op)) {
		int i, n;
		int old_stacktop = stacktop;
		n = inst->arg.i;
		for (i = prog->len - n - 1; i < prog->len - 1; i++) {
		    if (!inst_is_const(prog->code+i) || prog->code[i+1].comefroms)
			return;
		}
		for (i = prog->len - n - 1; i < prog->len - 1; i++) {
		    if (!pushval(prog->code[i].arg.val))
			goto const_expr_error;
		    prog->code[i].arg.val = NULL;
		}
		if (!reduce(inst->op, n))
		    goto const_expr_error;
		prog->len -= n;
		inst = &prog->code[prog->len - 1];
		/* inst->op = OP_PUSH; */ /* already true */
		inst->arg.val = stack[--stacktop];
		continue;
	    const_expr_error:
		while (stacktop > old_stacktop)
		    freeval(stack[--stacktop]);
		return /* 0 */; /* XXX */
	    }
	    return;

	case OPT_CTRL:
	    if (prog->len <= 1)
		return;

	    if (inst->op == OP_APPEND) {
		if (!inst->arg.str && inst_is_const(inst-1)) {
		    Value *val = inst[-1].arg.val;
		    prog->len--;
		    inst--;
		    inst->op = inst[1].op;
		    (inst->arg.str = valstr(val))->links++;
		    freeval(val);
		    continue;
		} else if (inst->arg.str && inst[-1].op == OP_APPEND &&
		    inst[-1].arg.str)
		{
		    prog->len--;
		    inst--;
		    SStringcat(inst->arg.str, inst[1].arg.str);
		    Stringfree(inst[1].arg.str);
		    /* inst->op = OP_APPEND; */ /* already true */
		    continue;
		}

	    } else if (inst->op == OP_ARG && !inst->arg.val && prog->len == 2 &&
		inst[-1].op == OP_APPEND && inst[-1].arg.str)
	    {
		/* {0: APPEND string; 1: ARG NULL} to {0: ARG string} */
		prog->len--;
		inst--;
		inst->op = inst[1].op;
		/* keep inst->arg.str */
		continue;

	    } else if (op_arg_type(inst->op) == OPA_VALP && !inst->arg.val &&
		inst_is_const(inst-1))
	    {
		/* e.g., {PUSH val; TEST NULL} to {TEST val} */
		prog->len--;
		inst--;
		inst->op = inst[1].op;
		/* inst->arg.val = inst[1].arg.val; */ /* already true */
		continue;
	    }
	    return;

	case OPT_JUMP:
	    if (prog->len <= 1)
		return;
	    if ((inst->op & ~1) == OP_JZ) {
		if (inst[-1].op == '!') {
		    /* e.g., {! 1; JZ x} to {JNZ x} */
		    prog->len--;
		    inst--;
		    inst->op = inst[1].op ^ 1;
		    inst->arg = inst[1].arg;
		    continue;
		} else if (inst_is_const(inst-1)) {
		    /* e.g., {PUSH INT 0; JZ x} to {JUMP x} */
		    int flag;
		    prog->len--;
		    inst--;
		    flag = valbool(inst->arg.val);
		    freeval(inst->arg.val);
		    inst->op = (!flag == !(inst[1].op & 1)) ? OP_JUMP : OP_NOP;
		    inst->arg.i = inst[1].arg.i;
		    continue;
		}
	    }
	    return;

	default:
	    return;
	}
    }
}

void code_add(Program *prog, opcode_t op, ...)
{
    va_list ap;
    va_start(ap, op);
    vcode_add(prog, op, 1, ap);
    va_end(ap);
}

static void code_add_nomecho(Program *prog, opcode_t op, ...)
{
    va_list ap;
    va_start(ap, op);
    vcode_add(prog, op, 0, ap);
    va_end(ap);
}

void eat_newline(Program *prog)
{
    while (ip[0] == '\\' && ip[1] == '\n') {
	loadstart++;
	for (ip += 2; is_space(*ip); ip++);
    }
}

void eat_space(Program *prog)
{
    while (is_space(*ip)) ip++;
    eat_newline(prog);
}

static int list(Program *prog, int subs)
{
    keyword_id_t oldblock;
    int is_a_command, is_a_condition, is_special;
    int failed = 0, result = 0;
    const char *exprstart, *stmtstart;
    static const char unexpect_msg[] = "unexpected /%s in %s block";
    int is_pipe = 0;
    int block_start, jump_point;

#define unexpected(innerblock, outerblock) \
    eprintf(unexpect_msg, keyword_table[innerblock - BREAK], \
        outerblock ? keyword_table[outerblock - BREAK] : "outer");

    /* Do NOT strip leading space here.  This allows user to type and send
     * lines with leading spaces (but completely empty lines are handled
     * by handle_input_line()).  During expansion, spaces AFTER a "%;"
     * or keyword will be skipped.
     */

    if (block == WHILE || block == IF) {
	block_start = prog->len;
    }

    do /* while (*ip) */ {
        if (subs >= SUB_NEWLINE) {
            while (is_space(*ip) || (ip[0] == '\\' && is_space(ip[1])))
                ++ip;
	    eat_newline(prog);
	}

        is_special = is_a_command = is_a_condition = FALSE;

        /* Lines begining with one "/" are tf commands.  Lines beginning
         * with multiple "/"s have the first removed, and are sent to server.
         */

        if ((subs > SUB_LITERAL) && (*ip == '/') && (*++ip != '/')) {
            is_a_command = TRUE;
            oldblock = block;
            if (subs >= SUB_KEYWORD) {
                stmtstart = ip;
                is_special = block = keyword_parse(prog);
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
                if (!expr(prog)) goto list_exit;
                if (*ip != ')') {
                    parse_error(prog, "condition", "operator or ')'");
                    goto list_exit;
                }
		++ip; /* skip ')' */
		eat_space(prog);
                block = (block == WHILE) ? DO : THEN;
            } else if (*ip) {
                eprintf("warning: statement starting with %s in /%s "
                    "condition sends text to server, "
                    "which is probably not what was intended.",
                    error_text(prog), keyword_table[block - BREAK]);
            }
        }

        if (is_a_command || is_a_condition) {
            switch(block) {
            case WHILE:
                if (!list(prog, subs)) failed = 1;
                else if (block == WHILE) {
                    parse_error(prog, "macro", "/do");
                    failed = 1;
                } else if (block == DO) {
                    parse_error(prog, "macro", "/done");
                    failed = 1;
                }
                block = oldblock;
                if (failed) goto list_exit;
                break;

            case DO:
                if (oldblock != WHILE) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
		code_add(prog, is_a_condition ? OP_JZ : OP_JRZ, -1);
		code_mark(prog, ip);
		jump_point = prog->len - 1;
                continue;

            case DONE:
                if (oldblock != DO) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
		code_add_nomecho(prog, OP_JUMP, block_start);
		code_add(prog, OP_DONE, 0);
		comefrom(prog, jump_point, prog->len - 1);
		eat_space(prog);
		code_mark(prog, ip);
		result = 1;  goto list_exit;

            case IF:
                if (!list(prog, subs)) {
                    failed = 1;
                } else if (block == IF || block == ELSEIF) {
                    parse_error(prog, "macro", "/then");
                    failed = 1;
                } else if (block == THEN || block == ELSE) {
                    parse_error(prog, "macro", "/endif");
                    failed = 1;
                }
                block = oldblock;
                if (failed) goto list_exit;
		code_mark(prog, ip);
                break;

            case THEN:
                if (oldblock != IF && oldblock != ELSEIF) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
		code_add_nomecho(prog, is_a_condition ? OP_JZ : OP_JRZ, -1);
		code_mark(prog, ip);
		jump_point = prog->len - 1;
                continue;

            case ELSEIF:
            case ELSE:
                if (oldblock != THEN) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
		code_add_nomecho(prog, OP_JUMP, -1);
		comefrom(prog, jump_point, prog->len);
                continue;

            case ENDIF:
                if (oldblock != THEN && oldblock != ELSE) {
                    unexpected(block, oldblock);
                    block = oldblock;
                    goto list_exit;
                }
		eat_space(prog);
		/* fill in the jump-to-end after each THEN block */
		{
		    int i;
		    for (i = block_start; i < prog->len; i++) {
			if (op_type_is(prog->code[i].op, JUMP) &&
			    prog->code[i].arg.i == -1)
			{
			    prog->code[i].arg.i = prog->len;
			}
		    }
		}
		code_add(prog, OP_ENDIF);   /* no-op, to hold mecho pointers */
                result = 1;  goto list_exit;

            default:
                /* not a control statement */
                ip = stmtstart - 1;
                is_special = 0;
                block = oldblock;
                if (!statement(prog, subs)) goto list_exit;
                break;
            }

        } else /* !(is_a_command || is_a_condition) */ {
            if (is_pipe) {
                eprintf("Piping input to a server command is not allowed.");
                goto list_exit;
            }
            if (!statement(prog, subs))
                goto list_exit;
        }

        is_pipe = (ip[0] == '%' && ip[1] == '|'); /* this stmnt pipes to next */
        if (is_pipe) {
	    Instruction tmp;
            if (!is_a_command) {
                eprintf("Piping output of a server command is not allowed.");
                goto list_exit;
            } else if (is_special) {
                eprintf("Piping output of a special command is not allowed.");
                goto list_exit;
            }
	    /* OP_PIPE needs to go BEFORE the exec operator, so we use
	     * code_add() to add it, then swap the last two instructions. */
            code_add(prog, OP_PIPE);
	    tmp = prog->code[prog->len - 1];
	    prog->code[prog->len - 1] = prog->code[prog->len - 2];
	    prog->code[prog->len - 2] = tmp;
        }

        if (is_end_of_cmdsub(ip)) {
            break;
        } else if (is_end_of_statement(ip)) {
            ip += 2;
	    eat_space(prog);
	    code_mark(prog, ip);
        } else if (*ip) {
            parse_error(prog, "macro", "end of statement");
            goto list_exit;
        }

    } while (*ip);

    code_mark(prog, ip);
    result = 1;

    if (is_pipe) {
        eprintf("'%|' must be followed by another command.");
        result = 0;
    }

list_exit:
    return result;
}

const char **keyword(const char *id)
{
    return (const char **)binsearch((void*)id, (void*)keyword_table,
        sizeof(keyword_table)/sizeof(char*), sizeof(char*), cstrstructcmp);
}

static keyword_id_t keyword_parse(Program *prog)
{
    const char **result, *end;
    char buf[KEYWORD_LENGTH+1];

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

/* percentsub() and dollarsub() append to the compile-time buffer *destp if
 * source is compile-time constant; otherwise, it emits [APPEND *destp],
 * creates a new compile-time buffer, and emits code for the sub.
 */
static int percentsub(Program *prog, int subs, String **destp)
{
    int result = 1;

    if (*ip == '%') {
	while (*ip == '%') Stringadd(*destp, *ip++);
    } else if (subs >= SUB_FULL) {
	if ((*destp)->len) {
	    code_add(prog, OP_APPEND, *destp);
	    (*destp = Stringnew(NULL, 0, 0))->links++;
	}
	result = varsub(prog, 0, !destp);
    } else {
	Stringadd(*destp, '%');
    }
    return result;
}

int dollarsub(Program *prog, String **destp)
{
    int result = 1;

    if (*ip == '$' && destp) {
	while (*ip == '$') Stringadd(*destp, *ip++);
    } else {
	if (destp && (*destp)->len) {
	    code_add(prog, OP_APPEND, *destp);
	    (*destp = Stringnew(NULL, 0, 0))->links++;
	}
	result = ((*ip == '[') ? exprsub(prog, !destp) :
	    (*ip == '(') ? cmdsub(prog, !destp) :
	    macsub(prog, !destp));
    }
    return result;
}

static int statement(Program *prog, int subs)
{
    const char *start;
    String *dest;
    int result = 1, resolvable = 0;
    int orig_len;
    opcode_t op;
    BuiltinCmd *cmd = NULL;

    orig_len = prog->len;
    /* len=1 forces dest->data to be allocated (expected by prog_interpret) */
    (dest = Stringnew(NULL, 1, 0))->links++;
    /*code_add(prog, OP_NEWSTMT);*/

    if (ip[0] != '/') {
	op = OP_SEND;
    } else if (ip[1] == '/') {
	ip++;
	op = OP_SEND;
    } else {
	ip++;
	op = OP_EXECUTE;
	resolvable = 1;
    }

    while (*ip) {
	eat_newline(prog);
        if (*ip == '\\' && subs >= SUB_NEWLINE) {
            ++ip;
            if (!backsub(prog, dest)) { result = 0; break; }
        } else if (*ip == '/' && subs >= SUB_FULL) {
            ++ip;
            if (!slashsub(prog, dest)) { result = 0; break; }
        } else if (*ip == '%' && subs >= SUB_NEWLINE) {
            if (is_end_of_statement(ip)) {
                while (dest->len && is_space(dest->data[dest->len-1]))
                    Stringtrunc(dest, dest->len-1);  /* nuke spaces before %; */
                break;
            }
            ++ip;
	    if (!percentsub(prog, subs, &dest)) { result = 0; break; }
	    if (prog->len != orig_len) resolvable = 0;
        } else if (*ip == '$' && subs >= SUB_FULL) {
            ++ip;
	    if (!dollarsub(prog, &dest)) { result = 0; break; }
	    if (prog->len != orig_len) resolvable = 0;
        } else if (subs >= SUB_FULL && is_end_of_cmdsub(ip)) {
            break;
        } else {
            /* is_statmeta() is much faster than all those if statements. */
            start = ip++;
            while (*ip && !is_statmeta(*ip) && !(is_space(*ip) && resolvable))
		ip++;
            SStringoncat(dest, prog->src, start - prog->src->data, ip - start);
	    if (resolvable && (!*ip || is_space(*ip))) {
		/* command name is constant, can be resolved at compile time */
		int i, truth = 1;
		for (i = 0; dest->data[i] == '!'; i++)
		    truth = !truth;
		op = (dest->data[i] == '@') ? (++i, OP_BUILTIN) : OP_COMMAND;
		if ((cmd = find_builtin_cmd(dest->data + i))) {
		    Stringtrunc(dest, 0);
		    while (is_space(*ip)) { ip++; }
		} else if (op == OP_BUILTIN) {
		    eprintf("%s: not a builtin command", dest->data + i);
		    result = 0;
		    break;
		} else {
		    int j = 0;
		    while (i <= dest->len)
			dest->data[j++] = dest->data[i++];
		    dest->len = j - 1;
		    op = OP_MACRO;
		}
		if (!truth)
		    op |= OPR_FALSE;
		resolvable = 0;
	    }
        }
    }

    if (!result) {
	Stringfree(dest);
    } else {
	if (prog->len > orig_len) {
	    if (dest->len) {
		code_add(prog, OP_APPEND, dest);
	    } else {
		Stringfree(dest);
	    }
	    dest = NULL;
	}
	if (cmd) {
	    /* XXX this optimization should be done AFTER code_add(), which
	     * may have optimized a complex argument down to a single string.
	     */
	    if (dest && (
                cmd->func == handle_test_command ||
		cmd->func == handle_result_command ||
		cmd->func == handle_return_command))
	    {
		const char *saved_ip = ip;
		Value *val = NULL;
		int exprstart = prog->len; /* start of expr code in prog */
		ip = dest->data;
		eat_space(prog);
		if (!*ip) {
		    val = shareval(val_zero);
		} else if (!expr(prog)) {
		    prog_free_tail(prog, exprstart); /* remove bad expr code */
		    val = shareval(val_zero);
		} else if (*ip) {
                    parse_error(prog, "expression", "end of expression");
		    prog_free_tail(prog, exprstart); /* remove bad expr code */
		    val = shareval(val_zero);
		}
		ip = saved_ip; /* XXX need to restore line number too */
		if (cmd->func == handle_result_command) {
		    code_add(prog, OP_RESULT, val);
		} else if (cmd->func == handle_return_command) {
		    code_add(prog, OP_RETURN, val);
		} else /* if (cmd->func == handle_test_command) */ {
		    code_add(prog, OP_TEST, val);
		}
		/* XXX is this safe? prog may have debug ptrs into dest. */
		Stringfree(dest);
	    } else {
		code_add(prog, OP_ARG, dest);
		code_add(prog, op, cmd);
	    }
	} else {
	    code_add(prog, op, dest);
	}
    }
    return result;
}

static int slashsub(Program *prog, String *dest)
{
    if (*ip == '/' && oldslash)
        while (*ip == '/') Stringadd(dest, *ip++);
    else
        Stringadd(dest, '/');
    return 1;
}

static const char *error_text(Program *prog)
{
    STATIC_BUFFER(buf);

    if (*ip) {
        const char *end = ip + 1;
        if (is_alnum(*ip) || is_quote(*ip) || *ip == '/') {
            while (is_alnum(*end)) end++;
        }
        Sprintf(buf, 0, "'%.*s'", end - ip, ip);
        return buf->data;
    } else {
        return "end of body";
    }
}

void parse_error(Program *prog, const char *type, const char *expect)
{
    eprintf("%s syntax error: expected %s, found %s.",
        type, expect, error_text(prog));
}


int exprsub(Program *prog, int in_expr)
{
    int result = 0;

    ip++; /* skip '[' */
    eat_space(prog);
    if (!expr(prog)) return 0;
    if (!*ip || is_end_of_statement(ip)) {
        eprintf("unmatched $[");
    } else if (*ip != ']') {
        parse_error(prog, "expression", "operator");
    } else {
	if (!in_expr)
	    code_add(prog, OP_APPEND, NULL);
        ++ip;
        result = 1;
    }
    return result;
}


static int cmdsub(Program *prog, int in_expr)
{
    int result;
    const char *saved_mark;

    code_add(prog, OP_CMDSUB);
    code_add(prog, OP_PUSHBUF, 0);
    cmdsub_count++;

    ip++; /* skip '(' */
    saved_mark = prog->mark;
    prog->mark = ip;
    result = list(prog, SUB_MACRO);
    prog->mark = saved_mark;

    cmdsub_count--;
    code_add(prog, OP_POPBUF, 0);
    code_add(prog, in_expr ? OP_PCMDSUB : OP_ACMDSUB);

    if (*ip != ')') {
        eprintf("unmatched (");
        return 0;
    }

    ip++;
    return result;
}

static int macsub(Program *prog, int in_expr)
{
    const char *s;
    int bracket, dynamic = 0;
    String *name;

    code_add(prog, OP_PUSHBUF, 0);

    (name = Stringnew(NULL, 0, 0))->links++;
    if ((bracket = (*ip == '{'))) ip++;
    while (*ip) {
        if (*ip == '\\') {
            ++ip;
            if (!backsub(prog, name)) goto macsub_err;
        } else if (is_end_of_statement(ip) || is_end_of_cmdsub(ip)) {
            break;
        } else if (*ip == '/') {
            ++ip;
            if (!slashsub(prog, name)) goto macsub_err;
        } else if (*ip == '}') {
            /* note: in case of "%{var-$mac}", we break even if !bracket. */
            /* Users shouldn't use '}' in macro names anyway. */
            break;
        } else if (!bracket && is_space(*ip)) {
            break;
        } else if (*ip == '$') {
            if (ip[1] == '$') {
                while(*++ip == '$') Stringadd(name, *ip);
            } else {
                if (!bracket) break;
                else Stringadd(name, *ip++);
            }
        } else if (*ip == '%') {
	    dynamic++;
            ++ip;
            if (!percentsub(prog, SUB_FULL, &name)) goto macsub_err;
        } else {
            for (s = ip++; *ip && !is_punct(*ip) && !is_space(*ip); ip++);
            SStringoncat(name, prog->src, s - prog->src->data, ip - s);
        }
    }
    if (bracket) {
        if (*ip != '}') {
            eprintf("unmatched ${");
            goto macsub_err;
        } else ip++;
    } else if (*ip == '$') {
        ip++;
    }

#if 0 /* XXX optimize */
    if (dynamic) {
#endif
	code_add(prog, OP_APPEND, name);
	name = NULL;
#if 0
    } else {
	prog->len -= 2;
    }
#endif
    code_add(prog, in_expr ? OP_PMAC : OP_AMAC, name);

    return 1;

macsub_err:
    Stringfree(name);
    return 0;
}

static int backsub(Program *prog, String *dest)
{
    if (is_digit(*ip)) {
        char c = strtochr(ip, &ip);
        Stringadd(dest, mapchar(c));
    } else if (!backslash) {
        Stringadd(dest, '\\');
    } else if (*ip) {
        Stringadd(dest, *ip++);
    }
    return 1;
}

int varsub(Program *prog, int sub_warn, int in_expr)
{
    int result = 0;
    const char *start, *contents;
    int bracket, except = FALSE, ell = FALSE, pee = FALSE, star = FALSE, n = -1;
    String *selector;
    String *dest = NULL;
    static int sub_warned = 0;

    (selector = Stringnew(NULL, 0, 0))->links++;

#if 0
    if (!in_expr) {
	if (*ip == '%') {
	    while (*ip == '%') Stringadd(dest, *ip++);
	    result = 1;
	    goto varsub_exit;
	}
	if (!*ip || is_space(*ip)) {
	    Stringadd(dest, '%');
	    result = 1;
	    goto varsub_exit;
	}
    }
#endif

    contents = ip;
    if ((bracket = (*ip == '{'))) ip++;

    if (ip[0] == '#' && (!bracket || ip[1] == '}')) {
        ++ip;
	code_add(prog, in_expr ? OP_PSPECIAL : OP_ASPECIAL, '#');

    } else if (ip[0] == '?' && (!bracket || ip[1] == '}')) {
        ++ip;
	code_add(prog, in_expr ? OP_PSPECIAL : OP_ASPECIAL, '?');

    } else {
        if (is_digit(*ip)) {
            start = ip;
            n = strtoint(ip, &ip);
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
                n = strtoint(ip, &ip);
            }
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
        Stringtrunc(selector, 0);
        if ((n < 0 && !star) || (bracket && (ell || pee))) {
            /* is non-special, or could be non-special if followed by alnum_ */
            if (is_alnum(*ip) || (*ip == '_')) {
                ell = pee = FALSE;
                n = -1;
                do ip++; while (is_alnum(*ip) || *ip == '_');
                Stringncpy(selector, start, ip - start);
            }
        }

	if (star) {
	    code_add(prog, in_expr ? OP_PSPECIAL : OP_ASPECIAL, '*');
	} else if (pee) {
	    if (n < 0) n = 1;
	    code_add(prog, in_expr ? OP_PREG : OP_AREG, n);
	    n = -1;
	} else if (ell) {
	    if (n < 0) n = 1;
	    code_add(prog, in_expr ? OP_PLPARM : OP_ALPARM, except ? -n : n);
	} else if (n > 0) {
	    code_add(prog, in_expr ? OP_PPARM : OP_APARM, except ? -n : n);
	} else if (n == 0) {
	    code_add(prog, in_expr ? OP_PSPECIAL : OP_ASPECIAL, '0');
	} else if (cstrcmp(selector->data, "R") == 0) {
	    code_add(prog, in_expr ? OP_PSPECIAL : OP_ASPECIAL, 'R');
	} else if (cstrcmp(selector->data, "PL") == 0) {
	    code_add(prog, in_expr ? OP_PREG : OP_AREG, -1);
	} else if (cstrcmp(selector->data, "PR") == 0) {
	    code_add(prog, in_expr ? OP_PREG : OP_AREG, -2);
	} else {
	    code_add(prog, in_expr ? OP_PVAR : OP_AVAR, selector);
	    selector->links++;
	}
    }

    if (*ip == '-') {
	int jump_point;
        ++ip;
	code_add(prog, OP_JNEMPTY, -1);
	jump_point = prog->len - 1;
	if (in_expr) {
	    code_add(prog, OP_POP);
	    code_add(prog, OP_PUSHBUF, NULL);
	}
	(dest = Stringnew(NULL, 0, 0))->links++;
        while (*ip) {
            if (is_end_of_statement(ip) || is_end_of_cmdsub(ip)) {
                break;
            } else if (bracket && *ip == '}') {
                break;
            } else if (!bracket && is_space(*ip)) {
                break;
            } else if (*ip == '%') {
                ++ip;
                if (!percentsub(prog, SUB_FULL, &dest)) goto varsub_exit;
            } else if (*ip == '$') {
                ++ip;
                if (!dollarsub(prog, &dest)) goto varsub_exit;
            } else if (*ip == '/') {
                ++ip;
                if (!slashsub(prog, dest)) goto varsub_exit;
            } else if (*ip == '\\') {
		if (ip[1] == '\n') {
		    eat_newline(prog);
		} else {
		    ++ip;
		    if (!backsub(prog, dest)) goto varsub_exit;
		}
            } else {
                for (start = ip++; *ip && is_alnum(*ip); ip++);
		SStringoncat(dest, prog->src, start - prog->src->data,
		    ip - start);
            }
        }
	if (dest->len) {
	    code_add(prog, OP_APPEND, dest);
	    dest = NULL;
	}
	if (in_expr) {
	    code_add(prog, OP_PBUF, NULL);
	}
	code_add(prog, OP_NOP, NULL);
	comefrom(prog, jump_point, prog->len - 1);
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
        eprintf("warning: \"%%%.*s\" substitution in expression is legal, "
	    "but can be confusing.  Try using \"{%.*s}\" instead.",
            ip-contents, contents,
            (ip-bracket)-(contents+bracket), contents+bracket);
    }
varsub_exit:
    if (dest) Stringfree(dest);
    Stringfree(selector);
    return result;
}

struct Value *handle_shift_command(String *args, int offset)
{
    int count;
    int error;

    count = (args->len - offset) ? atoi(args->data + offset) : 1;
    if (count < 0) return shareval(val_zero);
    if ((error = (count > tf_argc))) count = tf_argc;
    tf_argc -= count;
    if (tf_argv) {  /* true if macro was called as command, not as function */
        tf_argv += count;
    }
    return newint(!error);
}

#if USE_DMALLOC
void free_expand()
{
    freeval(user_result);
    freeval(val_one);
    freeval(val_zero);
}
#endif

