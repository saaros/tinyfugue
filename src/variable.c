/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.c,v 33000.7 1994/04/16 05:12:43 hawkeye Exp $ */


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
static Var     *FDECL(findnearestvar,(char *name));
static Toggler *FDECL(set_special_var,(Var *var, char *value));
static Var     *FDECL(newlocalvar,(char *name, char *value));
static Var     *FDECL(newglobalvar,(char *name, char *value));
static char    *FDECL(new_env,(char *name, char *value));
static void     FDECL(append_env,(char *str));
static char   **FDECL(find_env,(char *str));
static void     FDECL(remove_env,(char *str));
static void     FDECL(replace_env,(char *str));
static void     FDECL(listvar,(int exportflag));

#define findglobalvar(name)   (Var *)hash_find(name, var_table)
#define findspecialvar(name) \
        (Var *)binsearch((GENERIC*)&(name), (GENERIC*)special_var, NUM_VARS, \
            sizeof(Var), genstrcmp)

#define HASH_SIZE 197    /* prime number */

static List localvar[1];          /* local variables */
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
char *enum_color[] =	{ "black", "red", "green", "yellow",
			  "blue", "magenta", "cyan", "white",
			  "8", "9", "10", "11", "12", "13", "14", "15",
			  NULL };

#define VARINT     001
#define VARENUM    002
#define VARSTR     004
#define VARSPECIAL 010
#define VAREXPORT  020

/* Special variables.
 * Omitted last field (node) is implicitly initialized to NULL.
 */
Var special_var[] = {
  {"MAIL"	,	NULL, VARSTR,  NULL	, 0    , ch_mailfile },
  {"TERM"	,	NULL, VARSTR,  NULL	, 0    , change_term },
  {"TFHELP"     ,   HELPFILE, VARSTR,  NULL	, 0    , NULL },
  {"TFLIBDIR"	,     LIBDIR, VARSTR,  NULL	, 0    , NULL },
  {"always_echo",	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"background"	,	NULL, VARENUM, enum_flag, TRUE , tog_bg },
  {"backslash"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"bamf"	,	NULL, VARENUM, enum_bamf, FALSE, NULL },
  {"beep"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"bg_output"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"borg"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"catch_ctrls",	NULL, VARENUM, enum_ctrl, FALSE, NULL },
  {"cleardone"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"clearfull"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"clock"	,	NULL, VARENUM, enum_flag, TRUE , tog_clock },
  {"gag"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"gpri"	,	NULL, VARINT , NULL	, 0    , NULL },
  {"hilite"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"hiliteattr"	,	"B" , VARSTR,  NULL	, 0    , ch_hilite },
  {"hook"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"hpri"	,	NULL, VARINT , NULL	, 0    , NULL },
  {"ignore_sigquit",	NULL, VARENUM, enum_flag, FALSE, tog_sigquit },
  {"insert"	,	NULL, VARENUM, enum_flag, TRUE , tog_insert },
  {"isize"	,	NULL, VARINT , NULL	, 3    , ch_isize },
  {"kecho"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"kprefix"	,	NULL, VARSTR , NULL	, 0    , NULL },
  {"login"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"lp"		,	NULL, VARENUM, enum_flag, FALSE, tog_lp },
  {"lpquote"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"maildelay"	,	NULL, VARINT , NULL	, 60   , ch_maildelay },
  {"matching"	,	NULL, VARENUM, enum_match,1    , NULL },
  {"max_recur"	,	NULL, VARINT , NULL	, 100  , NULL },
  {"mecho"	,	NULL, VARENUM, enum_mecho,0    , NULL },
  {"more"	,	NULL, VARENUM, enum_flag, FALSE, tog_more },
  {"mprefix"	,	"+",  VARSTR , NULL	, 0    , NULL },
  {"oldslash"	,	NULL, VARINT , NULL	, 1    , NULL },
  {"prompt_sec"	,	NULL, VARINT , NULL	, 0    , NULL },
  {"prompt_usec",	NULL, VARINT , NULL	, 250000,NULL },
  {"ptime"	,	NULL, VARINT , NULL	, 1    , NULL },
  {"qecho"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"qprefix"	,	NULL, VARSTR , NULL	, 0    , NULL },
  {"quiet"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"quitdone"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"quoted_args",	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"redef"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"refreshtime",	NULL, VARINT , NULL	, 250000,NULL },
  {"scroll"	,	NULL, VARENUM, enum_flag, FALSE ,setup_screen },
  {"shpause"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"snarf"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"sockmload"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"sub"	,	NULL, VARENUM, enum_sub , 0    , NULL },
  {"telopt"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"time_format",    "%H:%M", VARSTR , NULL	, 0    , NULL },
  {"visual"	,	NULL, VARENUM, enum_flag, FALSE, tog_visual },
  {"watchdog"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"watchname"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"wordpunct"	,       "_-", VARSTR , NULL	, FALSE, NULL },
  {"wrap"	,	NULL, VARENUM, enum_flag, TRUE , NULL },
  {"wraplog"	,	NULL, VARENUM, enum_flag, FALSE, NULL },
  {"wrapsize"	,	NULL, VARINT , NULL	, 0,     NULL },
  {"wrapspace"	,	NULL, VARINT , NULL	, 0,     NULL },
  {NULL         ,	NULL, 0      , NULL	, 0,     NULL }
};

/* initialize structures for variables */
void init_variables()
{
    char **oldenv, **p, *value, buf[20], *str;
    Var *var;

    init_hashtable(var_table, HASH_SIZE, strcmp);
    init_list(localvar);

    /* special pre-defined variables */
    for (var = special_var; var->name; var++) {
        var->flags |= VARSPECIAL;
        if (var->flags & VARSTR) {
            if (var->value) var->value = STRDUP(var->value);
            var->ival = !!var->value;
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
            /* overwrite a pre-defined variable */
            if (var->value) FREE(var->value);
            set_special_var(var, value ? value : "");
        }
        if (value) *--value = '=';
        var->flags |= VAREXPORT;
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
        if (j < i) return j;
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

    level = (List *)MALLOC(sizeof(List));
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
    FREE(level);
}

static Var *findlevelvar(name, level)
    char *name;
    List *level;
{
    ListEntry *node;

    for (node = level->head; node; node = node->next) {
        if (strcmp(name, ((Var *)node->datum)->name) == 0) break;
    }
    return node ? (Var *)node->datum : NULL;
}

static Var *findlocalvar(name)
    char *name;
{
    ListEntry *node;
    Var *var = NULL;

    for (node = localvar->head; node && !var; node = node->next) {
        var = findlevelvar(name, (List *)node->datum);
    }
    return var;
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
    STATIC_BUFFER(buf);

    if (np) *np = 0;
    if ((var = findnearestvar(name))) {
        if (np) *np = (var->flags & VARSTR) ? 0 : var->ival;
        return var->value;
    }
    if (ucase(name[0]) == 'P' && isdigit(name[1])) {
        Stringterm(buf, 0);
        return regsubstr(buf, atoi(name + 1)) >= 0 ? buf->s : NULL;
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
    } else {
        return setvar(name, value, FALSE);
    }
}

static Var *newlocalvar(name, value)
    char *name, *value;
{
    Var *var;

    var = (Var *)MALLOC(sizeof(Var));
    var->node = NULL;
    var->name = STRDUP(name);
    var->value = STRDUP(value);
    var->flags = VARSTR;
    /* var->ival = atoi(value); */ /* never used */
    var->func = NULL;
    inlist((GENERIC *)var, (List*)(localvar->head->datum), NULL);
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
    var->node = NULL;
    var->name = STRDUP(name);
    var->value = STRDUP(value);
    var->flags = VARSTR;
    /* var->ival = atoi(value); */ /* never used */
    var->func = NULL;
    return var;
}


/*
 * Environment routines.
 */

/* create new environment string */
static char *new_env(name, value)
    char *name, *value;
{
    char *str;

    str = (char *)MALLOC(strlen(name) + 1 + strlen(value) + 1);
    return strcat(strcat(strcpy(str, name), "="), value);
}

/* Add "<name>=<value>" to environment.  Assumes name is not already defined. */
/* str must be duped before call */
static void append_env(str)
    char *str;
{
    if (envsize == envmax) {
        envmax += 5;
        environ = (char **)REALLOC((char*)environ, (envmax+1) * sizeof(char*));
    }
    environ[envsize] = str;
    environ[++envsize] = NULL;
}

/* Find the environment string for <name>.  str can be in "<name>" or
 * "<name>=<value>" format.
 */
static char **find_env(str)
    char *str;
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
    char *str;
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

char *setvar(name, value, exportflag)
    char *name, *value;
    int exportflag;
{
    Var *var;
    Toggler *func = NULL;

    if ((var = findspecialvar(name))) {
        if (var->value) FREE(var->value);
        func = set_special_var(var, value);
    } else if ((var = findglobalvar(name))) {
        FREE(var->value);
        var->value = STRDUP(value);
    } else {
        var = newglobalvar(name, value);
    }
    if (!var->node) var->node = hash_insert((GENERIC *)var, var_table);

    if (var->flags & VAREXPORT) {
        replace_env(new_env(name, value));
    } else if (exportflag) {
        append_env(new_env(name, value));
        var->flags |= VAREXPORT;
    }

    if (func) (*func)();
    return var->value;
}

void setivar(name, value, exportflag)
    char *name;
    int value, exportflag;
{
    char buf[20];
    sprintf(buf, "%d", value);
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
        if (!localflag) listvar(exportflag);
        return !localflag;
    } else if ((value = strchr(args, '='))) {
        *value++ = '\0';
        if (!*args) {
            tfputs("% missing variable name", tferr);
            return 0;
        }
    } else if ((value = strchr(args, ' '))) {
        for (*value++ = '\0'; isspace(*value); value++);
    } else {
        if ((var = localflag ? findlocalvar(args) : findglobalvar(args))) {
            oprintf("%% %s=%s", args, var->value);
            return 1;
        } else {
            oprintf("%% %s not set %sally", args, localflag ? "loc" : "glob");
            return 0;
        }
    }

    /* Posix.2 restricts the lhs of a variable assignment to
     * alphanumerics and underscores.  We'll do the same.
     */
    for (i = 0; args[i]; i++) {
        if (!(isalpha(args[i]) || args[i]=='_' || (i>0 && isdigit(args[i])))) {
            oputs("% illegal variable name.");
            return 0;
        }
    }

    if (!localflag) return setvar(args, value, exportflag) ? 1 : 0;

    if (!localvar->head) {
        tfputs("% /let illegal at top level.", tferr);
        return 0;
    } else if ((var = findlevelvar(args, (List *)localvar->head->datum))) {
        FREE(var->value);
        var->value = STRDUP(value);
    } else {
        var = newlocalvar(args, value);
    }
    return 1;
}

int handle_export_command(name)
    char *name;
{
    Var *var;

    if (!(var = findglobalvar(name))) {
        tfprintf(tferr, "%% %s not defined.", name);
        return 0;
    }
    if (!(var->flags & VAREXPORT)) append_env(new_env(var->name, var->value));
    var->flags |= VAREXPORT;
    return 1;
}

int handle_unset_command(name)
    char *name;
{
    int oldval;
    Var *var;

    if (!(var = findglobalvar(name))) return 0;

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
        if (oldval && var->func) (*var->func)();
    }
    return 1;
}

/*********/

/* Set a special variable, with proper coersion of the value. */
static Toggler *set_special_var(var, value)
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

static void listvar(exportflag)
    int exportflag;
{
    int i;
    ListEntry *node;
    Var *var;

    for (i = 0; i < var_table->size; i++) {
        if (var_table->bucket[i]) {
            for (node = var_table->bucket[i]->head; node; node = node->next) {
                var = (Var*)node->datum;
                if (!(var->flags & VAREXPORT) == !exportflag)
                    oprintf("/%s %s=%s", exportflag ? "setenv" : "set",
                        var->name, var->value);
            }
        }
    }
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

