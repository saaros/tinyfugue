/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.c,v 35004.26 1997/10/18 21:55:53 hawkeye Exp $ */


/**************************************
 * Internal and environment variables *
 **************************************/

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "output.h"
#include "socket.h"	/* tog_bg(), tog_lp() */
#include "search.h"
#include "commands.h"
#include "process.h"	/* runall() */
#include "expand.h"	/* SUB_KEYWORD */
#include "variable.h"

static Var   *FDECL(findlevelvar,(CONST char *name, List *level));
static Var   *FDECL(findlocalvar,(CONST char *name));
static int    FDECL(set_special_var,(Var *var, CONST char *val, Toggler **fpp, CONST char **oldvaluep));
static Var   *FDECL(newglobalvar,(CONST char *name, CONST char *value));
static char  *FDECL(new_env,(Var *var));
static void   FDECL(append_env,(char *str));
static char **FDECL(find_env,(CONST char *str));
static void   FDECL(remove_env,(CONST char *str));
static void   FDECL(replace_env,(char *str));
static int    FDECL(listvar,(CONST char *name, CONST char *value,
                    int mflag, int exportflag, int shortflag));

#define findglobalvar(name)   (Var *)hash_find(name, var_table)
#define findspecialvar(name) \
        (Var *)binsearch((GENERIC*)(name), (GENERIC*)special_var, NUM_VARS, \
            sizeof(Var), strstructcmp)

#define HASH_SIZE 197    /* prime number */

static List localvar[1];          /* local variables */
static HashTable var_table[1];    /* global variables */
static int envsize;
static int envmax;

#define bicode(a, b)  b 
#include "enumlist.h"
#undef bicode

static CONST char *enum_flag[]	= { "off", "on", NULL };
static CONST char *enum_mecho[]	= { "off", "on", "all", NULL };
static CONST char *enum_block[] = { "blocking", "nonblocking", NULL };

CONST char *enum_sub[]	= { "off", "on", "full", NULL };
CONST char *enum_color[]= { "black", "red", "green", "yellow",
			"blue", "magenta", "cyan", "white",
			"8", "9", "10", "11", "12", "13", "14", "15",
			"bgblack", "bgred", "bggreen", "bgyellow",
			"bgblue", "bgmagenta", "bgcyan", "bgwhite",
			NULL };

extern char **environ;

#define VARINT     001	/* type: nonnegative integer */
#define VARPOS     002	/* type: positive integer */
#define VARENUM    004	/* type: enumerated */
#define VARSTR     010	/* type: string */
#define VARSPECIAL 020	/* has special meaning to tf */
#define VAREXPORT  040	/* exported to environment */


/* Special variables. */
Var special_var[] = {
#define varcode(id, name, val, type, enums, ival, func) \
    { name, val, 0, type, enums, ival, func, NULL, NULL }
#include "varlist.h"
#undef varcode
};


/* initialize structures for variables */
void init_variables()
{
    char **oldenv, **p, *value, buf[20], *str;
    extern CONST char *current_command;
    CONST char *oldcommand;
    Var *var;
    Stringp scratch;

    init_hashtable(var_table, HASH_SIZE, strstructcmp);
    init_list(localvar);

    /* special pre-defined variables */
    for (var = special_var; var->name; var++) {
        var->flags |= VARSPECIAL;
        if (var->flags & VARSTR) {
            if (var->value) {
                var->len = strlen(var->value);
                var->value = STRNDUP(var->value, var->len);
            }
            var->ival = !!var->value;
        } else if (var->flags & VARENUM) {
            var->value = var->enumvec[var->ival >= 0 ? var->ival : 0];
            var->len = strlen(var->value);
            var->value = STRNDUP(var->value, var->len);
        } else /* integer */ {
            sprintf(buf, "%ld", var->ival);
            var->len = strlen(buf);
            var->value = STRNDUP(buf, var->len);
        }
        if (var->value)
            var->node = hash_insert((GENERIC *)var, var_table);
    }

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
        value = strchr(str, '=');
        if (value) *value++ = '\0';
        var = findspecialvar(str);
        if (!var) {
            /* new variable */
            var = newglobalvar(str, value ? value : "");
            var->node = hash_insert((GENERIC*)var, var_table);
        } else {
            /* overwrite a pre-defined special variable */
            set_special_var(var, value ? value : "", NULL, NULL);
            /* We do NOT call the var->func here, because the modules they
             * reference have not been initialized yet.  The init_*() calls
             * in main.c should call the funcs in the appropraite order.
             */
            if (!var->node) var->node = hash_insert((GENERIC *)var, var_table);
        }
        if (value) *--value = '=';  /* restore '=' */
        var->flags |= VAREXPORT;
    }
    current_command = oldcommand;

    /* run-time default values */
    Stringinit(scratch);
    if (!findglobalvar("TFLIBRARY")) {
        Sprintf(scratch, 0, "%s/stdlib.tf", TFLIBDIR);
        setvar("TFLIBRARY", scratch->s, 0);
    }
    if (!findglobalvar("TFHELP")) {
        Sprintf(scratch, 0, "%s/tf-help", TFLIBDIR);
        setvar("TFHELP", scratch->s, 0);
    }
    Stringfree(scratch);
}

void newvarscope(level)
    struct List *level;
{
    init_list(level);
    inlist((GENERIC *)level, localvar, NULL);
}

void nukevarscope()
{
    List *level;
    Var *var;

    level = (List *)unlist(localvar->head, localvar);
    while (level->head) {
        var = (Var *)unlist(level->head, level);
        FREE(var->name);
        FREE(var->value);
        FREE(var);
    }
}

static Var *findlevelvar(name, level)
    CONST char *name;
    List *level;
{
    ListEntry *node;

    for (node = level->head; node; node = node->next) {
        if (strcmp(name, ((Var *)node->datum)->name) == 0) break;
    }
    return node ? (Var *)node->datum : NULL;
}

static Var *findlocalvar(name)
    CONST char *name;
{
    ListEntry *node;
    Var *var = NULL;

    for (node = localvar->head; node && !var; node = node->next) {
        var = findlevelvar(name, (List *)node->datum);
    }
    return var;
}

/* get value of global variable <name> */
CONST char *getvar(name)
    CONST char *name;
{
    Var *var;

    return (var = findglobalvar(name)) ? var->value : NULL;
}

/* function form of findglobalvar() */
Var *ffindglobalvar(name)
    CONST char *name;
{
    return findglobalvar(name);
}

/* If np is nonnull, ival of var will be put there. */
CONST char *getnearestvar(name, np)
    CONST char *name;
    long *np;
{
    Var *var;
    STATIC_BUFFER(buf);

    if (np) *np = 0;
    if (((var = findlocalvar(name))) || ((var = findglobalvar(name)))) {
        if (np) *np = (var->flags & VARSTR) ? 0 : var->ival;
        return var->value;
    }
    if (ucase(name[0]) == 'P') {
        Stringterm(buf, 0);
        if (isdigit(name[1]))
            return regsubstr(buf, atoi(name + 1)) >= 0 ? buf->s : NULL;
        else if (ucase(name[1]) == 'L')
            return regsubstr(buf, -1) >= 0 ? buf->s : NULL;
        else if (ucase(name[1]) == 'R')
            return regsubstr(buf, -2) >= 0 ? buf->s : NULL;
    }
    return NULL;
}

Var *setnearestvar(name, value)
    CONST char *name, *value;
{
    Var *var;

    if ((var = findlocalvar(name))) {
        if (var->value != value) {
            FREE(var->value);
            var->len = strlen(value);
            var->value = STRNDUP(value, var->len);
        }
        return var;
    } else {
        return setvar(name, value, FALSE);
    }
}

Var *newlocalvar(name, value)
    CONST char *name, *value;
{
    Var *var;

    var = (Var *)XMALLOC(sizeof(Var));
    var->node = NULL;
    var->name = STRDUP(name);
    var->len = strlen(value);
    var->value = STRNDUP(value, var->len);
    var->flags = VARSTR;
    /* var->ival = atol(value); */ /* never used */
    var->func = NULL;
    var->status = NULL;
    inlist((GENERIC *)var, (List*)(localvar->head->datum), NULL);
    if (findglobalvar(name)) {
        do_hook(H_SHADOW, "%% Warning:  Local variable \"%s\" overshadows global variable of same name.", "%s", name);
    }
    return var;
}

static Var *newglobalvar(name, value)
    CONST char *name, *value;
{
    Var *var;

    var = (Var *)XMALLOC(sizeof(Var));
    var->node = NULL;
    var->name = STRDUP(name);
    var->len = strlen(value);
    var->value = STRNDUP(value, var->len);
    var->flags = VARSTR;
    /* var->ival = atol(value); */ /* never used */
    var->func = NULL;
    var->status = NULL;
    return var;
}


/*
 * Environment routines.
 */

/* create new environment string */
static char *new_env(var)
    Var *var;
{
    char *str;

    str = (char *)XMALLOC(strlen(var->name) + 1 + var->len + 1);
    sprintf(str, "%s=%s", var->name, var->value);
    return str;
}

/* Add str to environment.  Assumes name is not already defined. */
/* str must be duped before call */
static void append_env(str)
    char *str;
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
static char **find_env(str)
    CONST char *str;
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
static void remove_env(str)
    CONST char *str;
{
    char **envp;

    envp = find_env(str);
    FREE(*envp);
    do *envp = *(envp + 1); while (*++envp);
    envsize--;
}

/* Replace the environment string for <name> with "<name>=<value>". */
static void replace_env(str)
    char *str;
{
    char **envp;

    envp = find_env(str);
    FREE(*envp);
    *envp = str;
}


/*
 * Interfaces with rest of program.
 */

Var *setvar(name, value, exportflag)
    CONST char *name, *value;
    int exportflag;
{
    Var *var;
    Toggler *func = NULL;
    CONST char *oldvalue = NULL;
    static int depth = 0;

    if ((var = findspecialvar(name))) {
        if (!set_special_var(var, value, &func, &oldvalue))
            return NULL;
    } else if ((var = findglobalvar(name))) {
        if (value != var->value) {
            FREE(var->value);
            var->len = strlen(value);
            var->value = STRNDUP(value, var->len);
        }
    } else {
        var = newglobalvar(name, value);
    }
    if (!var->node) var->node = hash_insert((GENERIC *)var, var_table);

    if (var->flags & VAREXPORT) {
        replace_env(new_env(var));
    } else if (exportflag) {
        append_env(new_env(var));
        var->flags |= VAREXPORT;
    }

    if (func && depth==0) {
        if (!(*func)()) {
            /* restore old value (bug: doesn't un-export or un-set) */
            depth++;
            setvar(name, oldvalue ? oldvalue : "", exportflag);
            depth--;
        }
    }
    if (var->status)
        update_status_field(var, -1);
    if (oldvalue)
        FREE(oldvalue);

    return var;
}

void setivar(name, value, exportflag)
    CONST char *name;
    long value;
    int exportflag;
{
    char buf[20];
    sprintf(buf, "%ld", value);
    setvar(name, buf, exportflag);
}

int do_set(args, exportflag, localflag)
    char *args;
    int exportflag, localflag;
{
    char *value;
    Var *var;
    int i;

    if (!*args) {
        if (!localflag)
            return listvar(NULL, NULL, MATCH_SIMPLE, exportflag, 0);
    }

    for (value = args + 1; ; value++) {
        if (*value == '=') {
            *value++ = '\0';
            break;
        } else if (isspace(*value)) {
            for (*value++ = '\0'; isspace(*value); value++);
            if (*value == '\0') value = NULL;
            if (*value == '=')
                eprintf("warning: '=' following space is part of value.");
            break;
        } else if (*value == '\0') {
            value = NULL;
            break;
        }
    }

    /* Restrict variable names to alphanumerics and underscores, like POSIX.2 */
    for (i = 0; args[i]; i++) {
        if (!(isalpha(args[i]) || args[i]=='_' || (i>0 && isdigit(args[i])))) {
            oputs("% illegal variable name.");
            return 0;
        }
    }

    if (!value) {
        if ((var = localflag ? findlocalvar(args) : findglobalvar(args))) {
            oprintf("%% %s=%s", args, var->value);
            return 1;
        } else {
            oprintf("%% %s not set %sally", args, localflag ? "loc" : "glob");
            return 0;
        }
    }

    if (!localflag) return !!setvar(args, value, exportflag);

    if (!localvar->head) {
        eprintf("illegal at top level.");
        return 0;
    } else if ((var = findlevelvar(args, (List *)localvar->head->datum))) {
        FREE(var->value);
        var->len = strlen(value);
        var->value = STRNDUP(value, var->len);
    } else {
        newlocalvar(args, value);
    }
    return 1;
}

int handle_listvar_command(args)
    char *args;
{
    int mflag, opt;
    int exportflag = -1, error = 0, shortflag = 0;
    char *name = NULL, *value = NULL;

    mflag = matching;

    startopt(args, "m:gxs");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
            case 'm':
                error = ((mflag = enum2int(args, enum_match, "-m")) < 0);
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
            default:
                return 0;
          }
    }
    if (error) return 0;

    if (*args) {
        name = args;
        for (value = name; *value && !isspace(*value); value++);
        if (*value) {
            *value++ = '\0';
            while(isspace(*value)) value++;
        }
        if (!*value)
            value = NULL;
    }

    return listvar(name, value, mflag, exportflag, shortflag);
}

int handle_export_command(name)
    char *name;
{
    Var *var;

    if (!(var = findglobalvar(name))) {
        eprintf("%s not defined.", name);
        return 0;
    }
    if (!(var->flags & VAREXPORT)) append_env(new_env(var));
    var->flags |= VAREXPORT;
    return 1;
}

int handle_unset_command(name)
    char *name;
{
    long oldval;
    Var *var;

    if (!(var = findglobalvar(name))) return 0;

    if (var->flags & VARPOS) {
        eprintf("%s must a positive integer, so can not be unset.", var->name);
        return 0;
    }

    hash_remove(var->node, var_table);
    if (var->flags & VAREXPORT) remove_env(name);
    FREE(var->value);

    if (var->status) {
        var->status = NULL;
        update_status_field(var, -1);
    }

    if (!(var->flags & VARSPECIAL)) {
        FREE(var->name);
        FREE(var);
    } else {
        var->flags &= ~VAREXPORT;
        var->value = NULL;
        var->node = NULL;
        oldval = var->ival;
        var->ival = 0;
        if ((oldval || var->flags & VARSTR) && var->func)
            (*var->func)();
    }
    return 1;
}

/*********/

/* Set a special variable, with proper coersion of the value. */
static int set_special_var(var, value, fpp, oldvaluep)
    Var *var;
    CONST char *value;
    Toggler **fpp;
    CONST char **oldvaluep;
{
    static char buffer[20];
    long oldival = var->ival;
    CONST char *oldvalue = var->value;

    oflush();   /* flush buffer now, in case variable affects flushing */

    if (var->flags & VARENUM) {
        if ((var->ival = enum2int(value, var->enumvec, var->name)) < 0) {
            /* error. restore values and abort. */
            var->ival = oldival;
            var->value = oldvalue;
            return 0;
        }
        value = var->enumvec[var->ival];
        if (fpp) *fpp = (var->ival != oldival) ? var->func : NULL;

    } else if (var->flags & VARSTR) {
        /* Do NOT set ival.  Some variables (hilite) have a special value
         * set by their change function; other string variables ignore it.
         */
        if (fpp) *fpp = var->func;

    } else /* integer */ {
        while (isspace(*value)) value++;
        var->ival=atol(value);
        if (!isdigit(*value) || var->ival < !!(var->flags & VARPOS)) {
            eprintf("%s must be an integer greater than or equal to %d",
                var->name, !!(var->flags & VARPOS));
            var->ival = oldival;
            if (fpp) *fpp = NULL;
            return 0;
        }
        sprintf(buffer, "%ld", var->ival=atol(value));
        value = buffer;
        if (fpp) *fpp = (var->ival != oldival) ? var->func : NULL;
    }

    var->len = strlen(value);
    var->value = STRNDUP(value, var->len);
    if (oldvaluep) {
        *oldvaluep = oldvalue;
    } else {
        if (oldvalue) FREE(oldvalue);
    }
    return 1;
}


static int listvar(name, value, mflag, exportflag, shortflag)
    CONST char *name, *value;
    int mflag;
    int exportflag;  /* 1 exported; 0 global; -1 both */
    int shortflag;
{
    int i;
    ListEntry *node;
    Var *var;
    int count = 0;
    Pattern pname, pvalue;

    init_pattern_str(&pname, NULL);
    init_pattern_str(&pvalue, NULL);

    if (name)
        if (!init_pattern(&pname, name, mflag))
            goto listvar_end;

    if (value)
        if (!init_pattern(&pvalue, value, mflag))
            goto listvar_end;

    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            for (node = var_table->bucket[i]->head; node; node = node->next) {
                var = (Var*)node->datum;
                if (exportflag >= 0 && !(var->flags & VAREXPORT) != !exportflag)                    continue;
                if (name && !patmatch(&pname, var->name)) continue;
                if (value && !patmatch(&pvalue, var->value)) continue;
                if (shortflag)
                    oprintf("%s", var->name);
                else
                    oprintf("/%s %s=%s",
                        (var->flags & VAREXPORT) ? "setenv" : "set",
                        var->name, var->value);
                count++;
            }
        }
    }

listvar_end:
    free_pattern(&pname);
    free_pattern(&pvalue);
    return count;
}

#ifdef DMALLOC
void free_vars()
{
    char **p;
    int i;
    Var *var;

    for (p = environ; *p; p++) FREE(*p);
    FREE(environ);

    for (i = 0; i < NUM_VARS; i++) {
        if (special_var[i].value) FREE(special_var[i].value);
        if (special_var[i].node) hash_remove(special_var[i].node, var_table);
    }

    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            while (var_table->bucket[i]->head) {
                var = (Var *)unlist(var_table->bucket[i]->head, var_table->bucket[i]);
                FREE(var->name);
                FREE(var->value);
                FREE(var);
            }
        }
    }
    free_hash(var_table);
}
#endif

