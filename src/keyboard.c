/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: keyboard.c,v 33000.3 1994/04/03 00:51:32 hawkeye Exp $ */

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
#include "output.h"		/* iput(), idel(), redraw()... */
#include "history.h"		/* history_sub() */
#include "socket.h"		/* movesock() */
#include "expand.h"		/* process_macro() */
#include "search.h"
#include "commands.h"

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

int NDECL(dokey_dline);

STATIC_BUFFER(scratch);                 /* buffer for manipulating text */
STATIC_BUFFER(cat_keybuf);              /* Total buffer for /cat */
STATIC_BUFFER(current_input);           /* unprocessed keystrokes */
static KeyNode *keytrie = NULL;         /* root of keybinding trie */

Stringp keybuf;                         /* input buffer */
unsigned int keyboard_pos = 0;          /* current position in buffer */

/*
 * Some dokey operations are implemented internally with names like
 * DOKEY_FOO; others are implemented as macros in stdlib.tf with names
 * like /dokey_foo.  handle_dokey_command() looks first for an internal
 * function in efunc_table[], then for a macro, so all operations can be done
 * with "/dokey foo".  Conversely, internally-implemented operations should
 * have macros in stdlib.tf of the form "/def dokey_foo = /dokey foo",
 * so all operations can be performed with "/dokey_foo".
 */
static char *efunc_table[] = {
    "DLINE"  ,
    "FLUSH"  ,
    "HPAGE"  ,
    "LINE"   ,
    "LNEXT"  ,
    "NEWLINE",
    "PAGE"   ,
    "RECALLB",
    "RECALLF",
    "REDRAW" ,
    "REFRESH",
    "SEARCHB",
    "SEARCHF",
    "SOCKETB",
    "SOCKETF"
};

enum {
    DOKEY_DLINE  ,
    DOKEY_FLUSH  ,
    DOKEY_HPAGE  ,
    DOKEY_LINE   ,
    DOKEY_LNEXT  ,
    DOKEY_NEWLINE,
    DOKEY_PAGE   ,
    DOKEY_RECALLB,
    DOKEY_RECALLF,
    DOKEY_REDRAW ,
    DOKEY_REFRESH,
    DOKEY_SEARCHB,
    DOKEY_SEARCHF,
    DOKEY_SOCKETB,
    DOKEY_SOCKETF
};

void init_keyboard()
{
    Stringinit(keybuf);
}

/* Find the macro assosiated with <key> sequence. */
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

void handle_keyboard_input()
{
    char *s, buf[64];
    int i, count;
    static KeyNode *n;
    static int key_start = 0;
    static int input_start = 0;
    static int place = 0;

    /* read a block of text */
    if ((count = read(0, buf, sizeof(buf))) < 0) {
        if (errno == EINTR) return;
        perror("handle_keyboard_input: read");
        die("% Couldn't read keyboard.");
    }
    if (!count) return;

    for (i = 0; i < count; i++) {
        /* strip high bits and nul bytes */
        if ((buf[i] &= 0x7F)) Stringadd(current_input, buf[i]);
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

    if (len == 0) return;
    for (i = j = 0; i < len; i++) {
        if (isspace(input[i]))       /* convert newlines and tabs to spaces */
            input[j++] = ' ';
        else if (isprint(input[i]))
            input[j++] = input[i];
    }
    len = j;
    if (echoflag || always_echo) iput(input, len);

    if (keyboard_pos == keybuf->len) {                    /* add to end */
        Stringncat(keybuf, input, len);
    } else if (insert) {                                  /* insert in middle */
        Stringcpy(scratch, keybuf->s + keyboard_pos);
        Stringterm(keybuf, keyboard_pos);
        Stringncat(keybuf, input, len);
        SStringcat(keybuf, scratch);
    } else if (keyboard_pos + len < keybuf->len) {        /* overwrite */
        strncpy(keybuf->s + keyboard_pos, input, len);
    } else {                                              /* write past end */
        Stringterm(keybuf, keyboard_pos);
        Stringncat(keybuf, input, len);
    }                      
    keyboard_pos += len;
}


/*
 *  Builtin key functions.
 */

int handle_dokey_command(args)
    char *args;
{
    char **ptr;
    STATIC_BUFFER(buffer);
    Macro *macro;

    ptr = (char **)binsearch((GENERIC*)&args, (GENERIC*)efunc_table,
        sizeof(efunc_table)/sizeof(char*), sizeof(char*), gencstrcmp);

    if (!ptr) {
        Stringcat(Stringcpy(buffer, "dokey_"), args);
        if ((macro = find_macro(buffer->s))) return do_macro(macro, NULL);
        else tfprintf(tferr, "%% No editing function %s", args); 
        return 0;
    }

    switch (ptr - efunc_table) {

    case DOKEY_DLINE:      return dokey_dline();
    case DOKEY_FLUSH:      return dokey_flush();
    case DOKEY_HPAGE:      return dokey_hpage();
    case DOKEY_LINE:       return dokey_line();
    case DOKEY_LNEXT:      return literal_next = TRUE;

    case DOKEY_NEWLINE:
        reset_outcount();
        inewline();
        /* If we actually process the input now, weird things will happen with
         * current_command and mecho.  So we just set a flag and wait until the
         * end of handle_command(), when things are cleaner.
         */
        return input_is_complete = TRUE;  /* return value isn't really used */

    case DOKEY_PAGE:       return dokey_page();
    case DOKEY_RECALLB:    return recall_input(-1, FALSE);
    case DOKEY_RECALLF:    return recall_input(1, FALSE);
    case DOKEY_REDRAW:     return redraw();
    case DOKEY_REFRESH:    return logical_refresh();
    case DOKEY_SEARCHB:    return recall_input(-1, TRUE);
    case DOKEY_SEARCHF:    return recall_input(1, TRUE);
    case DOKEY_SOCKETB:    return movesock(-1);
    case DOKEY_SOCKETF:    return movesock(1);
    default:               return 0; /* impossible */
    }
}

int dokey_dline()
{
    Stringterm(keybuf, keyboard_pos = 0);
    logical_refresh();
    return keyboard_pos;
}

int do_kbdel(place)
    int place;
{
    if (place >= 0 && place < keyboard_pos) {
        Stringcpy(scratch, keybuf->s + keyboard_pos);
        SStringcat(Stringterm(keybuf, place), scratch);
        idel(place);
    } else if (keyboard_pos < place && place <= keybuf->len) {
        Stringcpy(scratch, keybuf->s + place);
        SStringcat(Stringterm(keybuf, keyboard_pos), scratch);
        idel(place);
    }
    return keyboard_pos;
}

#define isinword(c) (isalnum(c) || (wordpunct && strchr(wordpunct, (c))))

int do_kbwordleft()
{
    int place;

    place = keyboard_pos - 1;
    while (place >= 0 && !isinword(keybuf->s[place])) place--;
    while (place >= 0 && isinword(keybuf->s[place])) place--;
    return place + 1;
}

int do_kbwordright()
{
    int place;

    place = keyboard_pos;
    while (place < keybuf->len && !isinword(keybuf->s[place])) place++;
    while (place < keybuf->len && isinword(keybuf->s[place])) place++;
    return place;
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

    if (*scratch->s == '^')
        return history_sub(scratch->s + 1);

    record_input(scratch->s);

    return process_macro(scratch->s, NULL, sub);
}

#ifdef DMALLOC
void free_keyboard()
{
    Stringfree(keybuf);
}
#endif
