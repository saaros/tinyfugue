/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: macro.c,v 33000.13 1994/04/26 08:49:24 hawkeye Exp $ */


/**********************************************
 * Fugue macro package                        *
 *                                            *
 * Macros, hooks, triggers, hilites and gags  *
 * are all processed here.                    *
 **********************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "search.h"
#include "world.h"
#include "macro.h"
#include "keyboard.h"	/* bind_key()... */
#include "expand.h"
#include "socket.h"	/* xworld() */
#include "command.h"
#include "commands.h"

int invis_flag = 0;
int send_hook_level = 0;

static Macro  *FDECL(macro_spec,(char *args));
static int     FDECL(macro_match,(Macro *spec, Macro *macro, Pattern *aux_pat));
static int     FDECL(check_macro_patterns,(Macro *spec));
static int     FDECL(check_and_add_macro,(Macro *spec));
static Pattern*FDECL(make_aux_patterns,(Macro *spec));
static Macro  *FDECL(match_exact,(int hook, char *str, int flags));
static int     FDECL(list_defs,(TFILE *file, Macro *spec, int abbrev));
static int     FDECL(parse_hook,(char **args));
static int     FDECL(run_match,(Macro *macro, char *text, int hook,
                     Aline *aline));
static char   *FDECL(world_subs,(char *src));
static String *FDECL(hook_name,(int hook));
static String *FDECL(attr2str,(int attrs));
static int     FDECL(rpricmp,(CONST Macro *m1, CONST Macro *m2));
static void    FDECL(nuke_macro,(Macro *macro));
static void    NDECL(unkill_macro);
static int     FDECL(install_bind,(struct Macro *spec));

extern char *enum_match[];
extern char *enum_color[];

#define HASH_SIZE 197

static List maclist[1];                /* list of all (live) macros */
static List triglist[1];               /* list of macros by trigger */
static List hooklist[1];               /* list of macros by hook */
static Macro *dead_macros;             /* head of list of dead macros */
static HashTable macro_table[1];       /* macros hashed by name */
static World NoWorld, AnyWorld;        /* explicit "no" and "any" */
static int mnum = 0;                   /* macro ID number */

/* It is IMPORTANT that these be in the same order as enum Hooks */
static char *hook_table[] = {
  "ACTIVITY",   
  "BACKGROUND",
  "BAMF",
  "CONFAIL",
  "CONFLICT",
  "CONNECT",
  "DISCONNECT",
  "HISTORY",
  "KILL",
  "LOAD",
  "LOADFAIL",
  "LOG",
  "LOGIN",
  "MAIL",
  "MORE",
  "PENDING",
  "PROCESS",
  "PROMPT",
  "REDEF",
  "RESIZE",
  "RESUME",
  "SEND",
  "SHADOW",
  "SHELL",
  "WORLD"
};

#define NONNULL(str) ((str) ? (str) : "")

/* These macros allow easy sharing of trigger and hook code. The address and
 * dereference in PATTERN allows it to be used portably as an lvalue.
 */
#define MAC(Node)       ((Macro *)(Node->datum))
#define PATTERN(Node)   (    *(hook ? &MAC(Node)->hargs : &MAC(Node)->trig))
#define FLAG(mac)       ((int)(hook ?  MAC(node)->hook  :  MAC(node)->attr))
    /* FLAG() can't be lvalue */


void init_macros()
{
    init_hashtable(macro_table, HASH_SIZE, cstrcmp);
    init_list(maclist);
    init_list(triglist);
    init_list(hooklist);
}

/***************************************
 * Routines for parsing macro commands *
 ***************************************/

short parse_attrs(argp)      /* convert attr string to bitfields */
    char **argp;
{
    short attrs, color;

    for (attrs = 0; **argp; ++*argp) {
        switch(**argp) {
        case 'n':  attrs |= F_NORM;      break;
        case 'G':  attrs |= F_NOHISTORY; break;
        case 'g':  attrs |= F_GAG;       break;
        case 'u':  attrs |= F_UNDERLINE; break;
        case 'r':  attrs |= F_REVERSE;   break;
        case 'f':  attrs |= F_FLASH;     break;
        case 'd':  attrs |= F_DIM;       break;
        case 'B':  attrs |= F_BOLD;      break;
        case 'b':  attrs |= F_BELL;      break;
        case 'h':  attrs |= F_HILITE;    break;
        case 'C':
            if ((color = enum2int(++*argp, enum_color, "color")) < 0)
                return -1;
            return attrs | F_COLOR | color;
        default:
            tfprintf(tferr, "%S: invalid display attribute '%c'",
                error_prefix(), **argp);
            return -1;
        }
    }
    return (attrs & F_NORM) ? F_NORM : attrs;
}

/* Convert hook string to bit vector; return -1 on error. */
static int parse_hook(argp)
    char **argp;
{
    char *in, state, **hook;
    int result = 0;

    if (!**argp) return ALL_HOOKS;
    for (state = '|'; state == '|'; *argp = in) {
        for (in = *argp; *in && !isspace(*in) && *in != '|'; ++in);
        state = *in;
        *in++ = '\0';
        if (strcmp(*argp, "*") == 0) result = ALL_HOOKS;
        if (strcmp(*argp, "0") == 0) result = 0;
        else {
            hook = (char **)binsearch((GENERIC*)argp, (GENERIC *)hook_table,
                NUM_HOOKS, sizeof(char*), gencstrcmp);
            if (!hook) {
                tfprintf(tferr, "%S: invalid hook event \"%s\"",
                    error_prefix(), *argp);
                return -1;
            }
            result |= (1 << (hook - hook_table));
        }
    }
    if (!state) --*argp;
    return result;
}

/* macro_spec
 * Converts a macro description string to a more useful Macro structure.
 * Omitted fields are set to an illegal value that means "don't care".
 * In /def, don't care fields are set to their default values;
 * in macro_match(), they are not used in the comparison.  Don't care
 * values for numeric fields are -1 or 0, depending on the field; for
 * strings, NULL.
 */
static Macro *macro_spec(args)
    char *args;
{
    Macro *spec;
    char opt, *ptr;
    int num, error = FALSE;

    spec = (Macro *)MALLOC(sizeof(struct Macro));
    spec->name = spec->body = spec->bind = NULL;
    spec->numnode = spec->trignode = spec->hooknode = spec->hashnode = NULL;
    init_pattern(&spec->trig, NULL, 0);
    init_pattern(&spec->hargs, NULL, 0);
    init_pattern(&spec->wtype, NULL, 0);
    spec->world = NULL;
    spec->pri = spec->prob = spec->shots = spec->hook = spec->fallthru = -1;
    spec->mflag = -1;
    spec->invis = 0;
    spec->attr = spec->subattr = 0;
    spec->subexp = -1;
    spec->temp = TRUE;
    spec->dead = FALSE;

    startopt(args, "p#c#b:t:w:h:a:f:P:T:FiIn#1:m:");
    while ((opt = nextopt(&ptr, &num))) {
        switch (opt) {
        case 'm':
            if ((spec->mflag = enum2int(ptr, enum_match, "-m")) < 0)
                error = TRUE;
            break;
        case 'p':
            spec->pri = num;
            break;
        case 'c':
            spec->prob = num;
            break;
        case 'F':
            spec->fallthru = 1;
            break;
        case 'i':
            spec->invis = 1;
            break;
        case 'I':
            spec->invis = 2;
            break;
        case 'b':
            if (spec->bind) FREE(spec->bind);
            spec->bind = print_to_ascii(ptr);
            spec->bind = STRDUP(spec->bind);
            break;
        case 't':
            free_pattern(&spec->trig);
            init_pattern(&spec->trig, ptr, 0);  /* can't fail */
            break;
        case 'T':
            free_pattern(&spec->wtype);
            init_pattern(&spec->wtype, ptr, 0);  /* can't fail */
            break;
        case 'w':
            if (!*ptr || strcmp(ptr, "+") == 0) spec->world = &AnyWorld;
            else if (strcmp(ptr, "-") == 0) spec->world = &NoWorld;
            else if (!(spec->world = find_world(ptr))) {
                tfprintf(tferr, "%% No world %s", ptr);
                error = TRUE;
            }
            break;
        case 'h':
            free_pattern(&spec->hargs);
            if ((spec->hook = parse_hook(&ptr)) < 0) error = TRUE;
            else if (*ptr) {
                init_pattern(&spec->hargs, ptr, 0);  /* can't fail */
            }
            break;
        case 'a': case 'f':
            if ((spec->attr |= parse_attrs(&ptr)) < 0) error = TRUE;
            break;
        case 'P':
            spec->subexp = strtoi(&ptr);
            if (spec->subexp < 0 || spec->subexp >= NSUBEXP) {
                tfprintf(tferr, "%S: -P number must be between 0 and %d.",
                    error_prefix(), NSUBEXP - 1);
                error = TRUE;
            } else {
                if ((spec->subattr |= parse_attrs(&ptr)) < 0) error = TRUE;
            }
            break;
        case 'n':
            spec->shots = num;
            break;
        case '1':
            if (ptr[0] && ptr[1]) error = TRUE;
            else if (!*ptr || *ptr == '+') spec->shots = 1;
            else if (*ptr == '-') spec->shots = 0;
            else error = TRUE;
            if (error) tfputs("% Invalid argument to 1 option", tferr);
            break;
        default:
            error = TRUE;
        }

        if (error) {
            nuke_macro(spec);
            return NULL;
        }
    }

    if (!*ptr) return spec;
    spec->name = ptr;
    if ((ptr = strchr(ptr, '='))) {
        *ptr++ = '\0';
        ptr = stripstr(ptr);
        spec->body = (*ptr) ? STRDUP(ptr) : NULL;
    }
    spec->name = stripstr(spec->name);
    spec->name = *spec->name ? STRDUP(spec->name) : NULL;
    return spec;
}

static int check_macro_patterns(spec)
    Macro *spec;
{
    int result = 1;

    if (spec->attr & F_NORM) spec->attr = 0;
    if (spec->subattr & F_NORM) spec->subattr = 0;
    if (spec->mflag < 0) {
        spec->mflag = (spec->subattr) ? 2 : matching;
    } else if (spec->subattr && spec->mflag != 2) {
        cmderror("-P requires -mregexp");
        return 0;
    }

    if (!init_pattern(&spec->trig,  spec->trig.str,  spec->mflag)) result = 0;
    if (!init_pattern(&spec->hargs, spec->hargs.str, spec->mflag)) result = 0;
    if (!init_pattern(&spec->wtype, spec->wtype.str, spec->mflag)) result = 0;
    return result;
}

/* make_aux_pattern
 * Macro_match() needs to compare some string fields that aren't normally
 * patterns (name, body, bind).  This function creates patterns for those
 * fields.  The result is stored in a static array of patterns, which is
 * freed and reused each time this function is called.
 */
static Pattern *make_aux_patterns(spec)
    Macro *spec;
{
    static Pattern aux[3] = { {NULL, NULL}, {NULL, NULL}, {NULL, NULL} };

    free_pattern(&aux[0]);
    free_pattern(&aux[1]);
    free_pattern(&aux[2]);
    if (spec->mflag < 0) spec->mflag = matching;
    if      (!init_pattern(&aux[0], spec->name, spec->mflag)) return NULL;
    else if (!init_pattern(&aux[1], spec->body, spec->mflag)) return NULL;
    else if (!init_pattern(&aux[2], spec->bind, spec->mflag)) return NULL;
    return aux;
}

/* macro_match
 * Compares spec to macro.  aux_pat is an array of patterns for string
 * fields that aren't normally patterns (name, body, bind).
 * Caller must call regrelease() when done with macro_match().
 */
static int macro_match(spec, macro, aux)
    Macro *spec, *macro;
    Pattern *aux;
{
    if (!spec->invis && macro->invis) return 1;
    if (spec->invis == 2 && !macro->invis) return 1;
    if (spec->shots >= 0 && spec->shots != macro->shots) return 1;
    if (spec->fallthru >= 0 && spec->fallthru != macro->fallthru) return 1;
    if (spec->prob >= 0 && spec->prob != macro->prob) return 1;
    if (spec->pri >= 0 && spec->pri != macro->pri) return 1;
    if (spec->attr && (spec->attr & macro->attr) == 0) return 1;
    if (spec->subexp >= 0 && macro->subexp < 0) return 1;
    if (spec->subattr && (spec->subattr & macro->subattr) == 0) return 1;
    if (spec->world) {
        if (spec->world == &NoWorld) {
            if (macro->world) return 1;
        } else if (spec->world == &AnyWorld) {
            if (!macro->world) return 1;
        } else if (spec->world != macro->world) return 1;
    }
    if (spec->bind) {
        if (!*spec->bind) {
            if (!*macro->bind) return 1;
        } else {
            if (!patmatch(&aux[2], macro->bind, spec->mflag, FALSE)) return 1;
        }
    }
    if (!spec->hook && macro->hook > 0) return 1;
    if (spec->hook > 0) {
        if ((spec->hook & macro->hook) == 0) return 1;
        if (spec->hargs.str && *spec->hargs.str) {
            if (!patmatch(&spec->hargs, NONNULL(macro->hargs.str), spec->mflag, FALSE))
                return 1;
        }
    }
    if (spec->trig.str) {
        if (!*spec->trig.str) {
            if (!macro->trig.str) return 1;
        } else {
            if (!patmatch(&spec->trig, NONNULL(macro->trig.str), spec->mflag, FALSE))
                return 1;
        }
    }
    if (spec->wtype.str) {
        if (!*spec->wtype.str) {
            if (!macro->wtype.str) return 1;
        } else {
            if (!patmatch(&spec->wtype, NONNULL(macro->wtype.str), spec->mflag, FALSE))
                return 1;
        }
    }
    if (spec->name && !patmatch(&aux[0], macro->name, spec->mflag, FALSE))
        return 1;
    if (spec->body && !patmatch(&aux[1], macro->body, spec->mflag, FALSE))
        return 1;
    return 0;
}

Macro *find_macro(name)              /* find Macro by name */
    char *name;
{
    if (!*name) return NULL;
    return (Macro *)hash_find(name, macro_table);
}

static Macro *match_exact(hook, str, flags)   /* find single exact match */
    int hook, flags;
    char *str;
{
    ListEntry *node;
  
    if ((hook && !flags) || (!hook && !*str)) return NULL;
    node = hook ? hooklist->head : triglist->head;
    while (node) {
        if ((FLAG(node) & flags) &&
            (!PATTERN(node).str || cstrcmp(PATTERN(node).str, str) == 0))
                break;
        node = node->next;
    }
    return node ? MAC(node) : NULL;
}

/**************************
 * Routines to add macros *
 **************************/

/* create a Macro */
Macro *new_macro(name, trig, bind, hook, hargs, body, pri, prob, attr, invis)
    char *name, *trig, *bind, *body, *hargs;
    int hook, pri, prob, attr, invis;
{
    Macro *new;
    int error = 0;

    new = (Macro *) MALLOC(sizeof(struct Macro));
    new->numnode = new->trignode = new->hooknode = new->hashnode = NULL;
    new->name = STRDUP(name);
    new->body = STRDUP(body);
    new->bind = STRDUP(bind);
    new->hook = hook;
    new->mflag = matching;
    if (!init_pattern(&new->trig, trig, new->mflag)) error++;
    if (!init_pattern(&new->hargs, hargs, new->mflag)) error++;
    init_pattern(&new->wtype, NULL, 0);
    new->world = NULL;
    new->pri = pri;
    new->prob = prob;
    new->attr = attr;
    new->subexp = -1;
    new->subattr = 0;
    new->shots = 0;
    new->invis = invis;
    new->fallthru = 0;
    new->temp = TRUE;
    new->dead = FALSE;

    if (!error) return new;
    nuke_macro(new);
    return NULL;
}

/* add_macro
 * Install a permanent Macro in appropriate structures.
 * Only the keybinding is checked for conflicts; everything else is assumed
 * assumed to be error- and conflict-free.  If the install_bind fails, the
 * macro will be nuked.
 */
int add_macro(macro)
    Macro *macro;
{
    if (!macro) return 0;
    if (*macro->bind && !install_bind(macro)) return 0;
    macro->num = ++mnum;
    macro->numnode = inlist((GENERIC *)macro, maclist, NULL);
    if (*macro->name)
        macro->hashnode = hash_insert((GENERIC *)macro, macro_table);
    if (macro->trig.str)
        macro->trignode = sinsert((GENERIC *)macro, triglist, (Cmp *)rpricmp);
    if (macro->hook)
        macro->hooknode = sinsert((GENERIC *)macro, hooklist, (Cmp *)rpricmp);
    macro->temp = FALSE;
    return macro->num;
}

/* compares m1 and m2 based on reverse priority and fallthru */
static int rpricmp(m1, m2)
    CONST Macro *m1, *m2;
{
    if (m2->pri != m1->pri) return m2->pri - m1->pri;
    else return m2->fallthru - m1->fallthru;
}

static int install_bind(spec)   /* install Macro's binding in key structures */
    Macro *spec;
{
    Macro *macro;

    if ((macro = find_key(spec->bind))) {
        if (redef) {
            do_hook(H_REDEF, "%% Redefined %s %s", "%s %s",
                "binding", ascii_to_print(spec->bind));
            kill_macro(macro);
        } else {
            tfprintf(tferr, "%% Binding %s already exists.",
                ascii_to_print(spec->bind));
            nuke_macro(spec);
            return 0;
        }
    }
    if (bind_key(spec)) return 1;  /* fails if is prefix or has prefix */
    nuke_macro(spec);
    return 0;
}

int handle_def_command(args)
    char *args;
{
    Macro *spec;

    if (!*args || !(spec = macro_spec(args))) return 0;
    if (spec->name && *spec->name == '@') {
        tfputs("% Macro names may not begin with '@'.", tferr);
        nuke_macro(spec);
        return 0;
    }
    if (spec->name && find_command(spec->name)) {
        do_hook(H_CONFLICT,
        "%% warning: /%s conflicts with the builtin command of the same name.",
        "%s", spec->name);
    }

    return check_and_add_macro(spec);
}

/* Error checking, set "don't care" fields to default values, and add_macro().
 * If error checking fails, spec will be nuked.
 */
static int check_and_add_macro(spec)
    Macro *spec;
{
    Macro *macro = NULL;

    if (!check_macro_patterns(spec)) {
        nuke_macro(spec);
        return 0;
    }
    if (spec->world == &AnyWorld) spec->world = xworld();
    if (spec->pri < 0) spec->pri = 1;
    if (spec->prob < 0) spec->prob = 100;
    if (spec->shots < 0) spec->shots = 0;
    if (spec->invis) spec->invis = 1;
    if (spec->hook < 0) spec->hook = 0;
    if (spec->fallthru < 0) spec->fallthru = 0;
    if (spec->mflag < 0) spec->mflag = matching;
    if (spec->attr & F_NORM || !spec->attr) spec->attr = F_NORM;
    if (!spec->bind) spec->bind = STRNDUP("", 0);
    if (!spec->name) spec->name = STRNDUP("", 0);
    if (!spec->body) spec->body = STRNDUP("", 0);

    if (*spec->name && (macro = find_macro(spec->name)) && !redef) {
        tfprintf(tferr, "%% Macro %s already exists", spec->name);
        nuke_macro(spec);
        return 0;
    }
    if (!add_macro(spec)) return 0;
    if (macro) {
        do_hook(H_REDEF, "%% Redefined %s %s", "%s %s", "macro", spec->name);
        kill_macro(macro);
    }
    return spec->num;
}

int add_hook(args, body)                  /* define a new Macro with hook */
    char *args, *body;
{
    int hook;

    if ((hook = parse_hook(&args)) < 0) return 0;
    args = stripstr(args);
    if (!*args) args = NULL;
    return add_macro(new_macro("", NULL, "", hook, args, body, 0,100,F_NORM,0));
}

/* /edit: Edit an existing macro.
 * Actually editing the macro in place is quite hairy, so instead we
 * remove the old one, create a replacement and add it.  If the replacement
 * fails, we re-add the original.  BUG(?): In either case, the macro gets
 * renumbered.  We can't just re-use the old number, because the macro
 * would be out of order in maclist.
 */
int handle_edit_command(args)
    char *args;
{
    Macro *spec, *macro;
    ListEntry *node;
    int number;

    if (!*args || !(spec = macro_spec(args))) {
        return 0;
    } else if (!spec->name) {
        tfputs("% You must specify a macro.", tferr);
        nuke_macro(spec);
        return 0;
    } else if (spec->name[0] == '#') {
        number = atoi(spec->name + 1);
        for (node = maclist->head; node; node = node->next)
            if (MAC(node)->num == number) break;
        if (!node) {
            tfprintf(tferr, "%% Macro #%d does not exist", number);
            nuke_macro(spec);
            return 0;
        }
        macro = MAC(node);
    } else if (spec->name[0] == '$') {
        if (!(macro = match_exact(FALSE, spec->name + 1, F_ATTR))) {
            tfprintf(tferr, "%% Trigger %s does not exist", spec->name + 1);
            nuke_macro(spec);
            return 0;
        }
    } else if (!(macro = find_macro(spec->name))) {
        tfprintf(tferr, "%% Macro %s does not exist", spec->name);
        nuke_macro(spec);
        return 0;
    }

    kill_macro(macro);

    FREE(spec->name);
    spec->name = STRDUP(macro->name);

    if (!spec->body) spec->body = STRDUP(macro->body);
    if (!spec->bind) spec->bind = STRDUP(macro->bind);
    if (!spec->wtype.str && macro->wtype.str)
        spec->wtype.str = STRDUP(macro->wtype.str);
    if (!spec->trig.str && macro->trig.str)
        spec->trig.str = STRDUP(macro->trig.str);
    if (spec->hook < 0) {
        spec->hook = macro->hook;
        if (macro->hargs.str) spec->hargs.str = STRDUP(macro->hargs.str);
    }
    if (!spec->world) spec->world = macro->world;
    else if (spec->world == &AnyWorld) spec->world = xworld();
    if (spec->pri < 0) spec->pri = macro->pri;
    if (spec->prob < 0) spec->prob = macro->prob;
    if (spec->shots < 0) spec->shots = macro->shots;
    if (spec->fallthru < 0) spec->fallthru = macro->fallthru;
    if (spec->mflag < 0) spec->mflag = macro->mflag;
    if (spec->attr == 0) spec->attr = macro->attr;
    if (spec->subexp < 0) {
        spec->subexp = macro->subexp;
        spec->subattr = macro->subattr;
    }

    if (check_and_add_macro(spec)) return spec->num;

    /* Edit failed.  Restore original macro. */
    unkill_macro();
    return 0;
}


/********************************
 * Routines for removing macros *
 ********************************/

void kill_macro(macro)            /* remove all references to macro */
    Macro *macro;
{
    if (macro->dead) return;
    macro->dead = TRUE;
    macro->tnext = dead_macros;
    dead_macros = macro;
    unlist(macro->numnode, maclist);
    if (*macro->name) hash_remove(macro->hashnode, macro_table);
    if (macro->trig.str) unlist(macro->trignode, triglist);
    if (macro->hook) unlist(macro->hooknode, hooklist);
    if (*macro->bind) unbind_key(macro);
}

static void unkill_macro()        /* undo the last kill_macro() */
{
    Macro *macro = dead_macros;
    macro->dead = FALSE;
    dead_macros = macro->tnext;
    add_macro(macro);
}

void nuke_dead_macros()
{
    Macro *macro;

    while ((macro = dead_macros)) {
        dead_macros = dead_macros->tnext;
        nuke_macro(macro);
    }
}

static void nuke_macro(macro)            /* free macro structure */
    Macro *macro;
{
    if (!macro->dead && !macro->temp) {
        kill_macro(macro);
    }

    if (macro->name) FREE(macro->name);
    if (macro->body) FREE(macro->body);
    if (macro->bind) FREE(macro->bind);
    free_pattern(&macro->trig);
    free_pattern(&macro->hargs);
    free_pattern(&macro->wtype);
    FREE(macro);
}

int remove_macro(str, flags, byhook)        /* delete a macro */
    char *str;
    int flags;
    int byhook;
{
    Macro *macro;
    char *args;

    if (byhook) {
        args = str;
        if ((flags = parse_hook(&args)) < 0) return 0;
        if (!(macro = match_exact(byhook, args, flags)))
            tfprintf(tferr, "%% Hook on \"%s\" was not defined.", str);
    } else if (flags) {
        if (!(macro = match_exact(byhook, str, flags)))
            tfprintf(tferr, "%% Trigger on \"%s\" was not defined.", str);
    } else {
        if (!(macro = find_macro(str)))
            tfprintf(tferr, "%% Macro \"%s\" was not defined.", str);
    }
    if (!macro) return 0;
    kill_macro(macro);
    return 1;
}

int handle_purge_command(args)                /* delete all specified macros */
    char *args;
{
    Macro *spec;
    ListEntry *node, *next;
    Pattern *aux_pat;
    int result = 1;

    if (!(spec = macro_spec(args))) result = 0;
    if (result && !(check_macro_patterns(spec))) result = 0;
    if (result && !(aux_pat = make_aux_patterns(spec))) result = 0;
    if (result) {
        for (node = maclist->head; node; node = next) {
            next = node->next;
            if (macro_match(spec, MAC(node), aux_pat) == 0)
                kill_macro(MAC(node));
        }
    }
    regrelease();
    if (spec) nuke_macro(spec);
    return result;
}

int handle_undefn_command(args)                 /* delete macro by number */
    char *args;
{
    int num, result = 1;
    ListEntry *node;

    while (*args) {
        if ((num = numarg(&args)) >= 0) {
            for (node = maclist->head; node; node = node->next) {
                /* We know the macros are in decending numeric order. */
                if (MAC(node)->num <= num) break;
            }
            if (node && MAC(node)->num == num) kill_macro(MAC(node));
            else {
                tfprintf(tferr, "%% No macro with number %d", num);
                result = 0;
            }
        }
    }
    return result;
}

void remove_world_macros(w)
    World *w;
{
    ListEntry *node, *next;

    for (node = maclist->head; node; node = next) {
        next = node->next;
        if (MAC(node)->world == w) kill_macro(MAC(node));
    }
}


/**************************
 * Routine to list macros *
 **************************/

static String *hook_name(hook)        /* convert hook bitfield to string */
    int hook;
{
    int n;
    STATIC_BUFFER(buf);

    Stringterm(buf, 0);
    for (n = 0; n < (int)NUM_HOOKS; n++) {
                 /* ^^^^^ Some brain dead compilers need that cast */
        if (!((1 << n) & hook)) continue;
        if (buf->len) Stringadd(buf, '|');
        Stringcat(buf, hook_table[n]);
    }
    return buf;
}

static String *attr2str(attrs)
    int attrs;
{
    STATIC_BUFFER(buffer);

    Stringterm(buffer, 0);
    if (attrs & F_NOHISTORY) Stringadd(buffer, 'G');
    if (attrs & F_UNDERLINE) Stringadd(buffer, 'u');
    if (attrs & F_REVERSE)   Stringadd(buffer, 'r');
    if (attrs & F_FLASH)     Stringadd(buffer, 'f');
    if (attrs & F_DIM)       Stringadd(buffer, 'd');
    if (attrs & F_BOLD)      Stringadd(buffer, 'B');
    if (attrs & F_BELL)      Stringadd(buffer, 'b');
    if (attrs & F_HILITE)    Stringadd(buffer, 'h');
    if (attrs & F_COLOR)     Stringadd(buffer, 'C');
    if (attrs & F_COLOR)     Stringcat(buffer, enum_color[attrs & F_COLORMASK]);
    return buffer;
}

static int list_defs(file, spec, abbrev)    /* list all specified macros */
    TFILE *file;
    Macro *spec;
    int abbrev;
{
    Macro *p;
    ListEntry *node;
    Pattern *aux_pat;
    STATIC_BUFFER(buffer);
    int result = 0;

    if (!(aux_pat = make_aux_patterns(spec))) return 0;

    /* maclist is in reverse numeric order, so we start from tail */
    for (node = maclist->tail; node; node = node->prev) {
        p = MAC(node);
        if (macro_match(spec, p, aux_pat) != 0) continue;
        result = p->num;

        if (abbrev) {
            Sprintf(buffer, 0, "%% %d: ", p->num);
            if (p->attr & F_NOHISTORY) Stringcat(buffer, "(norecord) ");
            if (p->attr & F_GAG) Stringcat(buffer, "(gag) ");
            else if (p->attr & ~F_NORM) {
                if (p->attr & F_UNDERLINE) Stringcat(buffer, "(underline) ");
                if (p->attr & F_REVERSE)   Stringcat(buffer, "(reverse) ");
                if (p->attr & F_FLASH)     Stringcat(buffer, "(flash) ");
                if (p->attr & F_DIM)       Stringcat(buffer, "(dim) ");
                if (p->attr & F_BOLD)      Stringcat(buffer, "(bold) ");
                if (p->attr & F_BELL)      Stringcat(buffer, "(bell) ");
                if (p->attr & F_HILITE)    Stringcat(buffer, "(hilite) ");
                if (p->attr & F_COLOR)
                    Sprintf(buffer, SP_APPEND, "(%s) ",
                        enum_color[p->attr & F_COLORMASK]);
            } else if (p->trig.str) Stringcat(buffer, "(trig) ");
            if (p->trig.str)
                Sprintf(buffer, SP_APPEND, "'%q' ", '\'', p->trig.str);
            if (*p->bind)
                Sprintf(buffer, SP_APPEND, "(bind) '%q' ", '\'', ascii_to_print(p->bind));
            if (p->hook) Sprintf(buffer, SP_APPEND, "(hook) %S ", hook_name(p->hook));
            if (*p->name) Sprintf(buffer, SP_APPEND, "%s ", p->name);

        } else {
            if (!file) Sprintf(buffer, 0, "%% %d: /def ", p->num);
            else Stringcpy(buffer, "/def ");
            if (p->invis) Stringcat(buffer, "-i ");
            if ((p->trig.str || p->hook) && p->pri)
                Sprintf(buffer, SP_APPEND, "-p%d ", p->pri);
            if (p->trig.str && p->prob != 100)
                Sprintf(buffer, SP_APPEND, "-c%d ", p->prob);
            if (p->attr & F_GAG) {
                Stringcat(buffer, "-ag");
                if (p->attr & F_NOHISTORY)  Stringadd(buffer, 'G');
                Stringadd(buffer, ' ');
            } else if (p->attr & ~F_NORM) {
                Sprintf(buffer, SP_APPEND, "-a%S ", attr2str(p->attr));
            }
            if (p->subattr & ~F_NORM)
                Sprintf(buffer, SP_APPEND, "-P%d%S ", (int)p->subexp,
                    attr2str(p->subattr & F_HWRITE));
            if (p->trig.str || p->hargs.str || p->wtype.str)
                if (file || p->mflag != matching)
                    Sprintf(buffer, SP_APPEND, "-m%s ", enum_match[p->mflag]);
            if (p->wtype.str)
                Sprintf(buffer, SP_APPEND, "-T'%q' ", '\'', p->wtype.str);
            if (p->world)
                Sprintf(buffer, SP_APPEND, "-w'%q' ", '\'', p->world->name);
            if (p->shots)
                Sprintf(buffer, SP_APPEND, "-n%d ", p->shots);
            if (p->fallthru)
                Stringcat(buffer, "-F ");
            if (p->trig.str)
                Sprintf(buffer, SP_APPEND, "-t'%q' ", '\'', p->trig.str);
            if (p->hook) {
                Sprintf(buffer, SP_APPEND, "-h'%S", hook_name(p->hook));
                if (p->hargs.str)
                    Sprintf(buffer, SP_APPEND, " %q", '\'', p->hargs.str);
                Stringcat(buffer, "' ");
            }
            if (*p->bind) 
                Sprintf(buffer, SP_APPEND, "-b'%q' ", '\'', ascii_to_print(p->bind));
            if (*p->name) Sprintf(buffer, SP_APPEND, "%s ", p->name);
            if (*p->body) Sprintf(buffer, SP_APPEND, "= %s", p->body);
        }

        if (file) tfputs(buffer->s, file);
        else oputs(buffer->s);
    }
    regrelease();
    return result;
}

int save_macros(args)              /* write specified macros to file */
    char *args;
{
    char *p;
    Macro *spec;
    TFILE *file = NULL;
    int result = 1;
    char *mode = "w";

    if (strncmp(args, "-a", 2) == 0 && (args[2] == '\0' || isspace(args[2]))) {
        for (args += 2; isspace(*args); args++);
        mode = "a";
    }
    for (p = args; *p && !isspace(*p); p++);
    if (*p) *p++ = '\0';
    if (!(spec = macro_spec(p))) result = 0;
    if (result && !(check_macro_patterns(spec))) result = 0;
    if (result && !(file = tfopen(expand_filename(args), mode))) {
        operror(args);
        result = 0;
    }

    if (result) {
        oprintf("%% %sing macros to %s", *mode=='w' ? "Writ" : "Append",
            file->name);
        result = list_defs(file, spec, FALSE);
    }
    if (file) tfclose(file);
    if (spec) nuke_macro(spec);
    return result;
}

int handle_list_command(args)             /* list specified macros on screen */
    char *args;
{
    Macro *spec;
    int abbrev = FALSE;
    int result = 1;

    if (strncmp(args, "-s", 2) == 0 && (args[2] == '\0' || isspace(args[2]))) {
        args += 2;
        abbrev = TRUE;
    }
    if (!(spec = macro_spec(args))) result = 0;
    if (result && !check_macro_patterns(spec)) result = 0;
    if (result) result = list_defs(NULL, spec, abbrev);
    if (spec) nuke_macro(spec);
    return result;
}


/**************************
 * Routines to use macros *
 **************************/

static char *world_subs(src)            /* "world_*" subtitutions */
    char *src;
{
    char *ptr = src + 6;
    World *world, *def;

    if (cstrncmp("world_", src, 6) != 0) return NULL;
    if (!(world = xworld())) return "";
    def = get_default_world();

    if (!cstrcmp("name", ptr)) return world->name;
    else if (cstrcmp("character", ptr) == 0) {
        if (*world->character) return world->character;
        else if (def) return def->character;
    } else if (cstrcmp("password", ptr) == 0) {
        if (*world->pass) return world->pass;
        else if (def) return def->pass;
    } else if (cstrcmp("host", ptr) == 0) return world->address;
    else if (cstrcmp("port", ptr) == 0) return world->port;
    else if (cstrcmp("mfile", ptr) == 0) {
        if (*world->mfile) return world->mfile;
        else if (def) return def->mfile;
    } else if (cstrcmp("type", ptr) == 0) {
        if (*world->type) return world->type;
        else if (def) return def->type;
    } else return NULL;
    return "";
}

int do_macro(macro, args)       /* Do a macro! */
    Macro *macro;
    char *args;
{
    int result, old_invis_flag;

    old_invis_flag = invis_flag;
    invis_flag = macro->invis;
    result = process_macro(macro->body, args, SUB_MACRO);
    invis_flag = old_invis_flag;
    return result;
}

char *macro_body(name)                            /* get body of macro */
    char *name;
{
    Macro *m;
    char *result;

    if ((result = world_subs(name))) return result;
    if (!(m = find_macro(name))) return NULL;
    return m->body;
}


/****************************************
 * Routines to check triggers and hooks *
 ****************************************/

/* do_hook
 * Call macros that match <idx> and optionally the filled-in <argfmt>, and
 * prints the message in <fmt>.  Returns the number of matches that were run.
 */
#ifdef HAVE_STDARG
short do_hook(int idx, char *fmt, char *argfmt, ...)     /* do a hook event */
#else
/* VARARGS */
short do_hook(va_alist)
va_dcl
#endif
{
#ifndef HAVE_STDARG
    int idx;
    char *fmt, *argfmt;
#endif
    va_list ap;
    int ran = 0;
    Aline *aline = NULL;
    Stringp buf, args;    /* do_hook is re-entrant; can't use static buffers */

    if (hookflag) {
        Stringinit(args);
#ifdef HAVE_STDARG
        va_start(ap, argfmt);
#else
        va_start(ap);
        idx = va_arg(ap, int);
        fmt = va_arg(ap, char *);
        argfmt = va_arg(ap, char *);
#endif
        vSprintf(args, 0, argfmt, ap);
        va_end(ap);
    }

    if (fmt) {
        Stringinit(buf);
#ifdef HAVE_STDARG
        va_start(ap, argfmt);
#else
        va_start(ap);
        idx = va_arg(ap, int);
        fmt = va_arg(ap, char *);
        argfmt = va_arg(ap, char *);
#endif
        vSprintf(buf, 0, fmt, ap);
        va_end(ap);
        aline = new_aline(buf->s, 0);
    }

    if (hookflag) {
        if (idx == H_SEND) send_hook_level++;
        ran = find_and_run_matches(args->s, (1<<idx), aline);
        if (idx == H_SEND) send_hook_level--;
        Stringfree(args);
    }

    if (fmt) {
        oputa(aline);
        Stringfree(buf);
    }
    return ran;
}

/* Find and run one or more matches for a hook or trig.
 * <text> is text to be matched.  If <hook> is 0, this looks for a trigger;
 * if <hook> is nonzero, it is a hook bit flag.  If <aline> is non-NULL,
 * attributes of matching macros will be applied to <aline>.
 */
int find_and_run_matches(text, hook, aline)
    char *text;
    int hook;
    Aline *aline;
{
    Macro *first = NULL;
    int num = 0;                            /* # of non-fall-thrus */
    int ran = 0;                            /* # of executed macros */
    int lowerlimit = -1;                    /* lowest priority that can match */
    ListEntry *node, *next;

    /* Macros are sorted by decreasing priority, with fall-thrus first.
     * So, we search the list, running each matching fall-thru as we find it;
     * when we find a matching non-fall-thru, we collect any other non-fall-
     * thru matches of the same priority, and select one to run.
     */
    node = hook ? hooklist->head : triglist->head;
    for ( ; node && MAC(node)->pri >= lowerlimit; node = next) {
        next = node->next;
        if (hook && !(MAC(node)->hook & hook)) continue;
        if (MAC(node)->world && MAC(node)->world != xworld()) continue;
        if (MAC(node)->wtype.str) {
            if (!xworld()) continue;
            if (!patmatch(&MAC(node)->wtype, xworld()->type, MAC(node)->mflag, FALSE))
                continue;
        }
        if ((hook && !MAC(node)->hargs.str) ||
          patmatch(&PATTERN(node), text, MAC(node)->mflag, FALSE)) {
            if (MAC(node)->fallthru) {
                /* fall-thru matches can be run right away */
                ran += run_match(MAC(node), text, hook, aline);
                if (aline && !hook)
                    text = aline->str;    /* in case of /substitute */
            } else {
                /* collect list of non-fall-thru matches */
                lowerlimit = MAC(node)->pri;
                num++;
                MAC(node)->tnext = first;
                first = MAC(node);
            }
        }
    }

    /* run exactly one of the non fall-thrus. */
    if (num > 0) {
        if (num > 1)
            for (num = RRAND(0, num-1); num; num--)
                first = first->tnext;
        /* use regexp of the selected macro, not the last matched macro */
        reghold(hook ? first->hargs.re : first->trig.re, text, FALSE);
        ran += run_match(first, text, hook, aline);
    }

    regrelease();  /* we can't let reginfo point to possibly dying data */
    return ran;
}

/* run a macro that has been selected by a trigger or hook */
static int run_match(macro, text, hook, aline)
    Macro *macro;   /* macro to run */
    char *text;     /* argument text */
    int hook;       /* hook vector */
    Aline *aline;   /* aline to which attributes are applied */
{
    int ran = 0;

    /* Apply attributes (full and partial) to aline. */
    if (aline) {
        regexp *re = macro->trig.re;
        aline->attrs |= macro->attr;
        if (macro->subattr && aline->len && re->startp[0] < re->endp[0]) {
            int i;
            short n = macro->subexp;
            if (!aline->partials) {
                aline->partials = (short*)MALLOC(sizeof(short)*aline->len);
                for (i = 0; i < aline->len; ++i) aline->partials[i] = 0;
            }
            do {
                for (i = re->startp[n] - text; i < re->endp[n] - text; ++i)
                    aline->partials[i] |= macro->subattr;
            } while (*re->endp[0] &&
                patmatch(&macro->trig, re->endp[0], macro->mflag, FALSE));
        }
    }

    /* Execute the macro. */
    if ((hook && hookflag) || (!hook && borg)) {
        if (macro->prob == 100 || RRAND(0, 99) < macro->prob) {
            if (macro->shots && !--macro->shots) kill_macro(macro);
            if (*macro->body) {
                do_macro(macro, text);
                ran++;
            }
        }
    }

    return ran;
}

#ifdef DMALLOC
void free_macros()
{
    while (maclist->head) nuke_macro((Macro *)maclist->head->datum);
    free_hash(macro_table);
}
#endif

