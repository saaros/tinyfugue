/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003, 2004, 2005 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: variable.c,v 35004.106 2005/04/18 03:15:36 kkeys Exp $";


/**************************************
 * Internal and environment variables *
 **************************************/

#include "tfconfig.h"
#include "port.h"
#include "tf.h"
#include "util.h"
#include "pattern.h"
#include "search.h"
#include "tfio.h"
#include "output.h"
#include "attr.h"
#include "socket.h"	/* tog_bg(), tog_lp() */
#include "cmdlist.h"
#include "process.h"	/* runall() */
#include "expand.h"	/* SUB_KEYWORD */
#include "parse.h"	/* types */
#include "variable.h"

static const char *varchar(Var *var);
static Var   *findlevelvar(const char *name, List *level);
static Var   *findlocalvar(const char *name);
static int    set_special_var(Var *var, int type, void *value,
                       int funcflag, int exportflag);
static void   set_env_var(Var *var, int exportflag);
static Var   *newvar(const char *name, int type, void *value);
static char  *new_env(Var *var);
static void   replace_env(char *str);
static void   append_env(char *str);
static char **find_env(const char *str);
static void   remove_env(const char *str);
static int    listvar(const char *name, const char *value,
                       int mflag, int exportflag, int shortflag);
static int    obsolete_prompt(Var *var);
static int    undocumented_var(Var *var);
static void   init_special_variable(Var *var, const char *cval,
                       long ival, long uval);

#define hfindglobalvar(name, hash) \
	(Var*)hashed_find(name, hash, var_table)
#define findglobalvar(name) \
	(Var*)hashed_find(name, hash_string(name), var_table)

#define HASH_SIZE 997    /* prime number */

static List localvar[1];          /* local variables */
static HashTable var_table[1];    /* global variables */
static int envsize;
static int envmax;
static int setting_nearest = 0;

#define bicode(a, b)  b 
#include "enumlist.h"

static conString enum_mecho[]	= {
    STRING_LITERAL("off"),
    STRING_LITERAL("on"),
    STRING_LITERAL("all"),
    STRING_NULL };
static conString enum_block[] = {
    STRING_LITERAL("blocking"),
    STRING_LITERAL("nonblocking"),
    STRING_NULL };

conString enum_off[]	= {
    STRING_LITERAL("off"),
    STRING_NULL };
conString enum_flag[]	= {
    STRING_LITERAL("off"),
    STRING_LITERAL("on"),
    STRING_NULL };
conString enum_sub[]	= {
    STRING_LITERAL("off"),
    STRING_LITERAL("on"),
    STRING_LITERAL("full"),
    STRING_NULL };

extern char **environ;

#define VARSET     0001	/* has a value */
#define VARSPECIAL 0002	/* has special meaning to tf */
#define VAREXPORT  0004	/* exported to environment */
#define VARAUTOX   0010	/* automatically exported to environment */


/* Special variables. */
Var special_var[] = {
#define varcode(id, name, sval, type, flags, enums, ival, uval, func) \
    {{ name, type, 1, NULL }, flags|VARSPECIAL, enums, func, NULL, 0 },
#include "varlist.h"
#undef varcode
    {{ NULL, 0, 1, NULL }, 0, NULL, NULL, NULL, 0 }
};

Pattern looks_like_special_sub;    /* looks like a special substitution */
Pattern looks_like_special_sub_ic; /* same, ignoring case */

static inline Var *newglobalvar(const char *name, int type, void *value)
{
    Var *var;
    var = newvar(name, type, value);
    var->node = hash_insert((void*)var, var_table);
    return var;
}

static void init_special_variable(Var *var,
    const char *cval, long ival, long uval)
{
    var->val.sval = NULL;
    var->flags |= VARSET;
    switch (var->val.type & TYPES_BASIC) {
    case TYPE_STR:
        if (cval)
	    (var->val.sval = CS(Stringnew(cval, -1, 0)))->links++;
	else
	    var->flags &= ~VARSET;
        break;
    case TYPE_ENUM:
        var->val.sval = &var->enumvec[ival >= 0 ? ival : 0];
        var->val.sval->links++;
        /* fall through */
    case TYPE_INT:
    case TYPE_POS:
        var->val.u.ival = ival;
        break;
    case TYPE_DTIME:
    case TYPE_ATIME:
        var->val.u.tval.tv_sec = ival;
        var->val.u.tval.tv_usec = uval;
        break;
    default:
        break;
    }
    var->node = hash_insert((void *)var, var_table);
}

/* initialize structures for variables */
void init_variables(void)
{
    char **oldenv, **p, *str, *cvalue;
    conString *svalue;
    const char *oldcommand;
    Var *var;

    init_hashtable(var_table, HASH_SIZE, strstructcmp);
    init_list(localvar);

    init_pattern(&looks_like_special_sub,
	"(?-i)^(R|P[LR]|L\\d*|P\\d+)$", MATCH_REGEXP);
    init_pattern(&looks_like_special_sub_ic,
	 "(?i)^(R|P[LR]|L\\d*|P\\d+)$", MATCH_REGEXP);

    var = special_var;
#define varcode(id, name, sval, type, flags, enums, ival, uval, func) \
    init_special_variable(var++, (sval), (ival), (uval));
#include "varlist.h"
#undef varcode

    /* environment variables */
    oldcommand = current_command;
    current_command = "[environment]";
    for (p = environ; *p; p++);
    envsize = 0;
    envmax = p - environ;
    oldenv = environ;
    environ = (char **)XMALLOC((envmax + 1) * sizeof(char *));
    *environ = NULL;
    for (p = oldenv; *p; p++) {
        append_env(str = STRDUP(*p));
        /* There should always be an '=', but some shells (zsh?) violate this.*/
        cvalue = strchr(str, '=');
        if (cvalue) *cvalue++ = '\0';
        var = findglobalvar(str);
	/* note: Stringnew(cvalue,-1,0) would create a non-resizeable string */
        svalue = cvalue ? CS(Stringcpy(Stringnew(NULL,-1,0), cvalue)) : blankline;
        svalue->links++;
        if (!var) {
            /* new global variable */
            var = newglobalvar(str, TYPE_STR, svalue);
        } else if (var->flags & VARSPECIAL) {
            /* overwrite a pre-defined special variable */
            set_special_var(var, TYPE_STR, svalue, 0, 0);
            /* We do NOT call the var->func here, because the modules they
             * reference have not been initialized yet.  The init_*() calls
             * in main.c should call the funcs in the appropraite order.
             */
	} else {
	    /* Shouldn't happen unless environment contained same name twice */
	    set_var_direct(var, TYPE_STR, svalue);
	    if (!var->node) var->node = hash_insert((void *)var, var_table);
        }
        if (cvalue) *--cvalue = '=';  /* restore '=' */
        var->flags |= VAREXPORT;
        conStringfree(svalue);
    }
    current_command = oldcommand;
}

void pushvarscope(struct List *level)
{
    init_list(level);
    inlist((void *)level, localvar, NULL);
}

void popvarscope(void)
{
    List *level;
    Var *var;

    level = (List *)unlist(localvar->head, localvar);
    while (level->head) {
        var = (Var *)unlist(level->head, level);
	assert(var->val.count == 1);
	clearval(&var->val);
	FREE(var->val.name);
	var->val.name = NULL;
	FREE(var);
    }
}

static Var *findlevelvar(const char *name, List *level)
{
    ListEntry *node;

    for (node = level->head; node; node = node->next) {
        if (strcmp(name, ((Var *)node->datum)->val.name) == 0) break;
    }
    return node ? (Var *)node->datum : NULL;
}

static Var *findlocalvar(const char *name)
{
    ListEntry *node;
    Var *var = NULL;

    for (node = localvar->head; node && !var; node = node->next) {
        var = findlevelvar(name, (List *)node->datum);
    }
    return var;
}

/* get char* value of variable */
static const char *varchar(Var *var)
{
    if (!var || !(var->flags & VARSET))
        return NULL;
    if (!var->val.sval)
        valstr(&var->val);       /* force creation of sval */
    return var->val.sval->data;
}

/* get char* value of global variable <name> */
const char *getvar(const char *name)
{
    return varchar(findglobalvar(name));
}

/* function form of findglobalvar() */
Var *ffindglobalvar(const char *name)
{
    return findglobalvar(name);
}

Var *hfindnearestvar(const Value *idval)
{
    const char *name = idval->name;
    Var *var;

    if (!(var = findlocalvar(name)) &&
	!(var = hfindglobalvar(name, idval->u.hash)))
    {
        if (patmatch(&looks_like_special_sub, NULL, name)) {
            wprintf("\"%s\" in an expression is a variable reference, "
		"and is not the same as special substitution \"{%s}\".",
		name, name);
        }
        return NULL;
    }
    return var;
}

/* returned Value is only valid for lifetime of variable! */
Value *getvarval(Var *var)
{
    return (var && (var->flags & VARSET)) ? &var->val : NULL;
}

/* returned Value is only valid for lifetime of variable! */
Value *hgetnearestvarval(const Value *idval)
{
    Var *var;
    var = hfindnearestvar(idval);
    return (var && (var->flags & VARSET)) ? &var->val : NULL;
}

Var *hsetnearestvar(const Value *idval, int type, void *value)
{
    Var *var;
    const char *name = idval->name;
    unsigned int hash = idval->u.hash;

    if ((var = findlocalvar(name))) {
        set_var_direct(var, type, value);
    } else {
        setting_nearest++;
        var = setvar(NULL, name, hash, type, value, FALSE);
        setting_nearest--;
    }
    return var;
}

static Var *newvar(const char *name, int type, void *value)
{
    Var *var;

    var = (Var *)XMALLOC(sizeof(Var));
    var->node = NULL;
    var->val.type = 0;
    var->val.count = 1;
    var->val.name = NULL;
    var->val.sval = NULL;
    var->flags = 0;
    var->enumvec = NULL; /* never used */
    var->func = NULL;
    var->statuses = 0;
    var->statusfmts = 0;
    var->statusattrs = 0;
    set_var_direct(var, type, value);
    var->val.name = STRDUP(name);

    if (patmatch(&looks_like_special_sub, NULL, name)) {
	wprintf("\"%s\" conflicts with the name of a special "
	    "substitution, so it will not be accessible with a \"%%%s\" or "
	    "\"{%s}\" substitution.", name, name, name);
    }

    return var;
}

/* Sets a variable in the current level, creating the variable if necessary. */
Var *setlocalvar(const char *name, int type, void *value)
{
    Var *var, *global;

    if ((var = findlevelvar(name, (List *)localvar->head->datum))) {
        set_var_direct(var, type, value);
    } else {
        var = newvar(name, type, value);
        inlist((void *)var, (List*)(localvar->head->datum), NULL);
        if ((global = findglobalvar(name)) && (global->flags & VARSET)) {
            do_hook(H_SHADOW, "!Warning:  Local variable \"%s\" overshadows global variable of same name.", "%s", name);
        }
    }
    return var;
}


/*
 * Environment routines.
 */

/* create new environment string */
static char *new_env(Var *var)
{
    char *str;
    conString *value;

    value = valstr(&var->val);
    str = (char *)XMALLOC(strlen(var->val.name) + 1 + value->len + 1);
    sprintf(str, "%s=%s", var->val.name, value->data);
    return str;
}

/* Replace the environment string for <name> with "<name>=<value>". */
static void replace_env(char *str)
{
    char **envp;

    envp = find_env(str);
    FREE(*envp);
    *envp = str;
}

/* Add str to environment.  Assumes name is not already defined. */
/* str must be duped before call */
static void append_env(char *str)
{
    if (envsize == envmax) {
        envmax += 5;
        environ = (char **)XREALLOC((char*)environ, (envmax+1) * sizeof(char*));
    }
    environ[envsize] = str;
    environ[++envsize] = NULL;
}

/* Find the environment string for <name>.  str can be in "<name>" or
 * "<name>=<value>" format.
 */
static char **find_env(const char *str)
{
    char **envp;
    int len;

    for (len = 0; str[len] && str[len] != '='; len++);
    for (envp = environ; *envp; envp++)
        if (strncmp(*envp, str, len) == 0 && (*envp)[len] == '=')
            return envp;
    return NULL;
}

/* Remove the environment string for <name>. */
static void remove_env(const char *str)
{
    char **envp;

    envp = find_env(str);
    FREE(*envp);
    do *envp = *(envp + 1); while (*++envp);
    envsize--;
}


/* set type and value directly into variable */
void set_var_direct(Var *var, int type, void *value)
{

    if (value == &var->val.u) {
	/* self assignment:  if we didn't catch this, the clearval of the
	 * lhs would clobber the rhs */
	return;
    }

    assert(var->val.count == 1);
    clearval(&var->val);

    var->flags |= VARSET;
    type &= TYPES_BASIC;
    switch (type) {
    case TYPE_STR:
	if (value)
	    (var->val.sval = (conString*)value)->links++;
	else
	    var->flags &= ~VARSET;
	var->val.u.p = NULL;
        break;
    case TYPE_INT:
    case TYPE_POS:
        var->val.u.ival = *(int*)value;
        var->val.sval = NULL;
        break;
    case TYPE_DTIME:
    case TYPE_ATIME:
        var->val.u.tval = (*(struct timeval*)value);
        var->val.sval = NULL;
        break;
    case TYPE_FLOAT:
        var->val.u.fval = *(double*)value;
        var->val.sval = NULL;
        break;
    default:
        internal_error(__FILE__, __LINE__, "impossible type %d", type);
        break;
    }
    var->val.type = type;
}

static void set_env_var(Var *var, int exportflag)
{
    if (var->flags & VAREXPORT) {
        replace_env(new_env(var));
    } else if (exportflag || var->flags & VARAUTOX) {
        append_env(new_env(var));
        var->flags |= VAREXPORT;
    }
}

/*
 * Interfaces with rest of program.
 */

Var *setvar(Var *var, const char *name, unsigned int hash, int type,
    void *value, int exportflag)
{
    return (var || (var = hfindglobalvar(name, hash))) ?
	setexistingvar(var, type, value, exportflag) :
	setnewvar(name, hash, type, value, exportflag);
}

Var *setnewvar(const char *name, unsigned int hash, int type,
    void *value, int exportflag)
{
    Var *var;
    var = newglobalvar(name, type, value);
    if (setting_nearest && pedantic) {
	wprintf("variable '%s' was not previously defined in any "
	    "scope, so it has been created in the global scope.", name);
    }
    set_env_var(var, exportflag);

    if (var->statuses || var->statusfmts || var->statusattrs)
        update_status_field(var, -1);

    return var;
}

/* var already exists, and may be global or local */
Var *setexistingvar(Var *var, int type, void *value, int exportflag)
{
    if (var->flags & VARSPECIAL) {
        if (!set_special_var(var, type, value, 1, exportflag))
            return NULL;
    } else {
	set_var_direct(var, type, value);
	set_env_var(var, exportflag);
    }

    if (var->statuses || var->statusfmts || var->statusattrs)
        update_status_field(var, -1);

    return var;
}

/* returns pointer to first character past valid variable name */
char *spanvar(const char *start)
{
    const char *p;
    /* Restrict variable names to alphanumerics and underscores, like POSIX.2 */
    for (p = start; *p; p++) {
        if (!(is_alpha(*p) || *p == '_' || (p != start && is_digit(*p))))
            return (char *)p;
    }
    return (char *)p;
}

/* input: *pp points to first char after var name in /set.
 * Advances *pp to first char of value.
 * Returns -1 if invalid, 0 if no value, 1 if valid value.
 */
int setdelim(const char **pp)
{
    if (!**pp) {
	return 0; /* no value */
    } else if (is_space(**pp)) {
	do { (*pp)++; } while (is_space(**pp));
	if (!**pp)
	    return 0; /* no value */
	if (**pp == '=')
	    wprintf("'=' following space is part of value.");
	return 1; /* valid value */
    } else if (**pp == '=') {
	(*pp)++;
	return 1; /* valid value */
    } else {
	return -1; /* invalid */
    }
}

int do_set(const char *name, unsigned int hash, conString *value, int offset,
    int exportflag, int localflag)
{
    String *svalue;
    int result;

    (svalue = Stringodup(value, offset))->links++;
    if (!localflag) {
        result = !!setvar(NULL, name, hash, TYPE_STR, svalue, exportflag);
    } else if (!localvar->head) {
        eprintf("illegal at top level.");
        result = 0;
    } else {
        setlocalvar(name, TYPE_STR, svalue);
        result = 1;
    }
    Stringfree(svalue);
    return result;
}

/* Note: "/set var=value" is handled by OP_SET if "var" is compile-time const */
int command_set(String *args, int offset, int exportflag, int localflag)
{
    char *p, *name = args->data + offset;
    const char *val;
    int foundval;

    if (!*name) {
        if (localflag) return 0;
	return listvar(NULL, NULL, MATCH_SIMPLE, exportflag, 0);
    }

    val = p = spanvar(name);
    foundval = setdelim(&val);

    if (foundval == -1) { /* invalid */
	while (*p && !is_space(*p) && *p != '=') p++;
	*p = '\0';
	eprintf("illegal variable name: %s", name);
	return 0;
    }

    *p = '\0'; /* mark end of name */
    if (foundval == 0) { /* no value */
	Var *var = localflag ? findlocalvar(name) : findglobalvar(name);
        if (!var || !(var->flags & VARSET)) {
            oprintf("%% %s not set %sally", name, localflag ? "loc" : "glob");
            return 0;
        }
	if (!var->val.sval) valstr(&var->val);
	oprintf("%% %s=%S", name, var->val.sval);
	return 1;

    } else { /* valid value */
	return do_set(name, hash_string(name), CS(args), val - args->data,
	    exportflag, localflag);
    }
}

struct Value *handle_listvar_command(String *args, int offset)
{
    int mflag, opt;
    int exportflag = -1, error = 0, shortflag = 0;
    char *name = NULL, *cvalue = NULL;
    const char *ptr;

    mflag = matching;

    startopt(CS(args), "m:gxsv");
    while ((opt = nextopt(&ptr, NULL, NULL, &offset))) {
        switch (opt) {
            case 'm':
                error = ((mflag = enum2int(ptr, 0, enum_match, "-m")) < 0);
                break;
            case 'g':
                exportflag = 0;
                break;
            case 'x':
                exportflag = 1;
                break;
            case 's':
                shortflag = 1;
                break;
            case 'v':
                shortflag = 2;
                break;
            default:
                return shareval(val_zero);
          }
    }
    if (error) return shareval(val_zero);

    if (args->len - offset) {
        name = args->data + offset;
        for (cvalue = name; *cvalue && !is_space(*cvalue); cvalue++);
        if (*cvalue) {
            *cvalue++ = '\0';
            while(is_space(*cvalue)) cvalue++;
        }
        if (!*cvalue)
            cvalue = NULL;
    }

    return newint(listvar(name, cvalue, mflag, exportflag, shortflag));
}

struct Value *handle_export_command(String *args, int offset)
{
    Var *var;

    if (!(var = findglobalvar(args->data + offset))) {
        eprintf("%s not defined.", args->data + offset);
        return shareval(val_zero);
    }
    if (!(var->flags & VAREXPORT)) append_env(new_env(var));
    var->flags |= VAREXPORT;
    return shareval(val_one);
}

int unsetvar(Var *var)
{
    Toggler *func = NULL;

    if (!(var->flags & VARSET)) return 1;

    if (var->val.type == TYPE_POS) {
        eprintf("%s must be a positive integer, so can not be unset.",
            var->val.name);
        return 0;
    }

    var->flags &= ~VARSET;
    if (var->flags & VAREXPORT) {
	remove_env(var->val.name);
        var->flags &= ~VAREXPORT;
    }

    if (var->func) {
	switch (var->val.type & TYPES_BASIC) {
	case TYPE_STR:   if (var->val.sval->len > 0) func = var->func; break;
	case TYPE_ENUM:  /* fall through */
	case TYPE_POS:   /* fall through */
	case TYPE_INT:   if (var->val.u.ival != 0) func = var->func; break;
	case TYPE_FLOAT: if (var->val.u.fval != 0.0) func = var->func; break;
	case TYPE_ATIME: /* fall through */
	case TYPE_DTIME:
	    if (var->val.u.tval.tv_sec || var->val.u.tval.tv_usec)
		func = var->func;
	    break;
	default:
	    internal_error(__FILE__, __LINE__, "bad type %d", var->val.type);
	    break;
	}
    }
    clearval(&var->val);
    if (var->statuses || var->statusfmts || var->statusattrs)
        update_status_field(var, -1);
    if (func)
	(*func)(var);
    freevar(var);

    return 1;
}

struct Value *handle_unset_command(String *args, int offset)
{
    Var *var;

    if (!(var = findglobalvar(args->data + offset)))
	return shareval(val_zero);
    return unsetvar(var) ? shareval(val_one) : shareval(val_zero);
}

void freevar(Var *var)
{
    if (var->flags & (VARSET | VARSPECIAL) || var->statuses ||
	var->statusfmts || var->statusattrs)
	    return;
    hash_remove(var->node, var_table);
    FREE(var->val.name);
    FREE(var);
}

/*********/

/* Set a special variable, with proper coersion of the value. */
static int set_special_var(Var *var, int type, void *value, int funcflag,
    int exportflag)
{
    Value oldval;
    long ival;
    struct timeval tval;

    oldval = var->val;
    var->val.type = 0;
    var->val.sval = NULL;
    var->val.u.ival = 0;

    oflush();   /* flush buffer now, in case variable affects flushing */

    switch (oldval.type & TYPES_BASIC) {
    case TYPE_ENUM:
	var->val.type = TYPE_ENUM;
        switch (type & TYPES_BASIC) {
        case TYPE_STR:
            var->val.u.ival = enum2int(((conString*)value)->data, 0,
		var->enumvec, var->val.name);
            break;
        case TYPE_INT:
        case TYPE_POS:
            var->val.u.ival = enum2int(NULL, *(long*)value,
                var->enumvec, var->val.name);
            break;
        case TYPE_ATIME:
        case TYPE_DTIME:
            var->val.u.ival = enum2int(NULL,
		(long)((struct timeval*)value)->tv_sec,
                var->enumvec, var->val.name);
            break;
        case TYPE_FLOAT:
            var->val.u.ival = enum2int(NULL, (long)*(double*)value,
                var->enumvec, var->val.name);
            break;
	default:
	    internal_error(__FILE__, __LINE__, "invalid set type %d", type);
	    goto error;
        }
        if (var->val.u.ival < 0)
	    goto error;
        (var->val.sval = &var->enumvec[var->val.u.ival])->links++;
        funcflag = funcflag & (var->val.u.ival != oldval.u.ival);
        break;

    case TYPE_STR:
        set_var_direct(var, type, value);
        /* force var back to its correct type */
        if (!var->val.sval) valstr(&var->val);
        var->val.type = TYPE_STR;
        break;

    case TYPE_POS:  /* must be > 0 */
    case TYPE_INT:  /* must be >= 0 (for Vars.  Values in expr.c can be <0.) */
        set_var_direct(var, type, value);
        /* force var back to its correct type */
        ival = valint(&var->val);
	clearval(&var->val);
        var->val.type = oldval.type & TYPES_BASIC;
        var->val.u.ival = ival;
        /* validate */
        if (var->val.u.ival < (var->val.type==TYPE_POS)) {
            eprintf("%s must be an integer greater than or equal to %d",
                var->val.name, (var->val.type == TYPE_POS));
	    goto error;
        }
        funcflag = funcflag & (var->val.u.ival != oldval.u.ival);
        break;

    case TYPE_ATIME:
    case TYPE_DTIME:
        set_var_direct(var, type, value);
        /* force var back to its correct type */
        valtime(&tval, &var->val);
	clearval(&var->val);
        var->val.type = oldval.type & TYPES_BASIC;
        var->val.u.tval = tval;
        funcflag = funcflag & timercmp(&var->val.u.tval, &oldval.u.tval, ==);
        break;

    default:
	internal_error(__FILE__, __LINE__, "invalid var type %d", oldval.type);
	goto error;
    }

    var->flags |= VARSET;
    if (!var->node) var->node = hash_insert((void *)var, var_table);
    set_env_var(var, exportflag);

    if (funcflag && var->func) {
        if (!(*var->func)(var)) {
            /* restore old value and call func again */
	    /* (BUG: doesn't un-export or un-set) */
	    assert(var->val.count == 1);
	    clearval(&var->val);
	    var->val = oldval;
            (*var->func)(var);
            return 0;
        }
    }

    assert(oldval.count == 1);
    clearval(&oldval);
    return 1;

error:
    /* Clear new value and restore old value. */
    /* (BUG: doesn't un-export or un-set) */
    assert(var->val.count == 1);
    clearval(&var->val);
    var->val = oldval;
    return 0;
}


static int listvar(
    const char *name,
    const char *value,
    int mflag,
    int exportflag,  /* 1 exported; 0 global; -1 both */
    int shortflag)
{
    int i;
    ListEntry *node;
    Var *var;
    Pattern pname, pvalue;
    Vector vars = vector_init(1024);

    init_pattern_str(&pname, NULL);
    init_pattern_str(&pvalue, NULL);

    if (name)
        if (!init_pattern(&pname, name, mflag))
            goto listvar_end;

    if (value)
        if (!init_pattern(&pvalue, value, mflag))
            goto listvar_end;

    /* collect matching variables */
    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            for (node = var_table->bucket[i]->head; node; node = node->next) {
                var = (Var*)node->datum;
                if (!(var->flags & VARSET)) continue;
                if (exportflag >= 0 && !(var->flags & VAREXPORT) != !exportflag)                    continue;
                if (name && !patmatch(&pname, NULL, var->val.name)) continue;
                if (!var->val.sval) valstr(&var->val); /* force sval to exist */
                if (value && !patmatch(&pvalue, var->val.sval, NULL)) continue;
		vector_add(&vars, node->datum);
            }
        }
    }

    vector_sort(&vars, strpppcmp);

    for (i = 0; i < vars.size; i++) {
	var = vars.ptrs[i];
	switch (shortflag) {
	case 1:  oputs(var->val.name);     break;
	case 2:  oputline(var->val.sval);  break;
	default:
	    oprintf("/set%s %s=%S",
		(var->flags & VAREXPORT) ? "env" : "",
		var->val.name, var->val.sval);
	}
    }
    vector_free(&vars);

listvar_end:
    free_pattern(&pname);
    free_pattern(&pvalue);
    return vars.size;
}

static int obsolete_prompt(Var *var)
{
    wprintf("%s is obsolete.  Use prompt_wait instead.", var->val.name);
    return 1;
}

static int undocumented_var(Var *var)
{
    wprintf("%s is undocumented, and may be removed in a future version.",
	var->val.name);
    return 1;
}

#if USE_DMALLOC
void free_vars(void)
{
    char **p;
    int i;
    Var *var;

    free_pattern(&looks_like_special_sub);
    free_pattern(&looks_like_special_sub_ic);

    for (p = environ; *p; p++) FREE(*p);
    FREE(environ);

    for (i = 0; i < NUM_VARS; i++) {
        if (special_var[i].node) hash_remove(special_var[i].node, var_table);
        clearval(&special_var[i].val);
	/* var and var->val.name are static, can't be freed */
    }

    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            while (var_table->bucket[i]->head) {
                var = (Var *)unlist(var_table->bucket[i]->head,
		    var_table->bucket[i]);
		clearval(&var->val);
		FREE(var->val.name);
                FREE(var);
            }
        }
    }
    free_hash(var_table);
}
#endif

