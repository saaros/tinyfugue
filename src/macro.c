/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: macro.c,v 35004.135 2003/11/07 01:59:47 hawkeye Exp $";


/**********************************************
 * Fugue macro package                        *
 *                                            *
 * Macros, hooks, triggers, hilites and gags  *
 * are all processed here.                    *
 **********************************************/

#include "config.h"
#include "port.h"
#include "tf.h"
#include "util.h"
#include "search.h"
#include "tfio.h"
#include "world.h"
#include "macro.h"
#include "keyboard.h"	/* bind_key()... */
#include "expand.h"
#include "socket.h"	/* xworld() */
#include "output.h"	/* get_keycode() */
#include "commands.h"
#include "command.h"
#include "parse.h"	/* valbool() for /def -E */

typedef struct {
    Pattern name, body, bind, keyname, expr;
} AuxPat;

int invis_flag = 0;

static Macro  *macro_spec(String *args, int offset, int *mflag, int allowshort);
static int     macro_match(Macro *spec, Macro *macro, AuxPat *aux);
static int     complete_macro(Macro *spec, int num, ListEntry *where);
static int     init_aux_patterns(Macro *spec, int mflag, AuxPat *aux);
static Macro  *match_exact(int ishook, const char *str, long flags);
static int     list_defs(TFILE *file, Macro *spec, int mflag);
static void    apply_attrs_of_match(Macro *macro, String *text, long hook,
                     String *line);
static int     run_match(Macro *macro, String *text, long hook);
static String *hook_name(long hook) PURE;
static String *attr2str(attr_t attrs) PURE;
static int     rpricmp(const Macro *m1, const Macro *m2);
static void    nuke_macro(Macro *macro);


#define HASH_SIZE 997	/* prime number */

#define MACRO_TEMP	001
#define MACRO_DEAD	002
#define MACRO_SHORT	004

#define INVALID_SUBEXP	-3

static List maclist[1];			/* list of all (live) macros */
static List triglist[1];		/* list of macros by trigger */
static List hooklist[1];		/* list of macros by hook */
static Macro *dead_macros;		/* head of list of dead macros */
static HashTable macro_table[1];	/* macros hashed by name */
static World NoWorld, AnyWorld;		/* explicit "no" and "any" */
static int mnum = 0;			/* macro ID number */

typedef enum {
    HT_TEXT = 0x00,	/* normal text in fg world */
    HT_ALERT = 0x01,	/* alert */
    HT_WORLD = 0x02,	/* text in xsock->world, plus alert if xsock != fsock */
    HT_XSOCK = 0x04	/* alert should be cleared if xsock is foregrounded */
} hooktype_t;

typedef struct hookrec {
    const char *name;
    hooktype_t hooktype;
} hookrec_t;

static const hookrec_t hook_table[] = {
#define gencode(id, type)  { #id, type }
#include "hooklist.h"
#undef gencode
};

#define NONNULL(str) ((str) ? (str) : "")

/* These macros allow easy sharing of trigger and hook code. */
#define MAC(Node)       ((Macro *)((Node)->datum))


void init_macros(void)
{
    init_hashtable(macro_table, HASH_SIZE, cstrstructcmp);
    init_list(maclist);
    init_list(triglist);
    init_list(hooklist);
}

/***************************************
 * Routines for parsing macro commands *
 ***************************************/

/* convert attr string to bitfields */
/* We don't write **argp, but we can't declare it const**. */
int parse_attrs(char **argp, attr_t *attrp)
{
    int color;
    char *end, *name;
    char buf[16];

    *attrp = 0;

    while (**argp) {
        ++*argp;
        switch((*argp)[-1]) {
        case ',':  /* skip */            break;
        case 'n':  *attrp |= F_NONE;      break;
        case 'x':  *attrp |= F_EXCLUSIVE; break;
        case 'G':  *attrp |= F_NOHISTORY; break;
        case 'g':  *attrp |= F_GAG;       break;
        case 'u':  *attrp |= F_UNDERLINE; break;
        case 'r':  *attrp |= F_REVERSE;   break;
        case 'f':  *attrp |= F_FLASH;     break;
        case 'd':  *attrp |= F_DIM;       break;
        case 'B':  *attrp |= F_BOLD;      break;
        case 'b':  *attrp |= F_BELL;      break;
        case 'h':  *attrp |= F_HILITE;    break;
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
            if ((color = enum2int(name, 0, enum_color, "color")) < 0)
                return 0;
            *attrp = adj_attr(*attrp, color2attr(color));
            break;
        default:
            eprintf("invalid display attribute '%c'", (*argp)[-1]);
            return 0;
        }
    }
    return 1;
}

/* Convert hook string to bit vector; return -1 on error. */
long parse_hook(char **argp)
{
    char *in, state;
    const hookrec_t *hookrec;
    long result = 0;

    if (!**argp) return ALL_HOOKS;
    for (state = '|'; state == '|'; *argp = in) {
        for (in = *argp; *in && !is_space(*in) && *in != '|'; ++in);
        state = *in;
        *in++ = '\0';
        if (strcmp(*argp, "*") == 0) result = ALL_HOOKS;
        if (strcmp(*argp, "0") == 0) result = 0;
        else {
            hookrec = binsearch((void*)*argp, (void *)hook_table,
		NUM_HOOKS, sizeof(hookrec_t), cstrstructcmp);
            if (!hookrec) {
                eprintf("invalid hook event \"%s\"", *argp);
                return -1;
            }
            result |= (1L << (hookrec - hook_table));
        }
    }
    if (!state) *argp = NULL;
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
static Macro *macro_spec(String *args, int offset, int *xmflag, int allowshort)
{
    Macro *spec;
    char opt, *ptr, *s, *name = NULL;
    int i, n, mflag = -1, error = 0;
    long num;
    attr_t attrs;

    if (!(spec = (Macro *)MALLOC(sizeof(struct Macro)))) {
        eprintf("macro_spec: not enough memory");
        return NULL;
    }
    spec->num = 0;
    spec->body = spec->expr = NULL;
    spec->prog = spec->exprprog = NULL;
    spec->name = spec->bind = spec->keyname = NULL;
    spec->numnode = spec->trignode = spec->hooknode = spec->hashnode = NULL;
    init_pattern_str(&spec->trig, NULL);
    init_pattern_str(&spec->hargs, NULL);
    init_pattern_str(&spec->wtype, NULL);
    spec->world = NULL;
    spec->pri = spec->prob = spec->shots = spec->fallthru = spec->quiet = -1;
    spec->hook = -1;
    spec->invis = 0;
    spec->attr = 0;
    spec->nsubattr = 0;
    spec->subattr = NULL;
    spec->flags = MACRO_TEMP;
    spec->builtin = NULL;
    spec->used[USED_NAME] = spec->used[USED_TRIG] =
	spec->used[USED_HOOK] = spec->used[USED_KEY] = 0;

    startopt(args, "sp#c#b:B:E:t:w:h:a:f:P:T:FiIn#1m:qu" + !allowshort);
    while (!error && (opt = nextopt(&ptr, &num, NULL, &offset))) {
        switch (opt) {
        case 's':
            spec->flags |= MACRO_SHORT;
            break;
        case 'm':
            if (!(error = ((i = enum2int(ptr, 0, enum_match, "-m")) < 0))) {
		if ((error = (mflag >= 0 && mflag != i)))
		    eprintf("-m option conflicts with earlier -m or -P");
		mflag = i;
	    }
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
            ptr = print_to_ascii(ptr)->data;
            spec->bind = STRDUP(ptr);
            break;
        case 'B':
            if (spec->keyname) FREE(spec->keyname);
            if (spec->bind) FREE(spec->bind);
            spec->keyname = STRDUP(ptr);
            break;
        case 'E':
            if (spec->expr) Stringfree(spec->expr);
            (spec->expr = Stringnew(ptr, -1, 0))->links++;
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
            error = !parse_attrs(&ptr, &attrs);
            spec->attr = adj_attr(spec->attr, attrs);
            break;
        case 'P':
            if ((error = (spec->nsubattr > 0))) {
                eprintf("-P can be given only once per macro.");
		break;
	    } else if ((error = (mflag >= 0 && mflag != MATCH_REGEXP))) {
		eprintf("\"-P\" requires \"-mregexp -t<pattern>\"");
		break;
	    }
	    for (n = 0, s = ptr; *s; s++) {
		if (*s == ';') continue; /* don't count empties */
		n++;
		s = strchr(s, ';');
		if (!s) break;
	    }
	    spec->subattr = MALLOC(n * sizeof(subattr_t));
	    if (!spec->subattr) {
		eprintf("-P: out of memory");
		error++;
		break;
	    }
	    spec->nsubattr = n;

	    for (i = 0; !error && i < n; i++, ptr = s+1) {
		attr_t attr;
		if ((s = strchr(ptr, ';')))
		    *s = '\0';
		if (s == ptr) { /* skip empty */
		    i--;
		    continue;
		}
		if (*ptr == 'L') {
		    spec->subattr[i].subexp = -1;
		    ptr++;
		} else if (*ptr == 'R') {
		    spec->subattr[i].subexp = -2;
		    ptr++;
		} else {
		    spec->subattr[i].subexp = strtoint(ptr, &ptr);
		    if ((error = (spec->subattr[i].subexp < 0))) {
			eprintf("-P: number must be non-negative");
			break;
		    }
		}
		error = !parse_attrs(&ptr, &attr);
		spec->subattr[i].attr = attr;
	    }
	    mflag = MATCH_REGEXP;
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
        case 'u':
            spec->used[0] = 1;
            break;
        default:
            error = TRUE;
        }
    }

    if (error) {
        nuke_macro(spec);
        return NULL;
    }
    if (mflag < 0)
	mflag = matching;
    if (xmflag) *xmflag = mflag;
    init_pattern_mflag(&spec->trig, mflag);
    init_pattern_mflag(&spec->hargs, mflag);
    init_pattern_mflag(&spec->wtype, mflag);

    ptr = args->data + offset;
    if (!*ptr) return spec;
    name = ptr;

    if ((ptr = strchr(ptr, '='))) {
        *ptr++ = '\0';
        ptr = stripstr(ptr);
        (spec->body = Stringodup(args, ptr - args->data))->links++;
    }
    name = stripstr(name);
    spec->name = *name ? STRDUP(name) : NULL;

    return spec;
}


/* init_aux_patterns
 * Macro_match() needs to compare some string fields that aren't normally
 * patterns.  This function initializes patterns for those fields.
 */
static int init_aux_patterns(Macro *spec, int mflag, AuxPat *aux)
{
    init_pattern_str(&aux->name, spec->name);
    init_pattern_str(&aux->body, spec->body ? spec->body->data : NULL);
    init_pattern_str(&aux->expr, spec->expr ? spec->expr->data : NULL);
    init_pattern_str(&aux->bind, spec->bind);
    init_pattern_str(&aux->keyname, spec->keyname);
    if (mflag < 0) mflag = matching;

    return init_pattern_mflag(&aux->name, mflag) &&
	init_pattern_mflag(&aux->body, mflag) &&
	init_pattern_mflag(&aux->expr, mflag) &&
	init_pattern_mflag(&aux->bind, mflag) &&
	init_pattern_mflag(&aux->keyname, mflag);
}

static void free_aux_patterns(AuxPat *aux)
{
    free_pattern(&aux->name);
    free_pattern(&aux->body);
    free_pattern(&aux->bind);
    free_pattern(&aux->keyname);
    free_pattern(&aux->expr);
}

/* macro_match
 * Compares spec to macro.  aux contains patterns for string fields that
 * aren't normally patterns.  Returns 0 for match, 1 for nonmatch.
 */
static int macro_match(Macro *spec, Macro *macro, AuxPat *aux)
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
    if (spec->subattr) {
	int i, j;
	for (i = 0; i < spec->nsubattr; i++) {
	    for (j = 0; ; j++)
	    {
		if (j > macro->nsubattr) return 1;
		if (macro->subattr[j].subexp == spec->subattr[i].subexp) break;
		if (spec->subattr[i].attr == F_NONE) {
		    if (macro->subattr[j].attr)
			return 1;
		} else {
		    if ((spec->subattr[i].attr & macro->subattr[j].attr) == 0)
			return 1;
		}
	    }
	}
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
            if (!patmatch(&aux->keyname, NULL, macro->keyname)) return 1;
        }
    }

    if (spec->bind) {
        if (!*spec->bind) {
            if (!*macro->bind) return 1;
        } else {
            if (!patmatch(&aux->bind, NULL, macro->bind)) return 1;
        }
    }

    if (spec->expr) {
        if (!spec->expr->len) {
            if (!macro->expr) return 1;
        } else {
            if (!macro->expr) return 1;
	    if (!patmatch(&aux->expr, NULL, macro->expr->data)) return 1;
        }
    }

    if (!spec->hook && macro->hook > 0) return 1;
    if (spec->hook > 0) {
        if ((spec->hook & macro->hook) == 0) return 1;
        if (spec->hargs.str && *spec->hargs.str) {
            if (!patmatch(&spec->hargs, NULL, NONNULL(macro->hargs.str)))
                return 1;
        }
    }
    if (spec->trig.str) {
        if (!*spec->trig.str) {
            if (!macro->trig.str) return 1;
        } else {
            if (!patmatch(&spec->trig, NULL, NONNULL(macro->trig.str)))
                return 1;
        }
    }
    if (spec->wtype.str) {
        if (!*spec->wtype.str) {
            if (!macro->wtype.str) return 1;
        } else {
            if (!patmatch(&spec->wtype, NULL, NONNULL(macro->wtype.str)))
                return 1;
        }
    }
    if (spec->num && macro->num != spec->num)
        return 1;
    if (spec->name && !patmatch(&aux->name, NULL, macro->name))
        return 1;
    if (spec->body && !patmatch(&aux->body, macro->body, NULL))
        return 1;
    return 0;
}

/* find Macro by name */
Macro *find_macro(const char *name)
{
    if (!*name) return NULL;
    if (*name == '#') return find_num_macro(atoi(name + 1));
    return (Macro *)hash_find(name, macro_table);
}

/* find single exact match */
static Macro *match_exact(int ishook, const char *str, long flags)
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
Macro *new_macro(const char *trig, const char *bind, long hook,
    const char *hargs, const char *body, int pri, int prob, attr_t attr,
    int invis, int mflag)
{
    Macro *new;
    int error = 0;

    if (!(new = (Macro *) MALLOC(sizeof(struct Macro)))) {
        eprintf("new_macro: not enough memory");
        return NULL;
    }
    new->numnode = new->trignode = new->hooknode = new->hashnode = NULL;
    new->prog = new->exprprog = NULL;
    new->name = STRDUP("");
    (new->body = Stringnew(body, -1, 0))->links++;
    new->expr = NULL;
    new->bind = STRDUP(bind);
    new->keyname = STRDUP("");
    new->hook = hook;
    error += !init_pattern(&new->trig, trig, mflag);
    error += !init_pattern(&new->hargs, hargs, mflag);
    init_pattern_str(&new->wtype, NULL);
    new->world = NULL;
    new->pri = pri;
    new->prob = prob;
    new->attr = attr;
    new->nsubattr = 0;
    new->subattr = NULL;
    new->shots = 0;
    new->invis = invis;
    new->fallthru = FALSE;
    new->quiet = FALSE;
    new->flags = MACRO_TEMP;
    new->builtin = NULL;
    new->used[USED_NAME] = new->used[USED_TRIG] =
	new->used[USED_HOOK] = new->used[USED_KEY] = 0;

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
static int install_macro(Macro *macro)
{
    if (*macro->bind && !bind_key(macro)) {
	unlist(macro->numnode, maclist);
	nuke_macro(macro);
	return 0;
    }
    if (*macro->name) {
        macro->hashnode = hash_insert((void *)macro, macro_table);
	if (macro->builtin) { /* macro->builtin was set in complete_macro() */
	    macro->builtin->macro = macro;
	}
    }
    if (macro->trig.str) {
        macro->trignode = sinsert((void *)macro,
	    macro->world ? macro->world->triglist : triglist, (Cmp *)rpricmp);
    }
    if (macro->hook) {
        macro->hooknode = sinsert((void *)macro,
	    macro->world ? macro->world->hooklist : hooklist, (Cmp *)rpricmp);
    }
    macro->flags &= ~MACRO_TEMP;
    if (!*macro->name && (macro->trig.str || macro->hook) && macro->shots == 0 && pedantic) {
        eprintf("warning: new macro (#%d) does not have a name.", macro->num);
    }
    return macro->num;
}

/* add_macro
 * Install a permanent Macro in appropriate structures.
 * Only the keybinding is checked for conflicts; everything else is assumed
 * assumed to be error- and conflict-free.  If the bind_key fails, the
 * macro will be nuked.
 */
static int add_numbered_macro(Macro *macro, int num, ListEntry *where)
{
    if (!macro) return 0;
    macro->num = num ? num : ++mnum;
    macro->numnode = inlist((void *)macro, maclist, where);
    return install_macro(macro);
}

int add_macro(Macro *macro)
{
    return add_numbered_macro(macro, 0, NULL);
}

/* rebind_key_macros
 * Unbinds macros with keynames, and attempts to rebind them.
 */
void rebind_key_macros(void)
{
    Macro *p;
    ListEntry *node;
    const char *code;

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
static int rpricmp(const Macro *m1, const Macro *m2)
{
    if (m2->pri != m1->pri) return m2->pri - m1->pri;
    else return m2->fallthru - m1->fallthru;
}

struct Value *handle_def_command(String *args, int offset)
{
    Macro *spec;

    if (!(args->len - offset) || !(spec = macro_spec(args, offset, NULL, FALSE)))
        return shareval(val_zero);
    return newint(complete_macro(spec, 0, NULL));
}

/* Fill in "don't care" fields with default values, and add_numbered_macro().
 * If error checking fails, spec will be nuked.
 */
static int complete_macro(Macro *spec, int num, ListEntry *where)
{
    Macro *macro = NULL;
    int i;

    if (spec->name && *spec->name) {
        if (strchr("#@!/", *spec->name) || strchr(spec->name, ' ')) {
            eprintf("illegal macro name \"%s\".", spec->name);
            nuke_macro(spec);
            return 0;
        }
        if (keyword(spec->name) ||
	    ((spec->builtin = find_builtin_cmd(spec->name)) &&
	    (spec->builtin->reserved)))
	{
            eprintf("\"%s\" is a reserved word.", spec->name);
            nuke_macro(spec);
            return 0;
        }

        if (spec->builtin) {
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
    for (i = 0; i < spec->nsubattr; i++)
	spec->subattr[i].attr &= ~F_NONE;
    if (!spec->name) spec->name = STRNDUP("", 0);
    if (!spec->body) (spec->body = blankline)->links++;
    /*if (!spec->expr) (spec->expr = blankline)->links++;*/

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

    if (spec->nsubattr > 0 && spec->trig.mflag != MATCH_REGEXP) {
        eprintf("\"-P\" requires \"-mregexp -t<pattern>\"");
        nuke_macro(spec);
        return 0;
    }

    if (*spec->name && (macro = find_macro(spec->name)) && !redef) {
        eprintf("macro %s already exists", macro->name);
        nuke_macro(spec);
        return 0;
    }
    if (!add_numbered_macro(spec, num, where)) return 0;
    if (macro) {
        do_hook(H_REDEF, "!Redefined %s %s", "%s %s", "macro", macro->name);
        kill_macro(macro);
    }
    return spec->num;
}

/* define a new Macro with hook */
int add_hook(char *args, const char *body)
{
    long hook;

    if ((hook = parse_hook(&args)) < 0) return 0;
    if (args && !*args) args = NULL;
    return add_macro(new_macro(NULL, "", hook, args, body, 0, 100, 0, 0,
        matching));
}

/* /edit: Edit an existing macro.
 * Actually editing the macro in place is quite hairy, so instead we
 * remove the old one, create a replacement and add it.  If the replacement
 * fails, we re-add the original.  Either way, the number and position in
 * maclist are unchanged.
 */
struct Value *handle_edit_command(String *args, int offset)
{
    Macro *spec, *macro = NULL;
    int error = 0;
    int num;
    ListEntry *where;

    if (!(args->len - offset) || !(spec = macro_spec(args, offset, NULL, FALSE))) {
        return shareval(val_zero);
    } else if (!spec->name) {
        eprintf("You must specify a macro.");
    } else if (spec->name[0] == '$') {
        macro = match_exact(FALSE, spec->name + 1, F_ATTR);
    } else if (!(macro = find_macro(spec->name))) {
        eprintf("macro %s does not exist", spec->name);
    }

    if (!macro) {
        nuke_macro(spec);
        return shareval(val_zero);
    }

    num = macro->num;
    where = macro->numnode->prev;
    kill_macro(macro);

    FREE(spec->name);
    spec->name = STRDUP(macro->name);

    if (!spec->body && macro->body) (spec->body = macro->body)->links++;
    if (!spec->expr && macro->expr) (spec->expr = macro->expr)->links++;
    if (!spec->bind && macro->bind) spec->bind = STRDUP(macro->bind);
    if (!spec->keyname && macro->keyname) spec->keyname =STRDUP(macro->keyname);
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
    if (spec->nsubattr == 0 && macro->nsubattr > 0) {
	spec->nsubattr = macro->nsubattr;
	spec->subattr = MALLOC(spec->nsubattr * sizeof(subattr_t));
	memcpy(spec->subattr, macro->subattr,
	    spec->nsubattr * sizeof(subattr_t));
    }

    spec->used[USED_NAME] = macro->used[USED_NAME];
    spec->used[USED_TRIG] = macro->used[USED_TRIG];
    spec->used[USED_HOOK] = macro->used[USED_HOOK];
    spec->used[USED_KEY] = macro->used[USED_KEY];

    if (!error) {
        complete_macro(spec, num, where);
        return newint(spec->num);
    }

    /* Edit failed.  Resurrect original macro. */
    macro = dead_macros;
    macro->flags &= ~MACRO_DEAD;
    dead_macros = macro->tnext;
    add_numbered_macro(macro, num, where);
    return shareval(val_zero);
}


/********************************
 * Routines for removing macros *
 ********************************/

void kill_macro(Macro *macro)
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

void nuke_dead_macros(void)
{
    Macro *macro;

    while ((macro = dead_macros)) {
        dead_macros = dead_macros->tnext;
        nuke_macro(macro);
    }
}

/* free macro structure */
static void nuke_macro(Macro *m)
{
    if (!(m->flags & MACRO_DEAD) && !(m->flags & MACRO_TEMP)) {
        kill_macro(m);
    }
    if (m->trignode)
	unlist(m->trignode, m->world ? m->world->triglist : triglist);
    if (m->hooknode)
	unlist(m->hooknode, m->world ? m->world->hooklist : hooklist);

    if (m->body) Stringfree(m->body);
    if (m->expr) Stringfree(m->expr);
    if (m->bind) FREE(m->bind);
    if (m->keyname) FREE(m->keyname);
    if (m->subattr) FREE(m->subattr);
    if (m->prog) prog_free(m->prog);
    if (m->exprprog) prog_free(m->exprprog);
    if (m->builtin && m->builtin->macro == m)
	m->builtin->macro = NULL;
    free_pattern(&m->trig);
    free_pattern(&m->hargs);
    free_pattern(&m->wtype);
    if (m->name) FREE(m->name);
    FREE(m);
}

/* delete a macro */
int remove_macro(char *str, long flags, int byhook)
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

/* delete specified macros */
struct Value *handle_purge_command(String *args, int offset)
{
    Macro *spec;
    ListEntry *node, *next;
    int result = 0;
    int mflag;
    AuxPat aux;

    if (!(spec = macro_spec(args, offset, &mflag, FALSE)))
        return shareval(val_zero);
    if (spec->name && *spec->name == '#') {
        spec->num = atoi(spec->name + 1);
        FREE(spec->name);
        spec->name = NULL;
    }
    if (!(init_aux_patterns(spec, mflag, &aux)))
	goto error;
    for (node = maclist->head; node; node = next) {
	next = node->next;
	if (macro_match(spec, MAC(node), &aux) == 0) {
	    kill_macro(MAC(node));
	    result++;
	}
    }
    /* regrelease(); */
error:
    free_aux_patterns(&aux);
    nuke_macro(spec);
    return newint(result);
}

/* delete macro by number */
struct Value *handle_undefn_command(String *args, int offset)
{
    int num, result = 0;
    Macro *macro;
    char *ptr = args->data + offset;

    while (*ptr) {
        if ((num = numarg(&ptr)) >= 0 && (macro = find_num_macro(num))) {
            kill_macro(macro);
            result++;
        }
    }
    return newint(result);
}

Macro *find_num_macro(int num)
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

void remove_world_macros(World *w)
{
    ListEntry *node, *next;

    for (node = w->triglist->head; node; node = next) {
        next = node->next;
        kill_macro(MAC(node));
    }
    for (node = w->hooklist->head; node; node = next) {
        next = node->next;
        kill_macro(MAC(node));
    }
}


/**************************
 * Routine to list macros *
 **************************/

/* convert hook bitfield to string */
static String *hook_name(long hook)
{
    int n;
    STATIC_BUFFER(buf);

    Stringtrunc(buf, 0);
    for (n = 0; n < (int)NUM_HOOKS; n++) {
                 /* ^^^^^ Some brain dead compilers need that cast */
        if (!((1L << n) & hook)) continue;
        if (buf->len) Stringadd(buf, '|');
        Stringcat(buf, hook_table[n].name);
    }
    return buf;
}

static String *attr2str(attr_t attrs)
{
    STATIC_BUFFER(buffer);

    Stringtrunc(buffer, 0);
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
        Sprintf(buffer, SP_APPEND, "C%S", &enum_color[attr2fgcolor(attrs)]);
    if (attrs & F_BGCOLOR) {
        if (attrs & F_FGCOLOR) Stringadd(buffer, ',');
        Sprintf(buffer, SP_APPEND, "C%S", &enum_color[attr2bgcolor(attrs)]);
    }
    return buffer;
}

static void print_def(TFILE *file, String *buffer, Macro *p)
{
    int mflag = -1;

    if (!buffer)
	buffer = Stringnew(NULL, 0, 0);
    buffer->links++;

    if (!file) Sprintf(buffer, 0, "%% %d: /def ", p->num);
    else Stringcpy(buffer, "/def ");
    if (p->invis) Stringcat(buffer, "-i ");
    if (p->trig.str || p->hook)
	Sprintf(buffer, SP_APPEND, "-%sp%d ",
	    p->fallthru ? "F" : "", p->pri);
    if (p->prob != 100)
	Sprintf(buffer, SP_APPEND, "-c%d ", p->prob);
    if (p->attr) {
	Sprintf(buffer, SP_APPEND, "-a%S ", attr2str(p->attr));
    }
    if (p->nsubattr > 0) {
	int i;
	mflag = MATCH_REGEXP;
	Stringcat(buffer, "-P");
	for (i = 0; i < p->nsubattr; i++) {
	    if (i > 0) Stringadd(buffer, ';');
	    if (p->subattr[i].subexp == -1)
		Stringadd(buffer, 'L');
	    else if (p->subattr[i].subexp == -2)
		Stringadd(buffer, 'R');
	    else
		Sprintf(buffer, SP_APPEND, "%d", (int)p->subattr[i].subexp);
	    SStringcat(buffer, attr2str(p->subattr[i].attr));
	}
	Stringadd(buffer, ' ');
    }
    if (p->shots)
	Sprintf(buffer, SP_APPEND, "-n%d ", p->shots);
    if (p->world)
	Sprintf(buffer, SP_APPEND, "-w'%q' ", '\'', p->world->name);
    if (p->wtype.str) {
	if (p->wtype.mflag != mflag)
	    Sprintf(buffer, SP_APPEND, "-m%S ", &enum_match[p->wtype.mflag]);
	mflag = p->wtype.mflag;
	Sprintf(buffer, SP_APPEND, "-T'%q' ", '\'', p->wtype.str);
    }

    if (p->expr) 
	Sprintf(buffer, SP_APPEND, "-E'%q' ", '\'', p->expr->data);

    if (p->trig.str) {
	if (p->trig.mflag != mflag)
	    Sprintf(buffer, SP_APPEND, "-m%S ", &enum_match[p->trig.mflag]);
	mflag = p->trig.mflag;
	Sprintf(buffer, SP_APPEND, "-t'%q' ", '\'', p->trig.str);
    }
    if (p->hook) {
	if (p->hargs.mflag != mflag)
	    Sprintf(buffer, SP_APPEND, "-m%S ",
		&enum_match[p->hargs.mflag]);
	mflag = p->hargs.mflag;
	if (p->hargs.str && *p->hargs.str)
	    Sprintf(buffer, SP_APPEND, "-h'%S %q' ",
		hook_name(p->hook), '\'', p->hargs.str);
	else
	    Sprintf(buffer, SP_APPEND, "-h%S ", hook_name(p->hook));
    }

    if (*p->keyname) 
	Sprintf(buffer, SP_APPEND, "-B'%s' ", p->keyname);
    else if (*p->bind) 
	Sprintf(buffer, SP_APPEND, "-b'%q' ", '\'', ascii_to_print(p->bind)->data);

    if (p->quiet) Stringcat(buffer, "-q ");
    if (*p->name == '-') Stringcat(buffer, "- ");
    if (*p->name) Sprintf(buffer, SP_APPEND, "%s ", p->name);
    if (p->body->len) Sprintf(buffer, SP_APPEND, "= %S", p->body);

    tfputline(buffer, file ? file : tfout);
    Stringfree(buffer);
}

/* list all specified macros */
static int list_defs(TFILE *file, Macro *spec, int mflag)
{
    Macro *p;
    ListEntry *node;
    AuxPat aux;
    String *buffer = NULL;
    int result = 0;

    if (!(init_aux_patterns(spec, mflag, &aux))) goto error;
    if (spec->name && *spec->name == '#') {
        spec->num = atoi(spec->name + 1);
        FREE(spec->name);
        spec->name = NULL;
    }

    /* maclist is in reverse numeric order, so we start from tail */
    for (node = maclist->tail; node; node = node->prev) {
        p = MAC(node);
        if (macro_match(spec, p, &aux) != 0) continue;
        result = p->num;

        if (!buffer)
            (buffer = Stringnew(NULL, 0, 0))->links++;

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
                    Sprintf(buffer, SP_APPEND, "(%S) ",
                        &enum_color[attr2fgcolor(p->attr)]);
                if (p->attr & F_BGCOLOR)
                    Sprintf(buffer, SP_APPEND, "(%S) ",
                        &enum_color[attr2bgcolor(p->attr)]);
            } else if (p->nsubattr > 0) {
                Stringcat(buffer, "(partial) ");
            } else if (p->trig.str) {
		Stringcat(buffer, "(trig");
		if (spec->used[0])
		    Sprintf(buffer, SP_APPEND, " %d", p->used[USED_TRIG]);
		Stringcat(buffer, ") ");
            }
            if (p->trig.str)
                Sprintf(buffer, SP_APPEND, "'%q' ", '\'', p->trig.str);
            if (*p->keyname) {
		Stringcat(buffer, "(key");
		if (spec->used[0])
		    Sprintf(buffer, SP_APPEND, " %d", p->used[USED_KEY]);
                Sprintf(buffer, SP_APPEND, ") '%s' ", p->keyname);
            } else if (*p->bind) {
		Stringcat(buffer, "(bind");
		if (spec->used[0])
		    Sprintf(buffer, SP_APPEND, " %d", p->used[USED_KEY]);
                Sprintf(buffer, SP_APPEND, ") '%q' ", '\'',
                    ascii_to_print(p->bind)->data);
            }
            if (p->hook) {
		Stringcat(buffer, "(hook");
		if (spec->used[0])
		    Sprintf(buffer, SP_APPEND, " %d", p->used[USED_HOOK]);
                Sprintf(buffer, SP_APPEND, ") %S ", hook_name(p->hook));
	    }
            if (*p->name) {
		Sprintf(buffer, SP_APPEND, "%s ", p->name);
		if (spec->used[0])
		    Sprintf(buffer, SP_APPEND, "(%d) ", p->used[USED_NAME]);
	    }
	    tfputline(buffer, file ? file : tfout);

        } else {
	    print_def(file, buffer, p);
        }

        /* If something is sharing buffer, we can't reuse it in next loop. */
        if (buffer->links > 1) {
            Stringfree(buffer);
            buffer = NULL;
        }
    }
    /* regrelease(); */
error:
    free_aux_patterns(&aux);
    if (buffer) {
        Stringfree(buffer);
        buffer = NULL;
    }
    return result;
}

/* write specified macros to file */
int save_macros(String *args, int offset)
{
    Macro *spec;
    TFILE *file = NULL;
    int result = 1;
    const char *name, *mode = "w";
    char opt, *next;
    int mflag;

    startopt(args, "a");
    while ((opt = nextopt(NULL, NULL, NULL, &offset))) {
        if (opt != 'a') return 0;
        mode = "a";
    }

    next = args->data + offset;
    name = stringarg(&next, NULL);
    offset = next - args->data;
    if (!(spec = macro_spec(args, offset, &mflag, FALSE))) result = 0;
    if (result && !(file = tfopen(expand_filename(name), mode))) {
        operror(name);
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

/* list macros on screen */
struct Value *handle_list_command(String *args, int offset)
{
    Macro *spec;
    int result = 1;
    int mflag;

    if (!(spec = macro_spec(args, offset, &mflag, TRUE))) result = 0;
    if (result) result = list_defs(NULL, spec, mflag);
    if (spec) nuke_macro(spec);
    return newint(result);
}


/**************************
 * Routines to use macros *
 **************************/

/* Do a macro! */
int do_macro(Macro *macro, String *args, int offset, int used_type,
    String *kbnumlocal)
{
    int result, old_invis_flag, oldblock;
    const char *command;
    char numbuf[16];

    if (*macro->name) {
        command = macro->name;
    } else {
        sprintf(numbuf, "#%d", macro->num);
        command = numbuf;
    }
    if (used_type >= 0) {
	macro->used[used_type]++;
    }
    old_invis_flag = invis_flag;
    invis_flag = macro->invis;
    oldblock = block; /* XXX ? */
    block = 0; /* XXX ? */
    if (!macro->prog)
	macro->prog = compile_tf(macro->body, 0, SUB_MACRO, 0,
	    !macro->shots ? 2 : macro->shots > 10 ? 1 : 0);
    if (!macro->prog)
	result = 0;
    else
	result = prog_run(macro->prog, args, offset, command, kbnumlocal);
    invis_flag = old_invis_flag;
    block = oldblock; /* XXX ? */
    return result;
}

/* get body of macro */
const char *macro_body(const char *name)
{
    Macro *m;
    const char *body;

    if (strncmp("world_", name, 6) == 0 && (body = world_info(NULL, name + 6)))
        return body;
    if (!(m = find_macro(name))) return NULL;
    return m->body->data;
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
int do_hook(int idx, const char *fmt, const char *argfmt, ...)
{
    va_list ap;
    int ran = 0;
    String *line = NULL;
    String *args = NULL;
    /* do_hook is re-entrant, so we can't use static buffer.  macro regexps
     * may save a pointer to args, so we can't even use an auto buffer. */

    va_start(ap, argfmt);
    if (hookflag || hilite || gag) {
        (args = Stringnew(NULL, 96, 0))->links++;
        vSprintf(args, 0, argfmt, ap);
    }
    va_end(ap);

    if (fmt) {
        (line = Stringnew(NULL, 96, 0))->links++;
        if (*fmt == '!') {
            eprefix(line);
            fmt++;
        }
        va_start(ap, argfmt);
        vSprintf(line, SP_APPEND, fmt, ap);
        va_end(ap);
    }

    if (hookflag || hilite || gag) {
        ran = find_and_run_matches(args, (1L<<idx), &line, xworld(), TRUE,
	    0, hook_table[idx].hooktype);
        Stringfree(args);
    }

    if (line) {
        Stringfree(line);
    }
    return ran;
}

/* Find and run one or more matches for a hook or trig.
 * text is text to be matched; if NULL, *linep is used.
 * If %Pn subs are to be allowed, text should be NULL.
 * If <hook> is 0, this looks for a trigger;
 * if <hook> is nonzero, it is a hook bit flag.  If <linep> is non-NULL,
 * attributes of matching macros will be applied to *<linep>.
 */
int find_and_run_matches(String *text, long hook, String **linep, World *world,
    int globalflag, int exec_list_long, int hooktype)
{
    Queue runq[1];			    /* queue of macros to run */
    Macro *nonfallthru = NULL;		    /* list of non fall-thrus */
    int num = 0;                            /* # of non-fall-thrus */
    int ran = 0;                            /* # of executed macros */
    int lowerlimit = -1;                    /* lowest priority that can match */
    int header = 0;			    /* which headers have we printed? */
    ListEntry *gnode, *wnode, **nodep;
    Pattern *pattern;
    Macro *macro;
    const char *worldtype = NULL;

    /* Macros are sorted by decreasing priority, with fall-thrus first.  So,
     * we search the global and world lists in parallel.  For each matching
     * fall-thru, we apply its attributes; if it's a hook, we add it to a
     * queue, if it's a trigger, we execute it immediately.  When we find a
     * matching non-fall-thru, we collect a list of other non-fall-thru
     * matches of the same priority and select one, and apply its attributes;
     * again, if it's a hook, we add it to queue, if it's a trigger, we
     * execute.
     * Then, we print the line.
     * Then, if macros were queued (because this is a hook), we run all the
     * queued macros.
     * The point of the queue is so the line can be printed before any output
     * generated by the macros.  We would like to do this for triggers as well
     * as hooks, but then /substitute wouldn't work.
     */
    /* Note: kill_macro() does not remove macros from any lists, so this will
     * work correctly when a macro kills itself, or inserts a new macro just
     * after itself in a list.
     */

    if (world)
	worldtype = world_type(world);
    if (!worldtype)
	worldtype = "";
    if (!text)
        text = *linep;
    text->links++; /* in case substitute() frees text */
    recur_count++;
    if (hook) {
	gnode = hooklist->head;
	wnode = world ? world->hooklist->head : NULL;
    } else {
	gnode = triglist->head;
	wnode = world ? world->triglist->head : NULL;
    }

    if (exec_list_long == 0) {
	init_queue(runq);
    }

    while (gnode || wnode) {
	nodep = (!gnode) ? &wnode : (!wnode) ? &gnode :
	    (rpricmp(MAC(wnode), MAC(gnode)) > 0) ? &gnode : &wnode;
	macro = MAC(*nodep);
	*nodep = (*nodep)->next;

	if (macro->pri < lowerlimit && exec_list_long == 0)
	    break;
        if (macro->flags & MACRO_DEAD) continue;
        if (!(
	    (!hook && (
		(borg && macro->body && (macro->prob > 0)) ||
		(hilite && ((macro->attr & F_HILITE) || macro->nsubattr)) ||
		(gag && (macro->attr & F_GAG)))) ||
	    (hook && (macro->hook & hook))))
	{
	    continue;
	}
        /*if (macro->world && macro->world != world) continue;*/
        if (!globalflag && !macro->world) continue;
        if (macro->wtype.str) {
            if (!world) continue;
            if (!patmatch(&macro->wtype, NULL, worldtype))
                continue;
        }
        if (macro->expr) {
            struct Value *result = NULL;
            int expr_condition;
	    Program *prog;
	    if (macro->exprprog) {
		prog = macro->exprprog;
	    } else {
		prog = macro->exprprog = compile_tf(macro->expr, 0, -1, 1, 2);
	    }
	    if (prog)
		result = expr_value_safe(prog);
            expr_condition = valbool(result);
            freeval(result);
            if (!expr_condition) continue;
        }
        pattern = hook ? &macro->hargs : &macro->trig;
        if ((hook && !macro->hargs.str) || patmatch(pattern, text, NULL)) {
	    if (exec_list_long == 0) {
		if (macro->fallthru) {
		    if (linep && *linep)
			apply_attrs_of_match(macro, text, hook, *linep);
		    if (hook) {
			enqueue(runq, macro);
		    } else {
			ran += run_match(macro, text, hook);
			if (linep && !hook) {
			    /* in case of /substitute */ /* XXX */
			    Stringfree(text);
			    text = *linep;
			    text->links++;
			}
		    }
		} else {
		    /* collect list of non-fall-thru matches */
		    lowerlimit = macro->pri;
		    num++;
		    macro->tnext = nonfallthru;
		    nonfallthru = macro;
		}
	    } else {
		ran += (lowerlimit < 0);
		if (header < 3 && macro->pri < lowerlimit) {
		    oputs("% The following matching macros would not be "
			"applied:");
		    header = 3;
		}
		if (header < 2 && !macro->fallthru) {
		    oprintf("%% One of the following macros would %sbe "
			"applied:", ran > 1 ? "also " : "");
		    lowerlimit = macro->pri;
		    header = 2;
		}
		if (header < 1) {
		    oputs("% All of the following macros would be applied:");
		    header = 1;
		}
		if (exec_list_long > 1)
		    print_def(NULL, NULL, macro);
		else if (macro->name)
		    oprintf("%% %c %10d %s", macro->fallthru ? 'F' : ' ',
			macro->pri, macro->name);
		else
		    oprintf("%% %c %10d #%d", macro->fallthru ? 'F' :' ',
			macro->pri, macro->num);
	    }
        }
    }

    if (exec_list_long == 0) {
	/* select exactly one of the non fall-thrus. */
	if (num > 0) {
	    for (macro = nonfallthru, num = RRAND(0, num-1); num; num--)
		macro = macro->tnext;
	    if (linep && *linep)
		apply_attrs_of_match(macro, text, hook, *linep);
	    if (hook) {
		enqueue(runq, macro);
	    } else {
		ran += run_match(macro, text, hook);
	    }
	}

	/* print the line! */
	if (hook && linep && *linep) {
	    if (hooktype & HT_ALERT) {
		alert(*linep);
	    } else if (hooktype & HT_WORLD) {
		/* Note: world_output() must come before alert(), otherwise
		 * world_output() could cause an activity hook that would
		 * clobber the alert */
		if (xworld())
		    world_output(xworld(), *linep);
		if (!xsock_is_fg()) {
		    alert(*linep);
		}
	    } else {
		tfputline(*linep, tferr);
	    }
	    if (hooktype & HT_XSOCK) {
		xsock_alert_id(); /* alert should clear when xsock is fg'd */
	    }
	}

	/* run all of the queued macros */
	while ((macro = (Macro*)dequeue(runq))) {
	    ran += run_match(macro, text, hook);
	}
    } else {
	oprintf("%% %s would have %s %d macro%s.",
	    hook ? "Event" : "Text", hook ? "hooked" : "triggered",
	    ran, (ran != 1) ? "s" : "");
    }

    recur_count--;
    Stringfree(text);
    return ran;
}


/* apply attributes of a macro that has been selected by a trigger or hook */
static void apply_attrs_of_match(
    Macro *macro,         /* macro to apply */
    String *text,         /* argument text that matched trigger/hook */
    long hook,            /* hook vector */
    String *line)         /* line to which attributes are applied */
{
    RegInfo *old, *ri;

    if (text)
        old = new_reg_scope(hook ? macro->hargs.ri : macro->trig.ri, text);

    /* Apply attributes (full and partial) to line. */
    ri = macro->trig.ri;
    line->attrs = adj_attr(line->attrs, macro->attr);
    if (!hook && macro->trig.mflag == MATCH_REGEXP && line->len && hilite
	&& macro->nsubattr && ri->ovector[0] < ri->ovector[1])
    {
	int i, x, offset = 0;
	short start, end;
	int *saved_ovector = NULL;
	check_charattrs(line, line->len, 0, __FILE__, __LINE__);
	do {
	    for (x = 0; x < macro->nsubattr; x++) {
		if (macro->subattr[x].subexp == -1) {
		    start = 0;
		    end = ri->ovector[0];
		} else if (macro->subattr[x].subexp == -2) {
		    start = ri->ovector[1];
		    end = line->len;
		} else {
		    start = ri->ovector[macro->subattr[x].subexp * 2];
		    end = ri->ovector[macro->subattr[x].subexp * 2 + 1];
		}
		for (i = start; i < end; ++i)
		    line->charattrs[i] =
			adj_attr(line->charattrs[i], macro->subattr[x].attr);
		offset = ri->ovector[1];
	    }
	    if (!saved_ovector) {
		saved_ovector = ri->ovector;
		ri->ovector = NULL;
	    }
	} while (offset < line->len &&
	    tf_reg_exec(ri, text, NULL, offset) > 0);
	/* restore original startp/endp */
	if (saved_ovector) {
	    if (ri->ovector) FREE(ri->ovector);
	    ri->ovector = saved_ovector;
	}
	(ri->Str = line)->links++;
    }

    if (text)
        restore_reg_scope(old);
}

/* run a macro that has been selected by a trigger or hook */
static int run_match(
    Macro *macro,         /* macro to run */
    String *text,         /* argument text that matched trigger/hook */
    long hook)            /* hook vector */
{
    int ran = 0;
    struct Sock *callingsock = xsock;
    RegInfo *old;

    if (text)
        old = new_reg_scope(hook ? macro->hargs.ri : macro->trig.ri, text);

    /* Execute the macro. */
    if ((hook && hookflag) || (!hook && borg)) {
	callingsock = xsock;
        if (macro->prob == 100 || RRAND(0, 99) < macro->prob) {
            if (macro->shots && !--macro->shots) kill_macro(macro);
            if (mecho > macro->invis) {
                char numbuf[16];
                if (!*macro->name) sprintf(numbuf, "#%d", macro->num);
                tfprintf(tferr, "%S%s%s: /%s", do_mprefix(),
                    hook ? hook_name(hook)->data : "",
                    hook ? " HOOK" : "TRIGGER",
                    *macro->name ? macro->name : numbuf);
            }
            if (macro->body->len) {
                do_macro(macro, text, 0, hook ? USED_HOOK : USED_TRIG, NULL);
                ran += !macro->quiet;
            }
        }

	/* Restore xsock, in case macro called fg_sock().  main_loop() will
	 * set xsock=fsock, so any fg_sock() will effect xsock after the
	 * find_and_run_matches() loop is complete.
	 */
	xsock = callingsock;
    }

    if (text)
        restore_reg_scope(old);
    return ran;
}

#if USE_DMALLOC
void free_macros(void)
{
    while (maclist->head) nuke_macro((Macro *)maclist->head->datum);
    free_hash(macro_table);
}
#endif

