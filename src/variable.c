/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.c,v 35004.50 1999/01/31 00:27:58 hawkeye Exp $ */


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
static int    FDECL(set_special_var,(Var *var, long ivalue, CONST char *val,
                    Toggler **fpp, CONST char **oldvaluep));
static Var   *FDECL(newvar,(CONST char *name, CONST char *value));
static char  *FDECL(new_env,(Var *var));
static void   FDECL(replace_env,(char *str));
static void   FDECL(append_env,(char *str));
static char **FDECL(find_env,(CONST char *str));
static void   FDECL(remove_env,(CONST char *str));
static int    FDECL(listvar,(CONST char *name, CONST char *value,
                    int mflag, int exportflag, int shortflag));

#define newglobalvar(name, value)	newvar(name, value)
#define findglobalvar(name)		(Var *)hash_find(name, var_table)
#define findspecialvar(name) \
        (Var *)binsearch((GENERIC*)(name), (GENERIC*)special_var, NUM_VARS, \
            sizeof(Var), strstructcmp)

#define HASH_SIZE 197    /* prime number */

static List localvar[1];          /* local variables */
static HashTable var_table[1];    /* global variables */
static int envsize;
static int envmax;
static int setting_nearest = 0;

#define bicode(a, b)  b 
#include "enumlist.h"

static CONST char *enum_mecho[]	= { "off", "on", "all", NULL };
static CONST char *enum_block[] = { "blocking", "nonblocking", NULL };

CONST char *enum_flag[]	= { "off", "on", NULL };
CONST char *enum_sub[]	= { "off", "on", "full", NULL };
CONST char *enum_color[]= { "black", "red", "green", "yellow",
			"blue", "magenta", "cyan", "white",
			"8", "9", "10", "11", "12", "13", "14", "15",
			"bgblack", "bgred", "bggreen", "bgyellow",
			"bgblue", "bgmagenta", "bgcyan", "bgwhite",
			NULL };

extern char **environ;

#define VARINT     0001	/* type: nonnegative integer */
#define VARPOS     0002	/* type: positive integer */
#define VARENUM    0004	/* type: enumerated */
#define VARSTR     0010	/* type: string */
#define VARSPECIAL 0020	/* has special meaning to tf */
#define VAREXPORT  0040	/* exported to environment */
#define VARAUTOX   0100	/* automatically exported to environment */
#define VARSTRX    VARSTR | VARAUTOX	/* automatically exported string */


/* Special variables. */
Var special_var[] = {
#define varcode(id, name, val, type, enums, ival, func) \
    { name, val, 0, type, enums, ival, func, NULL, NULL }
    /* Some lame compilers (eg, HPUX cc) choke on  (val ? sizeof(val)-1 : 0)
     * so we have to initialize len at runtime.
     */
#include "varlist.h"
#undef varcode
};


/* initialize structures for variables */
void init_variables()
{
    char **oldenv, **p, *value, buf[20], *str;
    CONST char *oldcommand;
    Var *var;

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
            set_special_var(var, 0, value ? value : "", NULL, NULL);
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

    if (np) *np = -1;
    if (((var = findlocalvar(name))) || ((var = findglobalvar(name)))) {
        if (np && !(var->flags & VARSTR)) *np = var->ival;
        return var->value;
    }

    if (smatch("{R|L|L[1-9]*|P[RL]|P[0-9]}", name) == 0) {
        eprintf("Warning: \"%s\" may not be used as a variable reference.  Use the variable substitution \"{%s}\" instead.", name, name);
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
    } else {
        setting_nearest++;
        var = set_var_by_name(name, value, FALSE);
        setting_nearest--;
    }
    return var;
}

static Var *newvar(name, value)
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

Var *newlocalvar(name, value)
    CONST char *name, *value;
{
    Var *var;

    var = newvar(name, value);
    inlist((GENERIC *)var, (List*)(localvar->head->datum), NULL);
    if (findglobalvar(name)) {
        do_hook(H_SHADOW, "!Warning:  Local variable \"%s\" overshadows global variable of same name.", "%s", name);
    }
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

/* Replace the environment string for <name> with "<name>=<value>". */
static void replace_env(str)
    char *str;
{
    char **envp;

    envp = find_env(str);
    FREE(*envp);
    *envp = str;
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


/*
 * Interfaces with rest of program.
 */

Var *setvar(var, name, ivalue, value, exportflag)
    Var *var;
    CONST char *name, *value;
    long ivalue;
    int exportflag;
{
    Toggler *func = NULL;
    CONST char *oldvalue = NULL;
    static int depth = 0;

    if (var || (var = findspecialvar(name))) {
        if (!set_special_var(var, ivalue, value, &func, &oldvalue))
            return NULL;
    } else if ((var = findglobalvar(name))) {
        if (value != var->value) {
            FREE(var->value);
            var->len = strlen(value);
            var->value = STRNDUP(value, var->len);
        }
    } else {
        var = newglobalvar(name, value);
        if (setting_nearest && pedantic) {
            eprintf("warning: variable '%s' was not previously defined in any scope, so it has been created in the global scope.", name);
        }
    }
    if (!var->node) var->node = hash_insert((GENERIC *)var, var_table);

    if (var->flags & VAREXPORT) {
        replace_env(new_env(var));
    } else if (exportflag || var->flags & VARAUTOX) {
        append_env(new_env(var));
        var->flags |= VAREXPORT;
    }

    if (func && depth==0) {
        if (!(*func)()) {
            /* restore old value (bug: doesn't un-export or un-set) */
            depth++;
            setvar(var, NULL, 0, oldvalue ? oldvalue : "", exportflag);
            depth--;
        }
    }
    if (var->status)
        update_status_field(var, -1);
    if (oldvalue)
        FREE(oldvalue);

    return var;
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
        } else if (is_space(*value)) {
            for (*value++ = '\0'; is_space(*value); value++);
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
        if (!(is_alpha(args[i]) || args[i]=='_' || (i>0 && is_digit(args[i])))) {
            eprintf("illegal variable name: %s", args);
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

    if (!localflag) return !!set_var_by_name(args, value, exportflag);

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

struct Value *handle_listvar_command(args)
    char *args;
{
    int mflag, opt;
    int exportflag = -1, error = 0, shortflag = 0;
    char *name = NULL, *value = NULL;

    mflag = matching;

    startopt(args, "m:gxsv");
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
            case 'v':
                shortflag = 2;
                break;
            default:
                return newint(0);
          }
    }
    if (error) return newint(0);

    if (*args) {
        name = args;
        for (value = name; *value && !is_space(*value); value++);
        if (*value) {
            *value++ = '\0';
            while(is_space(*value)) value++;
        }
        if (!*value)
            value = NULL;
    }

    return newint(listvar(name, value, mflag, exportflag, shortflag));
}

struct Value *handle_export_command(name)
    char *name;
{
    Var *var;

    if (!(var = findglobalvar(name))) {
        eprintf("%s not defined.", name);
        return newint(0);
    }
    if (!(var->flags & VAREXPORT)) append_env(new_env(var));
    var->flags |= VAREXPORT;
    return newint(1);
}

struct Value *handle_unset_command(name)
    char *name;
{
    long oldval;
    Var *var;

    if (!(var = findglobalvar(name))) return newint(0);

    if (var->flags & VARPOS) {
        eprintf("%s must a positive integer, so can not be unset.", var->name);
        return newint(0);
    }
    if (var->status) {
        /* Note: there may be more than one status field using this variable */
        eprintf("%s is used in %%status_fields, so can not be unset.", var->name);
        return newint(0);
    }

    hash_remove(var->node, var_table);
    if (var->flags & VAREXPORT) remove_env(name);
    FREE(var->value);

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
    return newint(1);
}

/*********/

/* Set a special variable, with proper coersion of the value. */
static int set_special_var(var, ivalue, value, fpp, oldvaluep)
    Var *var;
    long ivalue;
    CONST char *value, **oldvaluep;
    Toggler **fpp;
{
    static char buffer[20];
    long oldival = var->ival;
    CONST char *oldvalue = var->value;

    oflush();   /* flush buffer now, in case variable affects flushing */

    if (var->flags & VARENUM) {
        var->ival = value ? enum2int(value, var->enumvec, var->name) : ivalue;
        if (var->ival < 0) {
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
        if (value) {
            while (is_space(*value)) value++;
            var->ival=atol(value);
            if (!is_digit(*value) || var->ival < !!(var->flags & VARPOS)) {
                eprintf("%s must be an integer greater than or equal to %d",
                    var->name, !!(var->flags & VARPOS));
                var->ival = oldival;
                if (fpp) *fpp = NULL;
                return 0;
            }
        } else {
            var->ival = ivalue;
        }
        sprintf(buffer, "%ld", var->ival);
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
                switch (shortflag) {
                case 1:  oprintf("%s", var->name);   break;
                case 2:  oprintf("%s", var->value);  break;
                default:
                    oprintf("/%s %s=%s",
                        (var->flags & VAREXPORT) ? "setenv" : "set",
                        var->name, var->value);
                }
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

