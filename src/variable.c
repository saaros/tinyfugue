/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/**************************************
 * Internal and environment variables *
 **************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "tf.h"
#include "util.h"
#include "output.h"
#include "macro.h"
#include "socket.h"
#include "search.h"
#include "signals.h"
#include "commands.h"

static Var     *FDECL(findlevelvar,(char *name, List *level));
static Var     *FDECL(findlocalvar,(char *name));
static Var     *FDECL(findglobalvar,(char *name));
static Var     *FDECL(findnearestvar,(char *name));
static Toggler *FDECL(set_tf_var,(Var *var, char *value));
static Var     *FDECL(newlocalvar,(char *name, char *value));
static Var     *FDECL(newglobalvar,(char *name, char *value));
static char    *FDECL(newstr,(char *name, char *value));
static void     FDECL(append_env,(char *name, char *value));
static char   **FDECL(find_env,(char *name));
static void     FDECL(remove_env,(char **envp));
static void     FDECL(replace_env,(char *name, char *value));

#define HASH_SIZE 197    /* prime number */

static List *localvar;            /* local variables */
static HashTable var_table[1];    /* global variables */
static int envsize;
static int envmax;

extern char **environ;

char *enum_flag[] =	{ "off", "on", NULL };
char *enum_bamf[] =	{ "off", "on", "old", NULL };
char *enum_ctrl[] =	{ "off", "ascii", "ansi", NULL };
char *enum_sub[] =	{ "off", "on", "full", NULL };
char *enum_match[] =	{ "simple", "glob", "regexp", NULL };
char *enum_mecho[] =	{ "off", "on", "all", NULL };
char *enum_color[] =	{ "black", "red", "green", "yellow", "blue",
			  "magenta", "cyan", "white", NULL };

#define VAREXPORT 1
#define VARINT    2
#define VARENUM   4
#define VARSTR    8

Var special_var[] = {
  {"MAIL"		, NULL, VARSTR,  NULL     , 0    , ch_mailfile	, NULL},
  {"TERM"		, NULL, VARSTR,  NULL     , 0    , change_term	, NULL},
  {"TFHELP"         , HELPFILE, VARSTR,  NULL     , 0    , NULL		, NULL},
  {"TFLIBDIR"	      , LIBDIR, VARSTR,  NULL     , 0    , NULL		, NULL},
  {"always_echo"	, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"background"		, NULL, VARENUM, enum_flag, TRUE , tog_bg	, NULL},
  {"backslash"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"bamf"		, NULL, VARENUM, enum_bamf, FALSE, NULL		, NULL},
  {"beep"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"bg_output"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"borg"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"catch_ctrls"	, NULL, VARENUM, enum_ctrl, FALSE, NULL		, NULL},
  {"cleardone"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"clearfull"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"clock"		, NULL, VARENUM, enum_flag, TRUE , ch_clock	, NULL},
  {"gag"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"gpri"		, NULL, VARINT , NULL     , 0    , NULL		, NULL},
  {"hilite"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"hiliteattr"		, "B" , VARSTR,  NULL     , 0    , ch_hilite	, NULL},
  {"hook"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"hpri"		, NULL, VARINT , NULL     , 0    , NULL		, NULL},
  {"ignore_sigquit"	, NULL, VARENUM, enum_flag, FALSE, tog_sigquit	, NULL},
  {"insert"		, NULL, VARENUM, enum_flag, TRUE , tog_insert	, NULL},
  {"isize"		, NULL, VARINT , NULL     , 3    , change_isize	, NULL},
  {"kecho"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"kprefix"		, NULL, VARSTR , NULL     , 0    , NULL		, NULL},
  {"login"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"lp"			, NULL, VARENUM, enum_flag, FALSE, tog_lp	, NULL},
  {"lpquote"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"maildelay"		, NULL, VARINT , NULL     , 60   , ch_maildelay	, NULL},
  {"matching"		, NULL, VARENUM, enum_match,1    , NULL		, NULL},
  {"max_recur"		, NULL, VARINT , NULL     , 100  , NULL		, NULL},
  {"mecho"		, NULL, VARENUM, enum_mecho,0    , NULL		, NULL},
  {"more"		, NULL, VARENUM, enum_flag, FALSE, tog_more	, NULL},
  {"mprefix"		, "+",  VARSTR , NULL     , 0    , NULL		, NULL},
  {"oldslash"		, NULL, VARINT , NULL     , 1    , NULL		, NULL},
  {"prompt_sec"		, NULL, VARINT , NULL     , 0    , NULL		, NULL},
  {"prompt_usec"	, NULL, VARINT , NULL     , 250000,NULL		, NULL},
  {"ptime"		, NULL, VARINT , NULL     , 1    , NULL		, NULL},
  {"qecho"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"qprefix"		, NULL, VARSTR , NULL     , 0    , NULL		, NULL},
  {"quiet"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"quitdone"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"quoted_args"	, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"redef"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"refreshtime"	, NULL, VARINT , NULL     , 250000,NULL		, NULL},
  {"scroll"		, NULL, VARENUM, enum_flag, FALSE ,setup_screen	, NULL},
  {"shpause"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"snarf"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"sockmload"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"sub"		, NULL, VARENUM, enum_sub , 0    , NULL		, NULL},
  {"time_format"      ,"%H:%M", VARSTR , NULL     , 0    , NULL		, NULL},
  {"visual"		, NULL, VARENUM, enum_flag, FALSE, tog_visual	, NULL},
  {"watchdog"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"watchname"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"wrap"		, NULL, VARENUM, enum_flag, TRUE , NULL		, NULL},
  {"wraplog"		, NULL, VARENUM, enum_flag, FALSE, NULL		, NULL},
  {"wrapsize"		, NULL, VARINT , NULL     , 0,     NULL		, NULL},
  {"wrapspace"		, NULL, VARINT , NULL     , 0,     NULL		, NULL}
};

/* initialize structures for variables */
void init_variables()
{
    char **oldenv, **p, *value, buf[20];
    int i;
    Var *var;

    init_hashtable(var_table, HASH_SIZE, strcmp);
    init_list(localvar = (List *)MALLOC(sizeof(List)));

    /* special pre-defined variables */
    for (i = 0; i < NUM_VARS; i++) {
        var = &special_var[i];
        if (var->flags & VARSTR) {
            if (var->value) var->value = STRDUP(var->value);
            var->ival = (var->value) ? 1 : 0;
        } else if (var->flags & VARENUM) {
            var->value = STRDUP(var->enumvec[var->ival]);
        } else /* VARINT */ {
            sprintf(buf, "%d", var->ival);
            var->value = STRDUP(buf);
        }
        var->node = hash_insert((GENERIC *)var, var_table);
    }

    /* environment variables */
    for (p = environ; *p; p++);
    envsize = 0;
    envmax = p - environ;
    oldenv = environ;
    environ = (char **)MALLOC((envmax + 1) * sizeof(char *));
    *environ = NULL;
    for (p = oldenv; *p; p++) {
        value = strchr(*p, '=');
        *value++ = '\0';
        i = binsearch(*p, (GENERIC*)special_var, NUM_VARS, sizeof(Var), strcmp);
        if (i < 0) {
            /* new variable */
            var = newglobalvar(*p, value);
            var->node = hash_insert((GENERIC *)var, var_table);
        } else {
            /* overwrite a pre-defined variable */
            var = &special_var[i];
            if (var->value) FREE(var->value);
            set_tf_var(var, value);
        }
        append_env(*p, value);
        var->flags |= VAREXPORT;
        *--value = '=';
    }
}

void init_values()
{
    Var *var;

    for (var = special_var; var - special_var < NUM_VARS; var++) {
        if (var->flags & VARSTR) {
            if (var->value && *var->value && var->func) (*var->func)();
        } else {
            if (var->ival && var->func) (*var->func)();
        }
    }
}

int enum2int(str, vec, msg)
    char *str, **vec, *msg;
{
    int i, j;
    STATIC_BUFFER(buf);

    for (i = 0; vec[i]; ++i) {
        if (cstrcmp(str, vec[i]) == 0) return i;
    }
    if (isdigit(*str)) {
        j = atoi(str);
        if (j >= 0 && j < i) return j;
    }
    Sprintf(buf, 0, "%S: valid values for %s are: %s", error_prefix(), msg,
        vec[0]);
    for (i = 1; vec[i]; ++i) Sprintf(buf, SP_APPEND, ", %s", vec[i]);
    tfputs(buf->s, tferr);
    return -1;
}


void newvarscope()
{
    List *level;

    init_list(level = (List *)MALLOC(sizeof(List)));
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
    FREE(level);
}

static Var *findlevelvar(name, level)
    char *name;
    List *level;
{
    ListEntry *vnode;
    Var *var = NULL;

    for (vnode = level->head; vnode && !var; vnode = vnode->next) {
        if (strcmp(name, ((Var *)(vnode->data))->name) == 0)
            var = (Var *)vnode->data;
    }
    return var;
}

static Var *findlocalvar(name)
    char *name;
{
    ListEntry *lnode;
    Var *var = NULL;

    for (lnode = localvar->head; lnode && !var; lnode = lnode->next) {
        var = findlevelvar(name, (List *)lnode->data);
    }
    return var;
}

static Var *findglobalvar(name)
    char *name;
{
    return (Var *)hash_find(name, var_table);
}

static Var *findnearestvar(name)
    char *name;
{
    Var *var;

    return (var = findlocalvar(name)) ? var : findglobalvar(name);
}

/* get value of global variable <name> */
char *getvar(name)
    char *name;
{
    Var *var;

    return (var = findglobalvar(name)) ? var->value : NULL;
}

/* If np is nonnull, ival of var will be put there. */
char *getnearestvar(name, np)
    char *name;
    int *np;
{
    Var *var;

    if (np) *np = 0;
    if ((var = findnearestvar(name))) {
        if (np) *np = (var->flags & VARSTR) ? 0 : var->ival;
        return var->value;
    }
    return NULL;
}

char *setnearestvar(name, value)
    char *name, *value;
{
    Var *var;

    if ((var = findlocalvar(name))) {
        FREE(var->value);
        return var->value = STRDUP(value);
    } else if (!localvar->head || (var = findglobalvar(name))) {
        return setvar(name, value, FALSE);
    } else {
        var = newglobalvar(name, value);
        var->node = hash_insert((GENERIC *)var, var_table);
        return var->value;
    }
}

char *setlocalvar(name, value)
    char *name, *value;
{
    Var *var;

    if (!localvar->head) {
        tfputs("% /let illegal at top level.", tferr);
        return NULL;
    }
    if ((var = findlevelvar(name, (List *)localvar->head->data))) {
        FREE(var->value);
        var->value = STRDUP(value);
    } else {
        var = newlocalvar(name, value);
    }
    return var->value;
}

static Var *newlocalvar(name, value)
    char *name, *value;
{
    Var *var;

    var = (Var *)MALLOC(sizeof(Var));
    var->name = STRDUP(name);
    var->value = STRDUP(value);
    var->flags = VARSTR;
    /* var->ival = atoi(value); */ /* never used */
    var->func = NULL;
    var->node = inlist((GENERIC *)var, (List*)(localvar->head->data), NULL);
    if (findglobalvar(name)) {
        do_hook(H_SHADOW, "%% Warning:  Local variable \"%s\" overshadows global variable of same name.", "%s", name);
    }
    return var;
}

static Var *newglobalvar(name, value)
    char *name, *value;
{
    Var *var;

    var = (Var *)MALLOC(sizeof(Var));
    var->name = STRDUP(name);
    var->value = STRDUP(value);
    var->flags = VARSTR;
    /* var->ival = atoi(value); */ /* never used */
    var->func = NULL;
    var->node = NULL;
    return var;
}

static char *newstr(name, value)
    char *name, *value;
{
    char *str;

    str = (char *)MALLOC(strlen(name) + 1 + strlen(value) + 1);
    return strcat(strcat(strcpy(str, name), "="), value);
}

/* Add "<name>=<value>" to environment.  Assumes name is not already defined. */
static void append_env(name, value)
    char *name, *value;
{
    if (envsize == envmax) {
        envmax = envsize + 5;
        environ = (char **)REALLOC((char*)environ, (envmax+1) * sizeof(char*));
    }
    environ[envsize] = newstr(name, value);
    environ[++envsize] = NULL;
}

/* Find the environment string for <name>. */
static char **find_env(name)
    char *name;
{
    char **envp;
    int len = strlen(name);

    for (envp = environ; *envp; envp++)
        if (strncmp(*envp, name, len) == 0 && (*envp)[len] == '=')
            return envp;
    return NULL;
}

/* Remove the environment string for <name>. */
static void remove_env(envp)
    char **envp;
{
    char *old;

    old = *envp;
    do *envp = *(envp + 1); while (*++envp);
    envsize--;
    FREE(old);
}

/* Replace the environment string for <name> with "<name>=<value>". */
static void replace_env(name, value)
    char *name, *value;
{
    char **envp;

    envp = find_env(name);
    FREE(*envp);
    *envp = newstr(name, value);
}

char *setvar(name, value, exportflag)
    char *name, *value;
    int exportflag;
{
    Var *var;
    int i;
    Toggler *func = NULL;

    i = binsearch(name, (GENERIC *)special_var, NUM_VARS, sizeof(Var), strcmp);

    if (i >= 0) {
        var = &special_var[i];
        if (var->value) FREE(var->value);
        func = set_tf_var(&special_var[i], value);
    } else if ((var = (Var *)hash_find(name, var_table))) {
        FREE(var->value);
        var->value = STRDUP(value);
        /* var->ival = atoi(value); */ /* never used */
    } else {
        var = newglobalvar(name, value);
    }

    if (var->node) {                     /* is it already defined? */
        if (var->flags & VAREXPORT) {
            replace_env(name, value);
        } else if (exportflag) {
            append_env(name, value);
            var->flags |= VAREXPORT;
        }
    } else {
        var->node = hash_insert((GENERIC *)var, var_table);
        if (exportflag) {
            append_env(name, value);
            var->flags |= VAREXPORT;
        }
    }

    if (func) (*func)();
    return var->value;
}

void setivar(name, value, exportflag)
    char *name;
    int value, exportflag;
{
    static char buf[20];
    sprintf(buf, "%d", value);
    setvar(name, buf, exportflag);
}

int handle_export_command(name)
    char *name;
{
    Var *var;

    if (!(var = (Var *)hash_find(name, var_table))) {
        tfprintf(tferr, "%% %s not defined.", name);
        return 0;
    }
    if (!(var->flags & VAREXPORT)) append_env(var->name, var->value);
    return 1;
}

int handle_unset_command(name)
    char *name;
{
    int oldval, i;
    Var *var;

    if (!(var = (Var *)hash_find(name, var_table))) return 0;

    hash_remove(var->node, var_table);
    var->node = NULL;

    i = binsearch(name, (GENERIC*)special_var, NUM_VARS, sizeof(Var), strcmp);
    if (i < 0) {
        if (var->flags & VAREXPORT) remove_env(find_env(name));
        FREE(var->name);
        FREE(var->value);
        FREE(var);
    } else {
        oldval = special_var[i].ival;
        special_var[i].ival = 0;
        if (special_var[i].flags & VAREXPORT) {
            remove_env(find_env(name));
            special_var[i].flags &= ~VAREXPORT;
        }
        FREE(special_var[i].value);
        special_var[i].value = NULL;
        if (oldval && special_var[i].func) (*special_var[i].func)();
    }
    return 1;
}

/* Set a special variable, with proper coersion of the value. */
static Toggler *set_tf_var(var, value)
    Var *var;
    char *value;
{
    int oldival;
    Toggler *func;
    static char buffer[20];

    oldival = var->ival;

    if (var->flags & VARINT) {
        sprintf(buffer, "%d", var->ival=atoi(value));
        if (strcmp(buffer, value) != 0) {
            tfprintf(tferr, "%S: %s: invalid integer value.", error_prefix(),
                var->name);
            return NULL;
        }
        value = buffer;
        func = (var->ival != oldival) ? var->func : NULL;

    } else if (var->flags & VARENUM) {
        if ((var->ival = enum2int(value, var->enumvec, var->name)) < 0) {
            var->ival = oldival;
            return NULL;
        }
        value = var->enumvec[var->ival];
        func = (var->ival != oldival) ? var->func : NULL;

    } else /* if (var->flags & VARSTR) */ {
        /* var->ival = atoi(value); */ /* never used */
        func = var->func;
    }

    var->value = STRDUP(value);
    return func;
}

void listvar(exportflag)
    int exportflag;
{
    int i;
    ListEntry *node;

    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            for (node = var_table->bucket[i]->head; node; node = node->next) {
                if (!(((Var *)(node->data))->flags & VAREXPORT) == !exportflag)
                    oprintf("/%s %s=%s", exportflag ? "setenv" : "set",
                        ((Var *)(node->data))->name,
                        ((Var *)(node->data))->value);
            }
        }
    }
}

#ifdef DMALLOC
void free_vars()
{
    char **p;
    int i;
    ListEntry *node, *next;

    for (p = environ; *p; p++) FREE(*p);
    FREE(environ);

    for (i = 0; i < NUM_VARS; i++) {
        if (special_var[i].value) FREE(special_var[i].value);
        if (special_var[i].node) hash_remove(special_var[i].node, var_table);
    }

    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            for (node = var_table->bucket[i]->head; node; node = next) {
                next = node->next;
                FREE(((Var *)(node->data))->name);
                FREE(((Var *)(node->data))->value);
                FREE(node->data);
                FREE(node);
            }
        }
    }
    free_hash(var_table);
}
#endif
