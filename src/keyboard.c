/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: keyboard.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

/**************************************************
 * Fugue keyboard handling.
 * Handles all keyboard input and keybindings.
 **************************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "keyboard.h"
#include "macro.h"		/* Macro, new_macro(), add_macro()... */
#include "output.h"		/* iputs(), idel(), redraw()... */
#include "history.h"		/* history_sub() */
#include "socket.h"		/* movesock() */
#include "expand.h"		/* process_macro() */

typedef struct KeyNode {
    int children;
    union {
        struct KeyNode **child;
        Macro *macro;
    } u;
} KeyNode;

static int literal_next = FALSE;
extern int echoflag;
int input_is_complete = FALSE;

static Macro   *FDECL(trie_insert,(KeyNode **root, Macro *macro, char *key));
static KeyNode *FDECL(untrie_key,(KeyNode **root, char *s));

/* The /dokey functions. (some are declared in output.h) */
static int NDECL(dokey_newline);
static int NDECL(dokey_recallb);
static int NDECL(dokey_recallf);
static int NDECL(dokey_searchb);
static int NDECL(dokey_searchf);
static int NDECL(dokey_socketb);
static int NDECL(dokey_socketf);
static int NDECL(dokey_bspc);
static int NDECL(dokey_bword);
static int NDECL(dokey_dline);
static int NDECL(dokey_dch);
static int NDECL(dokey_up);
static int NDECL(dokey_down);
static int NDECL(dokey_left);
static int NDECL(dokey_right);
static int NDECL(dokey_home);
static int NDECL(dokey_end);
static int NDECL(dokey_wleft);
static int NDECL(dokey_wright);
static int NDECL(dokey_deol);
static int NDECL(dokey_lnext);

STATIC_BUFFER(scratch);                 /* buffer for manipulating text */
STATIC_BUFFER(cat_keybuf);              /* Total buffer for /cat */
STATIC_BUFFER(current_input);           /* unprocessed keystrokes */
static KeyNode *keytrie = NULL;         /* root of keybinding trie */

Stringp keybuf;                         /* input buffer */
unsigned int keyboard_pos = 0;          /* current position in buffer */

typedef struct NamedEditFunc {          /* Editing function */
    char *name;
    EditFunc *func;
} NamedEditFunc;

static NamedEditFunc efunc[] = {
    { "RECALLB",  dokey_recallb  },
    { "RECALLF",  dokey_recallf  },
    { "SEARCHB",  dokey_searchb  },
    { "SEARCHF",  dokey_searchf  },
    { "SOCKETB",  dokey_socketb  },
    { "SOCKETF",  dokey_socketf  },
    { "BSPC"   ,  dokey_bspc     },
    { "BWORD"  ,  dokey_bword    },
    { "DLINE"  ,  dokey_dline    },
    { "DCH"    ,  dokey_dch      },
    { "REFRESH",  dokey_refresh  },
    { "REDRAW" ,  redraw         },
    { "UP"     ,  dokey_up       },
    { "DOWN"   ,  dokey_down     },
    { "RIGHT"  ,  dokey_right    },
    { "LEFT"   ,  dokey_left     },
    { "HOME"   ,  dokey_home     },
    { "END"    ,  dokey_end      },
    { "WLEFT"  ,  dokey_wleft    },
    { "WRIGHT" ,  dokey_wright   },
    { "DEOL"   ,  dokey_deol     },
    { "PAGE"   ,  dokey_page     },
    { "HPAGE"  ,  dokey_hpage    },
    { "LINE"   ,  dokey_line     },
    { "FLUSH"  ,  dokey_flush    },
    { "LNEXT"  ,  dokey_lnext    },
    { "NEWLINE",  dokey_newline  }
};

#define NFUNCS (sizeof(efunc) / sizeof(struct NamedEditFunc))

void init_keyboard()
{
    Stringinit(keybuf);
}

EditFunc *find_efunc(name)
    char *name;
{
    int i;

    for (i = 0; i < NFUNCS; i++)
        if (cstrcmp(name, efunc[i].name) == 0) break;
    return (i < NFUNCS) ? efunc[i].func : NULL;
}

/* Bind <cmd> to <key> */
void set_ekey(key, cmd)
    char *key, *cmd;
{
    Macro *macro;

    macro = new_macro("", NULL, key, 0, NULL, cmd, 0, 0, 0, TRUE);
    if (install_bind(macro)) add_macro(macro);
}

/* Find the macro assosiated with <key> sequence.  Pretty damn fast. */
Macro *find_key(key)
    char *key;
{
    KeyNode *n;

    for (n = keytrie; n && n->children && *key; n = n->u.child[*key++]);
    return (n && !n->children && !*key) ? n->u.macro : NULL;
}

/* Insert a macro into the keybinding trie */
static Macro *trie_insert(root, macro, key)
    KeyNode **root;
    Macro *macro;
    char *key;
{
    int i;

    if (!*root) {
        *root = (KeyNode *) MALLOC(sizeof(KeyNode));
        if (*key) {
            (*root)->children = 1;
            (*root)->u.child = (KeyNode **) MALLOC(128 * sizeof(KeyNode *));
            for (i = 0; i < 128; i++) (*root)->u.child[i] = NULL;
            return trie_insert(&(*root)->u.child[*key], macro, key + 1);
        } else {
            (*root)->children = 0;
            return (*root)->u.macro = macro;
        }
    } else {
        if (*key) {
            if ((*root)->children) {
                if (!(*root)->u.child[*key]) (*root)->children++;
                return trie_insert(&(*root)->u.child[*key], macro, key + 1);
            } else {
                tfprintf(tferr, "%% %s is prefixed by an existing sequence.",
                    ascii_to_print(macro->bind));
                return NULL;
            }
        } else {
            if ((*root)->children) {
                tfprintf(tferr, "%% %s is prefix of an existing sequence.",
                  ascii_to_print(macro->bind));
                return NULL;
            } else if (redef) {
                return (*root)->u.macro = macro;
            } else {
                tfprintf(tferr, "%% Binding %s already exists.",
                    ascii_to_print(macro->bind));
                return NULL;
            }
        }
    }
}

Macro *bind_key(macro)
    Macro *macro;
{
    return trie_insert(&keytrie, macro, macro->bind);
}

static KeyNode *untrie_key(root, s)
    KeyNode **root;
    char *s;
{
    if (!*s) {
        FREE(*root);
        return *root = NULL;
    }
    if (untrie_key(&((*root)->u.child[*s]), s + 1)) return *root;
    if (--(*root)->children) return *root;
    FREE((*root)->u.child);
    FREE(*root);
    return *root = NULL;
}

void unbind_key(macro)
    Macro *macro;
{
    untrie_key(&keytrie, macro->bind);
}

void do_grab(aline)
    Aline *aline;
{
    dokey_dline();
    handle_input_string(aline->str, aline->len);
}

void handle_keyboard_input()
{
    char *s, buf[64];
    int i, count;
    static KeyNode *n;
    static int key_start = 0;
    static int input_start = 0;
    static int place = 0;

    /* read a block of text */
    if ((count = read(0, buf, 64)) < 0) {
        if (errno == EINTR) return;
        else die("%% Couldn't read keyboard.\n");
    }
    if (!count) return;

    for (i = 0; i < count; i++) {
        /* make sure input is palatable */
        buf[i] &= 0x7f;
        if (buf[i] != '\0') Stringadd(current_input, buf[i]);
    }

    s = current_input->s;
    if (!n) n = keytrie;
    while (s[place]) {
        if (literal_next) {
            place++;
            key_start++;
            literal_next = FALSE;
            continue;
        }
        while (s[place] && n && n->children) n = n->u.child[s[place++]];
        if (!n || !keytrie->children) {
            /* No match.  Try a suffix. */
            place = ++key_start;
            n = keytrie;
        } else if (n->children) {
            /* Partial match.  Just hold on to it for now */
        } else {
            /* Total match.  Process everything up to this point, */
            /* and call the macro. */
            handle_input_string(s + input_start, key_start - input_start);
            key_start = input_start = place;
            do_macro(n->u.macro, "");
            n = keytrie;
        }
    }

    /* Process everything up to a possible match. */
    handle_input_string(s + input_start, key_start - input_start);

    /* Shift the window if there's no pending partial match. */
    if (!s[key_start]) {
        Stringterm(current_input, 0);
        place = key_start = 0;
    }
    input_start = key_start;
}

/* Update the input window and keyboard buffer. */
void handle_input_string(input, len)
    char *input;
    unsigned int len;
{
    int i, j;
    char save;

    if (len == 0) return;
    for (i = j = 0; i < len; i++) {
        if (isspace(input[i]))       /* convert newlines and tabs to spaces */
            input[j++] = ' ';
        else if (isprint(input[i]))
            input[j++] = input[i];
    }
    save = input[len = j];
    input[len] = '\0';
    if (echoflag || always_echo) iputs(input);
    input[len] = save;

    if (keyboard_pos == keybuf->len) {                    /* add to end */
        Stringncat(keybuf, input, len);
    } else if (insert) {                                  /* insert in middle */
        Stringcpy(scratch, keybuf->s + keyboard_pos);
        Stringterm(keybuf, keyboard_pos);
        Stringncat(keybuf, input, len);
        SStringcat(keybuf, scratch);
    } else if (keyboard_pos + len < keybuf->len) {        /* overwrite */
        for (i = 0, j = keyboard_pos; i < len; keybuf->s[j++] = input[i++]);
    } else {                                              /* write past end */
        Stringterm(keybuf, keyboard_pos);
        Stringncat(keybuf, input, len);
    }                      
    keyboard_pos += len;
}


/*
 *  Builtin key functions.
 */

static int dokey_newline()
{
    reset_outcount();
    inewline();
    /* If we actually process the input now, weird things will happen with
     * current_command and mecho.  So we just set a flag and wait until the
     * end of handle_command(), when things are cleaner.
     */
    input_is_complete = TRUE;
    return 1; /* return value isn't really used */
}

static int dokey_recallb()
{
    return recall_input(-1, FALSE);
}

static int dokey_recallf()
{
    return recall_input(1, FALSE);
}

static int dokey_searchb()
{
    return recall_input(-1, TRUE);
}

static int dokey_searchf()
{
    return recall_input(1, TRUE);
}

static int dokey_socketb()
{
    return movesock(-1);
}

static int dokey_socketf()
{
    return movesock(1);
}

int do_kbdel(place)
    int place;
{
    if (place >= 0 && place < keyboard_pos) {
        Stringcpy(scratch, keybuf->s + keyboard_pos);
        SStringcat(Stringterm(keybuf, place), scratch);
        idel(place);
    } else if (keyboard_pos < place && place < keybuf->len) {
        Stringcpy(scratch, keybuf->s + place);
        SStringcat(Stringterm(keybuf, keyboard_pos), scratch);
        idel(place);
    }
    return keyboard_pos;
}

static int dokey_bspc()
{
    return do_kbdel(keyboard_pos - 1);
}

static int dokey_dch()
{
    return do_kbdel(keyboard_pos + 1);
}

static int dokey_bword()
{
    int place;

    place = keyboard_pos - 1;
    while (place >= 0 && isspace(keybuf->s[place])) place--;
    while (place >= 0 && !isspace(keybuf->s[place])) place--;
    return do_kbdel(place + 1);
}

static int dokey_dline()
{
    Stringterm(keybuf, keyboard_pos = 0);
    logical_refresh();
    return keyboard_pos;
}

static int dokey_up()
{
    return newpos(keyboard_pos - getwrap());
}

static int dokey_down()
{
    return newpos(keyboard_pos + getwrap());
}

static int dokey_left()
{
    return newpos(keyboard_pos - 1);
}

static int dokey_right()
{
    return newpos(keyboard_pos + 1);
}

static int dokey_home()
{
    return newpos(0);
}

static int dokey_end()
{
    return newpos(keybuf->len);
}

static int dokey_wleft()
{
    int place;

    place = keyboard_pos - 1;
    while(isspace(keybuf->s[place])) place--;
    while(!isspace(keybuf->s[place])) if (--place < 0) break;
    place++;
    return newpos(place);
}

static int dokey_wright()
{
    int place;

    place = keyboard_pos;
    while (!isspace(keybuf->s[place]))
        if (++place > keybuf->len) break;
    while (isspace(keybuf->s[place])) place++;
    return newpos(place);
}

static int dokey_deol()
{
    int place = keybuf->len;
    Stringterm(keybuf, keyboard_pos);
    idel(place);
    return keyboard_pos;
}

static int dokey_lnext()
{
    return literal_next = TRUE;
}

int handle_input_line()
{
    extern int concat;

    SStringcpy(scratch, keybuf);
    Stringterm(keybuf, keyboard_pos = 0);
    input_is_complete = FALSE;

    if (concat) {
        if (scratch->s[0] == '.' && scratch->len == 1) {
            SStringcpy(scratch, cat_keybuf);
            Stringterm(cat_keybuf, 0);
            concat = 0;
        } else {
            SStringcat(cat_keybuf, scratch);
            if (concat == 2) Stringcat(cat_keybuf, "%;");
            return 0;
        }
    }

    if (kecho) tfprintf(tferr, "%s%S", kprefix, scratch);
    else if (scratch->len == 0 && visual) oputs("");

    if (*scratch->s == '^') {
        return history_sub(scratch->s + 1);
    }

    record_input(scratch->s);

    if (scratch->len)
        return process_macro(scratch->s, NULL, sub);
    else if (!snarf)
        return send_line("\n", 1);
    else
        return 0;
}

#ifdef DMALLOC
void free_keyboard()
{
    Stringfree(keybuf);
}
#endif
