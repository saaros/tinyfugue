/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: macro.c,v 35004.75 1999/01/31 00:27:46 hawkeye Exp $ */


/**********************************************
 * Fugue macro package                        *
 *                                            *
 * Macros, hooks, triggers, hilites and gags  *
 * are all processed here.                    *
 **********************************************/

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "search.h"
#include "world.h"
#include "macro.h"
#include "keyboard.h"	/* bind_key()... */
#include "expand.h"
#include "socket.h"	/* xworld() */
#include "output.h"	/* get_keycode() */
#include "commands.h"
#include "command.h"
#include "parse.h"	/* valbool() for /def -E */

int invis_flag = 0;

static Macro  *FDECL(macro_spec,(CONST char *args, int *mflag, int allowshort));
static int     FDECL(macro_match,(Macro *spec, Macro *macro, Pattern *aux_pat));
static int     FDECL(complete_macro,(Macro *spec));
static Pattern*FDECL(init_aux_patterns,(Macro *spec, int mflag));
static Macro  *FDECL(match_exact,(int ishook, CONST char *str, long flags));
static int     FDECL(list_defs,(TFILE *file, Macro *spec, int mflag));
static int     FDECL(run_match,(Macro *macro, CONST char *text, long hook,
                     Aline *aline));
static String *FDECL(hook_name,(long hook)) PURE;
static String *FDECL(attr2str,(attr_t attrs)) PURE;
static int     FDECL(rpricmp,(CONST Macro *m1, CONST Macro *m2));
static void    FDECL(nuke_macro,(Macro *macro));


#define HASH_SIZE 197

#define MACRO_TEMP	001
#define MACRO_DEAD	002
#define MACRO_SHORT	004

static List maclist[1];			/* list of all (live) macros */
static List triglist[1];		/* list of macros by trigger */
static List hooklist[1];		/* list of macros by hook */
static Macro *dead_macros;		/* head of list of dead macros */
static HashTable macro_table[1];	/* macros hashed by name */
static World NoWorld, AnyWorld;		/* explicit "no" and "any" */
static int mnum = 0;			/* macro ID number */

static CONST char *hook_table[] = {
#define bicode(a, b)  b 
#include "hooklist.h"
#undef bicode
};

#define NONNULL(str) ((str) ? (str) : "")

/* These macros allow easy sharing of trigger and hook code. */
#define MAC(Node)       ((Macro *)(Node->datum))


void init_macros()
{
    init_hashtable(macro_table, HASH_SIZE, cstrstructcmp);
    init_list(maclist);
    init_list(triglist);
    init_list(hooklist);
}

/***************************************
 * Routines for parsing macro commands *
 ***************************************/

attr_t parse_attrs(argp)    /* convert attr string to bitfields */
    char **argp;  /* We don't write **argp, but we can't declare it CONST**. */
{
    attr_t attrs = 0, color;
    char *end, *name;
    char buf[16];

    while (**argp) {
        ++*argp;
        switch((*argp)[-1]) {
        case ',':  /* skip */            break;
        case 'n':  attrs |= F_NONE;      break;
        case 'x':  attrs |= F_EXCLUSIVE; break;
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
            end = strchr(*argp, ',');
            if (end && end - *argp < sizeof(buf)) {
                name = strncpy(buf, *argp, end - *argp);
                name[end-*argp] = '\0';
                *argp = end + 1;
            } else {
                name = *argp;
                while (**argp) ++*argp;
            }
            if ((color = enum2int(name, enum_color, "color")) < 0)
                return -1;
            attrs |= color2attr(color);
            break;
        default:
            eprintf("invalid display attribute '%c'", (*argp)[-1]);
            return -1;
        }
    }
    return attrs;
}

/* Convert hook string to bit vector; return -1 on error. */
long parse_hook(argp)
    char **argp;
{
    char *in, state;
    CONST char **hookname;
    long result = 0;

    if (!**argp) return ALL_HOOKS;
    for (state = '|'; state == '|'; *argp = in) {
        for (in = *argp; *in && !is_space(*in) && *in != '|'; ++in);
        state = *in;
        *in++ = '\0';
        if (strcmp(*argp, "*") == 0) result = ALL_HOOKS;
        if (strcmp(*argp, "0") == 0) result = 0;
        else {
            hookname = (CONST char **)binsearch((GENERIC*)*argp,
                (GENERIC *)hook_table, NUM_HOOKS, sizeof(char*), cstrstructcmp);
            if (!hookname) {
                eprintf("invalid hook event \"%s\"", *argp);
                return -1;
            }
            result |= (1L << (hookname - hook_table));
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
static Macro *macro_spec(args, xmflag, allowshort)
    CONST char *args;
    int *xmflag, allowshort;
{
    Macro *spec;
    char opt, *ptr, *name = NULL;
    long num;
    int i, mflag = matching, error = 0;

    if (!(spec = (Macro *)MALLOC(sizeof(struct Macro)))) {
        eprintf("macro_spec: not enough memory");
        return NULL;
    }
    spec->num = 0;
    spec->name = spec->body = spec->bind = spec->keyname = spec->expr = NULL;
    spec->numnode = spec->trignode = spec->hooknode = spec->hashnode = NULL;
    init_pattern_str(&spec->trig, NULL);
    init_pattern_str(&spec->hargs, NULL);
    init_pattern_str(&spec->wtype, NULL);
    spec->world = NULL;
    spec->pri = spec->prob = spec->shots = spec->fallthru = spec->quiet = -1;
    spec->hook = -1;
    spec->invis = 0;
    spec->attr = spec->subattr = 0;
    spec->subexp = -1;
    spec->flags = MACRO_TEMP;

    startopt(args, "sp#c#b:B:E:t:w:h:a:f:P:T:FiIn#1m:q" + !allowshort);
    while (!error && (opt = nextopt(&ptr, &num))) {
        switch (opt) {
        case 's':
            spec->flags |= MACRO_SHORT;
            break;
        case 'm':
            error = ((mflag = enum2int(ptr, enum_match, "-m")) < 0);
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
            if (spec->keyname) FREE(spec->keyname);
            if (spec->bind) FREE(spec->bind);
            ptr = print_to_ascii(ptr);
            spec->bind = STRDUP(ptr);
            break;
        case 'B':
            if (spec->keyname) FREE(spec->keyname);
            if (spec->bind) FREE(spec->bind);
            spec->keyname = STRDUP(ptr);
            break;
        case 'E':
            if (spec->expr) FREE(spec->expr);
            spec->expr = STRDUP(ptr);
            break;
        case 't':
            free_pattern(&spec->trig);
            error += !init_pattern_str(&spec->trig, ptr);
            break;
        case 'T':
            free_pattern(&spec->wtype);
            error += !init_pattern_str(&spec->wtype, ptr);
            break;
        case 'w':
            if (!*ptr || strcmp(ptr, "+") == 0)
                spec->world = &AnyWorld;
            else if (strcmp(ptr, "-") == 0)
                spec->world = &NoWorld;
            else if ((error = !(spec->world = find_world(ptr))))
                eprintf("No world %s", ptr);
            break;
        case 'h':
            if (!(error = ((spec->hook = parse_hook(&ptr)) < 0))) {
                free_pattern(&spec->hargs);
                error += !init_pattern_str(&spec->hargs, ptr);
            }
            break;
        case 'a': case 'f':
            error = ((spec->attr |= parse_attrs(&ptr)) < 0);
            break;
        case 'P':
            i = strtoint(&ptr);
            mflag = MATCH_REGEXP;
            if ((error = (spec->subexp >= 0 && i != spec->subexp))) {
                eprintf("-P: Only one subexpression can be hilited per macro.");
            } else if ((error = (i < 0 || i >= NSUBEXP))) {
                eprintf("-P: number must be between 0 and %d.", NSUBEXP - 1);
            } else {
                spec->subexp = i;
                error = ((spec->subattr |= parse_attrs(&ptr)) < 0);
            }
            break;
        case 'n':
            spec->shots = num;
            break;
        case 'q':
            spec->quiet = TRUE;
            break;
        case '1':
            spec->shots = 1;
            break;
        default:
            error = TRUE;
        }
    }

    if (error) {
        nuke_macro(spec);
        return NULL;
    }
    if (xmflag) *xmflag = mflag;
    init_pattern_mflag(&spec->trig, mflag);
    init_pattern_mflag(&spec->hargs, mflag);
    init_pattern_mflag(&spec->wtype, mflag);

    if (!*ptr) return spec;
    name = ptr;

    if ((ptr = strchr(ptr, '='))) {
        *ptr++ = '\0';
        ptr = stripstr(ptr);
        spec->body = STRDUP(ptr);
    }
    name = stripstr(name);
    spec->name = *name ? STRDUP(name) : NULL;

    return spec;
}


/* init_aux_patterns
 * Macro_match() needs to compare some string fields that aren't normally
 * patterns (name, body, bind, keyname, expr).  This function initializes
 * patterns for those fields.  The result is stored in a static array of
 * patterns, which is freed and reused each time this function is called.
 */
static Pattern *init_aux_patterns(spec, mflag)
    Macro *spec;
    int mflag;
{
    static Pattern aux[5] =
        {{NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL}, {NULL, NULL}};

    free_pattern(&aux[0]);
    free_pattern(&aux[1]);
    free_pattern(&aux[2]);
    free_pattern(&aux[3]);
    free_pattern(&aux[4]);
    if (mflag < 0) mflag = matching;
    if      (!init_pattern(&aux[0], spec->name, mflag)) return NULL;
    else if (!init_pattern(&aux[1], spec->body, mflag)) return NULL;
    else if (!init_pattern(&aux[2], spec->bind, mflag)) return NULL;
    else if (!init_pattern(&aux[3], spec->keyname, mflag)) return NULL;
    else if (!init_pattern(&aux[4], spec->expr, mflag)) return NULL;
    return aux;
}

/* macro_match
 * Compares spec to macro.  aux_pat is an array of patterns for string
 * fields that aren't normally patterns (name, body, bind, keyname, expr).
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
    if (spec->attr) {
        if (spec->attr == F_NONE && macro->attr) return 1;
        if (spec->attr != F_NONE && (spec->attr & macro->attr) == 0) return 1;
    }
    if (spec->subexp >= 0 && macro->subexp < 0) return 1;
    if (spec->subattr) {
        if (spec->subattr == F_NONE && macro->subattr) return 1;
        if (spec->subattr != F_NONE && (spec->subattr & macro->subattr) == 0) return 1;
    }
    if (spec->world) {
        if (spec->world == &NoWorld) {
            if (macro->world) return 1;
        } else if (spec->world == &AnyWorld) {
            if (!macro->world) return 1;
        } else if (spec->world != macro->world) return 1;
    }

    if (spec->keyname) {
        if (!*spec->keyname) {
            if (!*macro->keyname) return 1;
        } else {
            if (!patmatch(&aux[3], macro->keyname)) return 1;
        }
    }

    if (spec->bind) {
        if (!*spec->bind) {
            if (!*macro->bind) return 1;
        } else {
            if (!patmatch(&aux[2], macro->bind)) return 1;
        }
    }

    if (spec->expr) {
        if (!*spec->expr) {
            if (!*macro->expr) return 1;
        } else {
            if (!patmatch(&aux[4], macro->expr)) return 1;
        }
    }

    if (!spec->hook && macro->hook > 0) return 1;
    if (spec->hook > 0) {
        if ((spec->hook & macro->hook) == 0) return 1;
        if (spec->hargs.str && *spec->hargs.str) {
            if (!patmatch(&spec->hargs, NONNULL(macro->hargs.str)))
                return 1;
        }
    }
    if (spec->trig.str) {
        if (!*spec->trig.str) {
            if (!macro->trig.str) return 1;
        } else {
            if (!patmatch(&spec->trig, NONNULL(macro->trig.str)))
                return 1;
        }
    }
    if (spec->wtype.str) {
        if (!*spec->wtype.str) {
            if (!macro->wtype.str) return 1;
        } else {
            if (!patmatch(&spec->wtype, NONNULL(macro->wtype.str)))
                return 1;
        }
    }
    if (spec->num && macro->num != spec->num)
        return 1;
    if (spec->name && !patmatch(&aux[0], macro->name))
        return 1;
    if (spec->body && !patmatch(&aux[1], macro->body))
        return 1;
    return 0;
}

Macro *find_macro(name)              /* find Macro by name */
    CONST char *name;
{
    if (!*name) return NULL;
    if (*name == '#') return find_num_macro(atoi(name + 1));
    return (Macro *)hash_find(name, macro_table);
}

static Macro *match_exact(ishook, str, flags)   /* find single exact match */
    int ishook;
    long flags;
    CONST char *str;
{
    ListEntry *node;
    long mac_flags;
    Pattern *pattern;
    Macro *macro;
  
    if ((ishook && !flags) || (!ishook && !*str)) return NULL;
    for (node = ishook ? hooklist->head : triglist->head; node; node=node->next)
    {
        macro = MAC(node);
        if (macro->flags & MACRO_DEAD) continue;
        mac_flags = ishook ? macro->hook : macro->attr;
        pattern = ishook ? &macro->hargs : &macro->trig;
        if ((mac_flags & flags) &&
            (!pattern->str || cstrcmp(pattern->str, str) == 0))
                break;
    }
    if (node) return macro;
    eprintf("%s on \"%s\" was not defined.", ishook ? "Hook" : "Trigger", str);
    return NULL;
}

/**************************
 * Routines to add macros *
 **************************/

/* create a Macro */
Macro *new_macro(trig, bind, hook, hargs, body, pri, prob, attr, invis, mflag)
    CONST char *trig, *bind, *body, *hargs;
    int pri, prob, invis, mflag;
    long hook;
    attr_t attr;
{
    Macro *new;
    int error = 0;

    if (!(new = (Macro *) MALLOC(sizeof(struct Macro)))) {
        eprintf("new_macro: not enough memory");
        return NULL;
    }
    new->numnode = new->trignode = new->hooknode = new->hashnode = NULL;
    new->name = STRDUP("");
    new->body = STRDUP(body);
    new->bind = STRDUP(bind);
    new->keyname = STRDUP("");
    new->expr = STRDUP("");
    new->hook = hook;
    error += !init_pattern(&new->trig, trig, mflag);
    error += !init_pattern(&new->hargs, hargs, mflag);
    init_pattern_str(&new->wtype, NULL);
    new->world = NULL;
    new->pri = pri;
    new->prob = prob;
    new->attr = attr;
    new->subexp = -1;
    new->subattr = 0;
    new->shots = 0;
    new->invis = invis;
    new->fallthru = FALSE;
    new->quiet = FALSE;
    new->flags = MACRO_TEMP;

    if (!error) return new;
    nuke_macro(new);
    return NULL;
}

/* add_macro
 * Install a permanent Macro in appropriate structures.
 * Only the keybinding is checked for conflicts; everything else is assumed
 * assumed to be error- and conflict-free.  If the bind_key fails, the
 * macro will be nuked.
 */
int add_macro(macro)
    Macro *macro;
{
    if (!macro) return 0;
    if (*macro->bind && !bind_key(macro)) {
        nuke_macro(macro);
        return 0;
    }
    macro->num = ++mnum;
    macro->numnode = inlist((GENERIC *)macro, maclist, NULL);
    if (*macro->name)
        macro->hashnode = hash_insert((GENERIC *)macro, macro_table);
    if (macro->trig.str)
        macro->trignode = sinsert((GENERIC *)macro, triglist, (Cmp *)rpricmp);
    if (macro->hook)
        macro->hooknode = sinsert((GENERIC *)macro, hooklist, (Cmp *)rpricmp);
    macro->flags &= ~MACRO_TEMP;
    if (!*macro->name && (macro->trig.str || macro->hook) && macro->shots == 0 && pedantic) {
        eprintf("warning: new macro (#%d) does not have a name.", macro->num);
    }
    return macro->num;
}

/* rebind_key_macros
 * Unbinds macros with keynames, and attempts to rebind them.
 */
void rebind_key_macros()
{
    Macro *p;
    ListEntry *node;
    CONST char *code;

    for (node = maclist->tail; node; node = node->prev) {
        p = MAC(node);
        if (!*p->keyname) continue;
        code = get_keycode(p->keyname);
        if (strcmp(code, p->bind) == 0) {
            /* same code, don't need to rebind */
        } else {
            if (*p->bind) unbind_key(p);
            FREE(p->bind);
            p->bind = STRDUP(code);
            if (!*code)
                eprintf("warning: no code for key \"%s\"", p->keyname);
            else if (bind_key(p))
                do_hook(H_REDEF, "!Redefined %s %s", "%s %s",
                    "key", p->keyname);
            else
                kill_macro(p);
        }
    }
}

/* compares m1 and m2 based on reverse priority and fallthru */
static int rpricmp(m1, m2)
    CONST Macro *m1, *m2;
{
    if (m2->pri != m1->pri) return m2->pri - m1->pri;
    else return m2->fallthru - m1->fallthru;
}

struct Value *handle_def_command(args)
    char *args;
{
    Macro *spec;

    if (!*args || !(spec = macro_spec(args, NULL, FALSE))) return newint(0);
    return newint(complete_macro(spec));
}

/* Fill in "don't care" fields with default values, and add_macro().
 * If error checking fails, spec will be nuked.
 */
static int complete_macro(spec)
    Macro *spec;
{
    Macro *macro = NULL;

    if (spec->name) {
        if (strchr("#@!/", *spec->name) || strchr(spec->name, ' ')) {
            eprintf("illegal macro name \"%s\".", spec->name);
            nuke_macro(spec);
            return 0;
        }
        if (keyword(spec->name)) {
            eprintf("\"%s\" is a reserved word.", spec->name);
            nuke_macro(spec);
            return 0;
        }

        if (find_command(spec->name)) {
            do_hook(H_CONFLICT,
                "!warning: macro \"%s\" conflicts with the builtin command.",
                "%s", spec->name);
        }
    }

    if (spec->world == &AnyWorld) spec->world = xworld();
    if (spec->pri < 0) spec->pri = 1;
    if (spec->prob < 0) spec->prob = 100;
    if (spec->shots < 0) spec->shots = 0;
    if (spec->invis) spec->invis = 1;
    if (spec->hook < 0) spec->hook = 0;
    if (spec->fallthru < 0) spec->fallthru = 0;
    if (spec->quiet < 0) spec->quiet = 0;
    spec->attr &= ~F_NONE;
    spec->subattr &= ~F_NONE;
    if (!spec->name) spec->name = STRNDUP("", 0);
    if (!spec->body) spec->body = STRNDUP("", 0);
    if (!spec->expr) spec->expr = STRNDUP("", 0);

    if (!spec->keyname) spec->keyname = STRNDUP("", 0);
    else if (*spec->keyname) {
        if (spec->bind) FREE(spec->bind);
        spec->bind = get_keycode(spec->keyname);
        if (!spec->bind) {
            eprintf("unknown key name \"%s\".", spec->keyname);
            nuke_macro(spec);
            return 0;
        }
        spec->bind = STRDUP(spec->bind);
        if (!*spec->bind)
            eprintf("warning: no code for key \"%s\".", spec->keyname);
    }

    if (!spec->bind) spec->bind = STRNDUP("", 0);

    if (spec->subexp >= 0 && spec->trig.mflag != MATCH_REGEXP) {
        eprintf("\"-P\" requires \"-mregexp -t<pattern>\"");
        nuke_macro(spec);
        return 0;
    }

    if (*spec->name && (macro = find_macro(spec->name)) && !redef) {
        eprintf("macro %s already exists", macro->name);
        nuke_macro(spec);
        return 0;
    }
    if (!add_macro(spec)) return 0;
    if (macro) {
        do_hook(H_REDEF, "!Redefined %s %s", "%s %s", "macro", macro->name);
        kill_macro(macro);
    }
    return spec->num;
}

int add_hook(args, body)                  /* define a new Macro with hook */
    char *args;
    CONST char *body;
{
    long hook;

    if ((hook = parse_hook(&args)) < 0) return 0;
    if (!*args) args = NULL;
    return add_macro(new_macro(NULL, "", hook, args, body, 0, 100, 0, 0,
        matching));
}

/* /edit: Edit an existing macro.
 * Actually editing the macro in place is quite hairy, so instead we
 * remove the old one, create a replacement and add it.  If the replacement
 * fails, we re-add the original.  BUG(?): In either case, the macro gets
 * renumbered.  We can't just re-use the old number, because the macro
 * would be out of order in maclist.
 */
struct Value *handle_edit_command(args)
    char *args;
{
    Macro *spec, *macro = NULL;
    int error = 0;

    if (!*args || !(spec = macro_spec(args, NULL, FALSE))) {
        return newint(0);
    } else if (!spec->name) {
        eprintf("You must specify a macro.");
    } else if (spec->name[0] == '$') {
        macro = match_exact(FALSE, spec->name + 1, F_ATTR);
    } else if (!(macro = find_macro(spec->name))) {
        eprintf("macro %s does not exist", spec->name);
    }

    if (!macro) {
        nuke_macro(spec);
        return newint(0);
    }

    kill_macro(macro);

    FREE(spec->name);
    spec->name = STRDUP(macro->name);

    if (!spec->body) spec->body = STRDUP(macro->body);
    if (!spec->bind) spec->bind = STRDUP(macro->bind);
    if (!spec->keyname) spec->keyname = STRDUP(macro->keyname);
    if (!spec->expr) spec->expr = STRDUP(macro->expr);
    if (!spec->wtype.str && macro->wtype.str)
        error += !copy_pattern(&spec->wtype, &macro->wtype);
    if (!spec->trig.str && macro->trig.str)
        error += !copy_pattern(&spec->trig, &macro->trig);
    if (spec->hook < 0) {
        spec->hook = macro->hook;
        if (macro->hargs.str)
            error += !copy_pattern(&spec->hargs, &macro->hargs);
    }
    if (!spec->world) spec->world = macro->world;
    else if (spec->world == &AnyWorld) spec->world = xworld();
    if (spec->pri < 0) spec->pri = macro->pri;
    if (spec->prob < 0) spec->prob = macro->prob;
    if (spec->shots < 0) spec->shots = macro->shots;
    if (spec->fallthru < 0) spec->fallthru = macro->fallthru;
    if (spec->quiet < 0) spec->quiet = macro->quiet;
    if (spec->attr == 0) spec->attr = macro->attr;
    if (spec->subexp < 0 && macro->subexp >= 0) {
        spec->subexp = macro->subexp;
        spec->subattr = macro->subattr;
    }

    if (!error) {
        complete_macro(spec);
        return newint(spec->num);
    }

    /* Edit failed.  Resurrect original macro. */
    macro = dead_macros;
    macro->flags &= ~MACRO_DEAD;
    dead_macros = macro->tnext;
    add_macro(macro);
    return newint(0);
}


/********************************
 * Routines for removing macros *
 ********************************/

void kill_macro(macro)
    Macro *macro;
{
   /* Remove macro from maclist, macro_table, and key_trie, and put it on
    * the dead_macros list.  When called from find_and_run_matches(), this
    * allows a new macro to be defined without conflicting with the name
    * or binding of this macro.
    * The macro must NOT be removed from triglist and hooklist, so
    * find_and_run_matches() can work correctly when a macro kills itself,
    * is a one-shot, or defines another macro that is inserted immediately
    * after it in triglist/hooklist.  macro will be removed from triglist
    * and hooklist in nuke_macro().
    */

    if (macro->flags & MACRO_DEAD) return;
    macro->flags |= MACRO_DEAD;
    macro->tnext = dead_macros;
    dead_macros = macro;
    unlist(macro->numnode, maclist);
    if (*macro->name) hash_remove(macro->hashnode, macro_table);
    if (*macro->bind) unbind_key(macro);
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
    if (!(macro->flags & MACRO_DEAD) && !(macro->flags & MACRO_TEMP)) {
        kill_macro(macro);
    }
    if (macro->trignode) unlist(macro->trignode, triglist);
    if (macro->hooknode) unlist(macro->hooknode, hooklist);

    if (macro->name) FREE(macro->name);
    if (macro->body) FREE(macro->body);
    if (macro->expr) FREE(macro->expr);
    if (macro->bind) FREE(macro->bind);
    if (macro->keyname) FREE(macro->keyname);
    free_pattern(&macro->trig);
    free_pattern(&macro->hargs);
    free_pattern(&macro->wtype);
    FREE(macro);
}

int remove_macro(str, flags, byhook)        /* delete a macro */
    char *str;
    long flags;
    int byhook;
{
    Macro *macro;
    char *args;

    if (byhook) {
        args = str;
        if ((flags = parse_hook(&args)) < 0) return 0;
        macro = match_exact(TRUE, args, flags);
    } else if (flags) {
        macro = match_exact(FALSE, str, flags);
    } else {
        if (!(macro = find_macro(str)))
            eprintf("Macro \"%s\" was not defined.", str);
    }
    if (!macro) return 0;
    kill_macro(macro);
    return 1;
}

struct Value *handle_purge_command(args)      /* delete all specified macros */
    char *args;
{
    Macro *spec;
    ListEntry *node, *next;
    Pattern *aux_pat;
    int result = 1;
    int mflag;

    if (!(spec = macro_spec(args, &mflag, FALSE))) return newint(0);
    if (spec->name && *spec->name == '#') {
        spec->num = atoi(spec->name + 1);
        FREE(spec->name);
        spec->name = NULL;
    }
    if (!(aux_pat = init_aux_patterns(spec, mflag))) result = 0;
    else {
        for (node = maclist->head; node; node = next) {
            next = node->next;
            if (macro_match(spec, MAC(node), aux_pat) == 0)
                kill_macro(MAC(node));
        }
    }
    /* regrelease(); */
    nuke_macro(spec);
    return newint(result);
}

struct Value *handle_undefn_command(args)          /* delete macro by number */
    char *args;
{
    int num, result = 0;
    Macro *macro;

    while (*args) {
        if ((num = numarg(&args)) >= 0 && (macro = find_num_macro(num))) {
            kill_macro(macro);
            result++;
        }
    }
    return newint(result);
}

Macro *find_num_macro(num)
    int num;
{
    ListEntry *node;

    for (node = maclist->head; node; node = node->next) {
        if (MAC(node)->num == num) return MAC(node);
        /* Macros are in decending numeric order, so we can stop if passed. */
        if (MAC(node)->num < num) break;
    }
    eprintf("no macro with number %d", num);
    return NULL;
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
    long hook;
{
    int n;
    STATIC_BUFFER(buf);

    Stringterm(buf, 0);
    for (n = 0; n < (int)NUM_HOOKS; n++) {
                 /* ^^^^^ Some brain dead compilers need that cast */
        if (!((1L << n) & hook)) continue;
        if (buf->len) Stringadd(buf, '|');
        Stringcat(buf, hook_table[n]);
    }
    return buf;
}

static String *attr2str(attrs)
    attr_t attrs;
{
    STATIC_BUFFER(buffer);

    Stringterm(buffer, 0);
    if (attrs & F_NONE)      Stringadd(buffer, 'n');
    if (attrs & F_EXCLUSIVE) Stringadd(buffer, 'x');
    if (attrs & F_GAG)       Stringadd(buffer, 'g');
    if (attrs & F_NOHISTORY) Stringadd(buffer, 'G');
    if (attrs & F_UNDERLINE) Stringadd(buffer, 'u');
    if (attrs & F_REVERSE)   Stringadd(buffer, 'r');
    if (attrs & F_FLASH)     Stringadd(buffer, 'f');
    if (attrs & F_DIM)       Stringadd(buffer, 'd');
    if (attrs & F_BOLD)      Stringadd(buffer, 'B');
    if (attrs & F_BELL)      Stringadd(buffer, 'b');
    if (attrs & F_HILITE)    Stringadd(buffer, 'h');
    if (attrs & F_FGCOLOR)
        Sprintf(buffer, SP_APPEND, "C%s", enum_color[attr2fgcolor(attrs)]);
    if (attrs & F_BGCOLOR) {
        if (attrs & F_FGCOLOR) Stringadd(buffer, ',');
        Sprintf(buffer, SP_APPEND, "C%s", enum_color[attr2bgcolor(attrs)]);
    }
    return buffer;
}

static int list_defs(file, spec, mflag)  /* list all specified macros */
    TFILE *file;
    Macro *spec;
    int mflag;
{
    Macro *p;
    ListEntry *node;
    Pattern *aux_pat;
    STATIC_BUFFER(buffer);
    int result = 0;

    if (!(aux_pat = init_aux_patterns(spec, mflag))) return 0;
    if (spec->name && *spec->name == '#') {
        spec->num = atoi(spec->name + 1);
        FREE(spec->name);
        spec->name = NULL;
    }

    /* maclist is in reverse numeric order, so we start from tail */
    for (node = maclist->tail; node; node = node->prev) {
        p = MAC(node);
        if (macro_match(spec, p, aux_pat) != 0) continue;
        result = p->num;
        mflag = -1;

        if (spec->flags & MACRO_SHORT) {
            Sprintf(buffer, 0, "%% %d: ", p->num);
            if (p->attr & F_NOHISTORY) Stringcat(buffer, "(norecord) ");
            if (p->attr & F_GAG) Stringcat(buffer, "(gag) ");
            else if (p->attr & (F_HWRITE | F_EXCLUSIVE)) {
                if (p->attr & F_UNDERLINE) Stringcat(buffer, "(underline) ");
                if (p->attr & F_REVERSE)   Stringcat(buffer, "(reverse) ");
                if (p->attr & F_FLASH)     Stringcat(buffer, "(flash) ");
                if (p->attr & F_DIM)       Stringcat(buffer, "(dim) ");
                if (p->attr & F_BOLD)      Stringcat(buffer, "(bold) ");
                if (p->attr & F_BELL)      Stringcat(buffer, "(bell) ");
                if (p->attr & F_HILITE)    Stringcat(buffer, "(hilite) ");
                if (p->attr & F_FGCOLOR)
                    Sprintf(buffer, SP_APPEND, "(%s) ",
                        enum_color[attr2fgcolor(p->attr)]);
                if (p->attr & F_BGCOLOR)
                    Sprintf(buffer, SP_APPEND, "(%s) ",
                        enum_color[attr2bgcolor(p->attr)]);
            } else if (p->subattr & (F_HWRITE | F_EXCLUSIVE)) {
                Stringcat(buffer, "(partial) ");
            } else if (p->trig.str) {
                Stringcat(buffer, "(trig) ");
            }
            if (p->trig.str)
                Sprintf(buffer, SP_APPEND, "'%q' ", '\'', p->trig.str);
            if (*p->keyname) {
                Sprintf(buffer, SP_APPEND, "(key) '%s' ", p->keyname);
            } else if (*p->bind) {
                Sprintf(buffer, SP_APPEND, "(bind) '%q' ", '\'',
                    ascii_to_print(p->bind));
            }
            if (p->hook)
                Sprintf(buffer, SP_APPEND, "(hook) %S ", hook_name(p->hook));
            if (*p->name) Sprintf(buffer, SP_APPEND, "%s ", p->name);

        } else {
            if (!file) Sprintf(buffer, 0, "%% %d: /def ", p->num);
            else Stringcpy(buffer, "/def ");
            if (p->invis) Stringcat(buffer, "-i ");
            if (p->trig.str || p->hook)
                Sprintf(buffer, SP_APPEND, "-%sp%d ",
                    p->fallthru ? "F" : "", p->pri);
            if (p->trig.str && p->prob != 100)
                Sprintf(buffer, SP_APPEND, "-c%d ", p->prob);
            if (p->attr) {
                Sprintf(buffer, SP_APPEND, "-a%S ", attr2str(p->attr));
            }
            if (p->subattr) {
                mflag = MATCH_REGEXP;
                Sprintf(buffer, SP_APPEND, "-P%d%S ", (int)p->subexp,
                    attr2str(p->subattr));
            }
            if (p->shots)
                Sprintf(buffer, SP_APPEND, "-n%d ", p->shots);
            if (p->world)
                Sprintf(buffer, SP_APPEND, "-w'%q' ", '\'', p->world->name);
            if (p->wtype.str) {
                if (p->wtype.mflag != mflag)
                    Sprintf(buffer, SP_APPEND, "-m%s ", enum_match[p->wtype.mflag]);
                mflag = p->wtype.mflag;
                Sprintf(buffer, SP_APPEND, "-T'%q' ", '\'', p->wtype.str);
            }

            if (*p->expr) 
                Sprintf(buffer, SP_APPEND, "-E'%q' ", '\'', p->expr);

            if (p->trig.str) {
                if (p->trig.mflag != mflag)
                    Sprintf(buffer, SP_APPEND, "-m%s ", enum_match[p->trig.mflag]);
                mflag = p->trig.mflag;
                Sprintf(buffer, SP_APPEND, "-t'%q' ", '\'', p->trig.str);
            }
            if (p->hook) {
                if (p->hargs.mflag != mflag)
                    Sprintf(buffer, SP_APPEND, "-m%s ", enum_match[p->hargs.mflag]);
                mflag = p->hargs.mflag;
                Sprintf(buffer, SP_APPEND, "-h'%S", hook_name(p->hook));
                if (p->hargs.str)
                    Sprintf(buffer, SP_APPEND, " %q", '\'', p->hargs.str);
                Stringcat(buffer, "' ");
            }

            if (*p->keyname) 
                Sprintf(buffer, SP_APPEND, "-B'%s' ", p->keyname);
            else if (*p->bind) 
                Sprintf(buffer, SP_APPEND, "-b'%q' ", '\'', ascii_to_print(p->bind));

            if (p->quiet) Stringcat(buffer, "-q ");
            if (*p->name == '-') Stringcat(buffer, "- ");
            if (*p->name) Sprintf(buffer, SP_APPEND, "%s ", p->name);
            if (*p->body) Sprintf(buffer, SP_APPEND, "= %s", p->body);
        }

        if (file) tfputs(buffer->s, file);
        else oputs(buffer->s);
    }
    /* regrelease(); */
    return result;
}

int save_macros(args)              /* write specified macros to file */
    char *args;
{
    Macro *spec;
    TFILE *file = NULL;
    int result = 1;
    CONST char *name, *mode = "w";
    char opt;
    int mflag;

    startopt(args, "a");
    while ((opt = nextopt(&args, NULL))) {
        if (opt != 'a') return 0;
        mode = "a";
    }

    name = stringarg(&args, NULL);
    if (!(spec = macro_spec(args, &mflag, FALSE))) result = 0;
    if (result && !(file = tfopen(expand_filename(name), mode))) {
        operror(args);
        result = 0;
    }

    if (result) {
        oprintf("%% %sing macros to %s", *mode=='w' ? "Writ" : "Append",
            file->name);
        result = list_defs(file, spec, mflag);
    }
    if (file) tfclose(file);
    if (spec) nuke_macro(spec);
    return result;
}

struct Value *handle_list_command(args)   /* list specified macros on screen */
    char *args;
{
    Macro *spec;
    int result = 1;
    int mflag;

    if (!(spec = macro_spec(args, &mflag, TRUE))) result = 0;
    if (result) result = list_defs(NULL, spec, mflag);
    if (spec) nuke_macro(spec);
    return newint(result);
}


/**************************
 * Routines to use macros *
 **************************/

int do_macro(macro, args)       /* Do a macro! */
    Macro *macro;
    CONST char *args;
{
    int result, old_invis_flag;
    CONST char *command;
    char numbuf[16];

    if (*macro->name) {
        command = macro->name;
    } else {
        sprintf(numbuf, "#%d", macro->num);
        command = numbuf;
    }
    old_invis_flag = invis_flag;
    invis_flag = macro->invis;
    result = process_macro(macro->body, args, SUB_MACRO, command);
    invis_flag = old_invis_flag;
    return result;
}

CONST char *macro_body(name)                            /* get body of macro */
    CONST char *name;
{
    Macro *m;
    CONST char *body;

    if (strncmp("world_", name, 6) == 0 && (body = world_info(NULL, name + 6)))
        return body;
    if (!(m = find_macro(name))) return NULL;
    return m->body;
}


/****************************************
 * Routines to check triggers and hooks *
 ****************************************/

/* do_hook
 * Call macros that match <idx> and optionally the filled-in <argfmt>, and
 * prints the message in <fmt>.  Returns the number of matches that were run.
 * A leading '!' in <fmt> is replaced with "% ", file name, and line number.
 * Note that calling do_hook() makes the caller non-atomic; be careful.
 */
int do_hook VDEF((int idx, CONST char *fmt, CONST char *argfmt, ...))
{
#ifndef HAVE_STDARG
    int idx;
    char *fmt, *argfmt;
#endif
    va_list ap;
    int ran = 0;
    Aline *aline = NULL;
    Stringp buf, args;    /* do_hook is re-entrant; can't use static buffers */

#ifdef HAVE_STDARG
    va_start(ap, argfmt);
#else
    va_start(ap);
    idx = va_arg(ap, int);
    fmt = va_arg(ap, char *);
    argfmt = va_arg(ap, char *);
#endif
    if (hookflag || hilite || gag) {
        Stringninit(args, 96);
        vSprintf(args, 0, argfmt, ap);
    }
    va_end(ap);

    if (fmt) {
        Stringninit(buf, 96);
        if (*fmt == '!') {
            eprefix(buf);
            fmt++;
        }
#ifdef HAVE_STDARG
        va_start(ap, argfmt);
#else
        va_start(ap);
        idx = va_arg(ap, int);
        fmt = va_arg(ap, char *);
        argfmt = va_arg(ap, char *);
#endif
        vSprintf(buf, SP_APPEND, fmt, ap);
        va_end(ap);
        aline = new_alinen(buf->s, 0, buf->len);
    }

    if (hookflag || hilite || gag) {
        ran = find_and_run_matches(args->s, (1L<<idx), &aline, xworld(), TRUE);
        Stringfree(args);
    }

    if (fmt) {
        tfputa(aline, tferr);
        Stringfree(buf);
    }
    return ran;
}

/* Find and run one or more matches for a hook or trig.
 * <text> is text to be matched.  If <hook> is 0, this looks for a trigger;
 * if <hook> is nonzero, it is a hook bit flag.  If <alinep> is non-NULL,
 * attributes of matching macros will be applied to *<alinep>.
 */
int find_and_run_matches(text, hook, alinep, world, globalflag)
    CONST char *text;
    long hook;
    Aline **alinep;
    World *world;
    int globalflag;
{
    Macro *first = NULL;
    int num = 0;                            /* # of non-fall-thrus */
    int ran = 0;                            /* # of executed macros */
    int lowerlimit = -1;                    /* lowest priority that can match */
    ListEntry *node;
    Pattern *pattern;
    Macro *macro;

    /* Macros are sorted by decreasing priority, with fall-thrus first.
     * So, we search the list, running each matching fall-thru as we find it;
     * when we find a matching non-fall-thru, we collect any other non-fall-
     * thru matches of the same priority, and select one to run.
     */
    /* Note: kill_macro() does not remove macros from triglist or hooklist,
     * so this will work correctly when a macro kills itself, or inserts a
     * new macro just after itself in triglist/hooklist.
     */

    recur_count++;
    node = hook ? hooklist->head : triglist->head;
    for ( ; node && MAC(node)->pri >= lowerlimit; node = node->next) {
        macro = MAC(node);
        if (macro->flags & MACRO_DEAD) continue;
        if (!(
	    (macro->trig.str && (
		(borg && macro->body && (macro->prob > 0)) ||
		(hilite && ((macro->attr & F_HILITE) || macro->subexp)) ||
		(gag && (macro->attr & F_GAG)))) ||
	    (hook && (macro->hook & hook))))
	{
	    continue;
	}
        if (macro->world && macro->world != world) continue;
        if (!globalflag && !macro->world) continue;
        if (macro->wtype.str) {
            CONST char *type;
            if (!world) continue;
            type = world_type(world);
            if (!patmatch(&macro->wtype, type ? type : ""))
                continue;
        }
        if (*macro->expr) {
            struct Value *result;
            int expr_condition;
            result = expr_value_safe(macro->expr);
            expr_condition = valbool(result);
            freeval(result);
            if (!expr_condition) continue;
        }
        pattern = hook ? &macro->hargs : &macro->trig;
        if ((hook && !macro->hargs.str) || patmatch(pattern, text)) {
            if (macro->fallthru) {
                /* fall-thru matches can be run right away */
                ran += run_match(macro, text, hook, alinep?*alinep:NULL);
                if (alinep && !hook)
                    text = (*alinep)->str;    /* in case of /substitute */
            } else {
                /* collect list of non-fall-thru matches */
                lowerlimit = macro->pri;
                num++;
                macro->tnext = first;
                first = macro;
            }
        }
    }

    /* run exactly one of the non fall-thrus. */
    if (num > 0) {
        for (num = RRAND(0, num-1); num; num--)
            first = first->tnext;
        ran += run_match(first, text, hook, alinep ? *alinep : NULL);
    }

    recur_count--;
    return ran;
}


/* run a macro that has been selected by a trigger or hook */
static int run_match(macro, text, hook, aline)
    Macro *macro;         /* macro to run */
    CONST char *text;     /* argument text */
    long hook;            /* hook vector */
    Aline *aline;   /* aline to which attributes are applied */
{
    int ran = 0;
    struct Sock *callingsock = xsock;
    void *oldregscope;

    oldregscope = new_reg_scope(hook ? macro->hargs.re : macro->trig.re, text);

    /* Apply attributes (full and partial) to aline. */
    if (aline) {
        regexp *re = macro->trig.re;
        add_attr(aline->attrs, macro->attr);
        if (!hook && macro->trig.mflag == MATCH_REGEXP && aline->len && hilite
            && macro->subattr && re->startp[0] < re->endp[0])
        {
            int i;
            short n = macro->subexp;
            if (!aline->partials) {
                aline->partials = (short*)XMALLOC(sizeof(short)*aline->len);
                for (i = 0; i < aline->len; ++i)
                    aline->partials[i] = aline->attrs;
                aline->attrs &= ~F_HWRITE;
            }
            do {
                for (i = re->startp[n] - text; i < re->endp[n] - text; ++i)
                    add_attr(aline->partials[i], macro->subattr);
            } while (*re->endp[0] &&
                patmatch(&macro->trig, re->endp[0]));
            /* restore original startp/endp */
            patmatch(&macro->trig, text);
        }
    }

    /* Execute the macro. */
    if ((hook && hookflag) || (!hook && borg)) {
        if (macro->prob == 100 || RRAND(0, 99) < macro->prob) {
            if (macro->shots && !--macro->shots) kill_macro(macro);
            if (mecho > macro->invis) {
                char numbuf[16];
                if (!*macro->name) sprintf(numbuf, "#%d", macro->num);
                tfprintf(tferr, "%S%s%s: /%s", do_mprefix(),
                    hook ? hook_name(hook)->s : "",
                    hook ? " HOOK" : "TRIGGER",
                    *macro->name ? macro->name : numbuf);
            }
            if (*macro->body) {
                do_macro(macro, text);
                ran += !macro->quiet;
            }
        }
    }

    restore_reg_scope(oldregscope);

    /* Restore xsock, in case macro called fg_sock().  main_loop() will
     * set xsock=fsock, so any fg_sock() will effect xsock after the
     * find_and_run_matches() loop is complete.
     */
    xsock = callingsock;

    return ran;
}

#ifdef DMALLOC
void free_macros()
{
    while (maclist->head) nuke_macro((Macro *)maclist->head->datum);
    free_hash(macro_table);
}
#endif

