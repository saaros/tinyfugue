/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
static const char RCSid[] = "$Id: keyboard.c,v 35004.75 2003/12/22 05:35:17 hawkeye Exp $";

/**************************************************
 * Fugue keyboard handling.
 * Handles all keyboard input and keybindings.
 **************************************************/

#include "config.h"
#include "port.h"
#include "tf.h"
#include "util.h"
#include "search.h"
#include "tfio.h"
#include "macro.h"	/* Macro, find_macro(), do_macro()... */
#include "keyboard.h"
#include "output.h"	/* iput(), idel(), redraw()... */
#include "history.h"	/* history_sub() */
#include "expand.h"	/* macro_run() */
#include "commands.h"
#include "tty.h"	/* no_tty */
#include "variable.h"	/* unsetvar() */

static int literal_next = FALSE;
static TrieNode *keynode = NULL;	/* current node matched by input */

int pending_line = FALSE;
int pending_input = FALSE;
struct timeval keyboard_time;

static int  dokey_newline(void);
static int  replace_input(String *line);
static void handle_input_string(const char *input, unsigned int len);


STATIC_BUFFER(scratch);                 /* buffer for manipulating text */
STATIC_BUFFER(current_input);           /* unprocessed keystrokes */
static TrieNode *keytrie = NULL;        /* root of keybinding trie */

AUTO_BUFFER(keybuf);                    /* input buffer */
int keyboard_pos = 0;                   /* current position in buffer */

/*
 * Some dokey operations are implemented internally with names like
 * DOKEY_FOO; others are implemented as macros in stdlib.tf with names
 * like /dokey_foo.  handle_dokey_command() looks first for an internal
 * function in efunc_table[], then for a macro, so all operations can be done
 * with "/dokey foo".  Conversely, internally-implemented operations should
 * have macros in stdlib.tf of the form "/def dokey_foo = /dokey foo",
 * so all operations can be performed with "/dokey_foo".
 */
enum {
#define gencode(id) DOKEY_##id
#include "keylist.h"
#undef gencode
};

static const char *efunc_table[] = {
#define gencode(id) #id
#include "keylist.h"
#undef gencode
};

#define kbnumval	(kbnum ? atoi(kbnum->data) : 1)


void init_keyboard(void)
{
    gettime(&keyboard_time);
}

/* Find the macro assosiated with <key> sequence. */
Macro *find_key(const char *key)
{
    return (Macro *)trie_find(keytrie, (unsigned char*)key);
}

int bind_key(Macro *spec)   /* install Macro's binding in key structures */
{
    Macro *macro;
    int status;

    if ((macro = find_key(spec->bind))) {
        if (redef) {
            kill_macro(macro);
            /* intrie is guaranteed to succeed */
        } else {
            eprintf("Binding %S already exists.", ascii_to_print(spec->bind));
            return 0;
        }
    }

    status = intrie(&keytrie, spec, (unsigned char*)spec->bind);

    if (status < 0) {
        eprintf("'%S' is %s an existing keybinding.",
            ascii_to_print(spec->bind),
            (status == TRIE_SUPER) ? "prefixed by" : "a prefix of");
        return 0;
    }

    if (macro && redef)
        do_hook(H_REDEF, "!Redefined %s %S", "%s %S",
            "binding", ascii_to_print(spec->bind));

    return 1;
}

void unbind_key(Macro *macro)
{
    untrie(&keytrie, (unsigned char*)macro->bind);
    keynode = NULL;  /* in case it pointed to a node that no longer exists */
}

/* returns 0 at EOF, 1 otherwise */
int handle_keyboard_input(int read_flag)
{
    char buf[64];
    const char *s;
    int i, count = 0;
    static int key_start = 0;
    static int input_start = 0;
    static int place = 0;
    static int eof = 0;

    /* Solaris select() incorrectly reports the terminal as readable if
     * the user typed LNEXT or FLUSH, when in fact there is nothing to read.
     * So we wait for read() to return 0 several times in a row
     * (hopefully, more times than anyone would realistically type FLUSH in
     * a row) before deciding it's EOF.
     */

    if (eof < 100 && read_flag) {
        /* read a block of text */
        if ((count = read(STDIN_FILENO, buf, sizeof(buf))) < 0) {
            /* error or interrupt */
            if (errno == EINTR) return 1;
            die("handle_keyboard_input: read", errno);
        } else if (count > 0) {
            /* something was read */
	    eof = 0;
            gettime(&keyboard_time);
        } else {
            /* nothing was read, and nothing is buffered */
	    /* Don't close stdin; we might be wrong (solaris bug), and we
	     * don't want the fd to be reused anyway. */
	    eof++;
#if 0
            if (!no_tty)
                internal_error(__FILE__, __LINE__, "read 0 from stdin tty");
#endif
        }
    }

    if (count == 0 && place == 0)
	goto end;

    for (i = 0; i < count; i++) {
        if (istrip) buf[i] &= 0x7F;
        if (buf[i] & 0x80) {
	    if (!literal_next &&
		(meta_esc == META_ON || (!is_print(buf[i]) && meta_esc)))
	    {
		Stringadd(current_input, '\033');
		buf[i] &= 0x7F;
	    }
	    if (!is_print(buf[i]))
		buf[i] &= 0x7F;
        }
        Stringadd(current_input, mapchar(buf[i]));
    }

    s = current_input->data;
    if (!s) /* no good chars; current_input not yet allocated */
	goto end;
    while (place < current_input->len) {
        if (!keynode) keynode = keytrie;
        if ((pending_input = pending_line))
            break;
        if (literal_next) {
            place++;
            key_start++;
            literal_next = FALSE;
            continue;
        }
        while (place < current_input->len && keynode && keynode->children)
            keynode = keynode->u.child[(unsigned char)s[place++]];
        if (!keynode) {
            /* No keybinding match; check for builtins. */
            if (s[key_start] == '\n' || s[key_start] == '\r') {
                handle_input_string(s + input_start, key_start - input_start);
                place = input_start = ++key_start;
                dokey_newline();
                /* handle_input_line(); */
            } else if (s[key_start] == '\b' || s[key_start] == '\177') {
                handle_input_string(s + input_start, key_start - input_start);
                place = input_start = ++key_start;
                do_kbdel(keyboard_pos - kbnumval);
		reset_kbnum();
            } else if (kbnum && is_digit(s[key_start]) &&
		key_start == input_start)
	    {
		int n;
		Stringadd(kbnum, s[key_start]);
		place = input_start = ++key_start;
		n = kbnumval < 0 ? -kbnumval : kbnumval;
		if (max_kbnum > 0 && n > max_kbnum)
		    Sprintf(kbnum, "%c%d", kbnumval<0 ? '-' : '+', max_kbnum);
		update_status_field(&special_var[VAR_kbnum], -1);
            } else {
                /* No builtin; try a suffix. */
                place = ++key_start;
            }
            keynode = NULL;
        } else if (!keynode->children) {
            /* Total match; process everything up to here and call the macro. */
	    AUTO_BUFFER(kbnumlocalbuf);
	    String *kbnumlocal = NULL;
            Macro *macro = (Macro *)keynode->u.datum;
            handle_input_string(s + input_start, key_start - input_start);
            key_start = input_start = place;
            keynode = NULL;  /* before do_macro(), for reentrance */
	    if (kbnum) {
		kbnumlocal = kbnumlocalbuf;
		SStringcpy(kbnumlocal, kbnum);
		reset_kbnum();
	    }
            do_macro(macro, NULL, 0, USED_KEY, kbnumlocal);
	    if (kbnumlocal) Stringfree(kbnumlocal);
        } /* else, partial match; just hold on to it for now. */
    }

    /* Process everything up to a possible match. */
    handle_input_string(s + input_start, key_start - input_start);

    /* Shift the window if there's no pending partial match. */
    if (key_start >= current_input->len) {
        Stringtrunc(current_input, 0);
        place = key_start = 0;
    }
    input_start = key_start;
    if (pending_line && !read_depth)
        handle_input_line();
end:
    return eof < 100;
}

/* Update the input window and keyboard buffer. */
static void handle_input_string(const char *input, unsigned int len)
{
    int putlen = len, extra = 0;

    if (len == 0) return;
    if (kbnum) {
	if (kbnumval > 1) {
	    extra = kbnumval - 1;
	    putlen = len + extra;
	}
	reset_kbnum();
    }

    /* if this is a fresh line, input history is already synced;
     * if user deleted line, input history is already synced;
     * if called from replace_input, we don't want input history synced.
     */
    if (keybuf->len) sync_input_hist();

    if (keyboard_pos == keybuf->len) {                    /* add to end */
	if (extra) {
	    Stringnadd(keybuf, *input, extra);
	    keyboard_pos += extra;
	}
	Stringncat(keybuf, input, len);
    } else if (insert) {                                  /* insert in middle */
        Stringcpy(scratch, keybuf->data + keyboard_pos);
        Stringtrunc(keybuf, keyboard_pos);
	if (extra) {
	    Stringnadd(keybuf, *input, extra);
	    keyboard_pos += extra;
	}
        Stringncat(keybuf, input, len);
        SStringcat(keybuf, scratch);
    } else if (keyboard_pos + len + extra < keybuf->len) {    /* overwrite */
	while (extra) {
	    keybuf->data[keyboard_pos++] = *input;
	    extra--;
	}
	memcpy(keybuf->data + keyboard_pos, input, len);
    } else {                                              /* write past end */
        Stringtrunc(keybuf, keyboard_pos);
	if (extra) {
	    Stringnadd(keybuf, *input, extra);
	    keyboard_pos += extra;
	}
        Stringncat(keybuf, input, len);
    }                      
    keyboard_pos += len;
    iput(putlen);
}


struct Value *handle_input_command(String *args, int offset)
{
    handle_input_string(args->data + offset, args->len - offset);
    return shareval(val_one);
}


/*
 *  Builtin key functions.
 */

struct Value *handle_dokey_command(String *args, int offset)
{
    const char **ptr;
    STATIC_BUFFER(buffer);
    Macro *macro;
    int n;

    n = kbnumval;
    if (kbnum) reset_kbnum();

    ptr = (const char **)binsearch((void*)(args->data + offset),
        (void*)efunc_table,
        sizeof(efunc_table)/sizeof(char*), sizeof(char*), cstrstructcmp);

    if (!ptr) {
        SStringocat(Stringcpy(buffer, "dokey_"), args, offset);
        if ((macro = find_macro(buffer->data)))
            return newint(do_macro(macro, NULL, 0, USED_NAME, NULL));
        else eprintf("No editing function %s", args->data + offset); 
        return shareval(val_zero);
    }

    switch (ptr - efunc_table) {

    case DOKEY_CLEAR:      return newint(clear_display_screen());
    case DOKEY_FLUSH:      return newint(screen_end(0));
    case DOKEY_LNEXT:      return newint(literal_next = TRUE);
    case DOKEY_NEWLINE:    return newint(dokey_newline());
    case DOKEY_PAUSE:      return newint(pause_screen());
    case DOKEY_RECALLB:    return newint(replace_input(recall_input(-n,0)));
    case DOKEY_RECALLBEG:  return newint(replace_input(recall_input(-n,2)));
    case DOKEY_RECALLEND:  return newint(replace_input(recall_input(n,2)));
    case DOKEY_RECALLF:    return newint(replace_input(recall_input(n,0)));
    case DOKEY_REDRAW:     return newint(redraw());
    case DOKEY_REFRESH:    return newint((logical_refresh(), keyboard_pos));
    case DOKEY_SEARCHB:    return newint(replace_input(recall_input(-n,1)));
    case DOKEY_SEARCHF:    return newint(replace_input(recall_input(n,1)));
    case DOKEY_SELFLUSH:   return newint(selflush());
    default:               return shareval(val_zero); /* impossible */
    }
}

static int dokey_newline(void)
{
    reset_outcount(NULL);
    inewline();
    /* We might be in the middle of a macro (^M -> /dokey newline) now,
     * so we can't process the input now, or weird things will happen with
     * current_command and mecho.  So we just set a flag and wait until
     * later when things are cleaner.
     */
    pending_line = TRUE;
    return 1;  /* return value isn't really used */
}

static int replace_input(String *line)
{
    if (!line) {
        dobell(1);
        return 0;
    }
    if (keybuf->len) {
        Stringtrunc(keybuf, keyboard_pos = 0);
        logical_refresh();
    }
    handle_input_string(line->data, line->len);
    return 1;
}

int do_kbdel(int place)
{
    if (place >= 0 && place < keyboard_pos) {
        Stringcpy(scratch, keybuf->data + keyboard_pos);
        SStringcat(Stringtrunc(keybuf, place), scratch);
        idel(place);
    } else if (place > keyboard_pos && place <= keybuf->len) {
        Stringcpy(scratch, keybuf->data + place);
        SStringcat(Stringtrunc(keybuf, keyboard_pos), scratch);
        idel(place);
    } else {
        dobell(1);
    }
    sync_input_hist();
    return keyboard_pos;
}

#define is_inword(c) (is_alnum(c) || (wordpunct && strchr(wordpunct, (c))))

int do_kbword(int start, int dir)
{
    int stop = (dir < 0) ? -1 : keybuf->len;
    int place = start<0 ? 0 : start>keybuf->len ? keybuf->len : start;
    place -= (dir < 0);

    while (place != stop && !is_inword(keybuf->data[place])) place += dir;
    while (place != stop && is_inword(keybuf->data[place])) place += dir;
    return place + (dir < 0);
}

int do_kbmatch(int start)
{
    static const char *braces = "(){}[]";
    const char *type;
    int dir, stop, depth = 0;
    int place = start<0 ? 0 : start>keybuf->len ? keybuf->len : start;

    while (1) {
        if (place >= keybuf->len) return -1;
        if ((type = strchr(braces, keybuf->data[place]))) break;
        ++place;
    }
    dir = ((type - braces) % 2) ? -1 : 1;
    stop = (dir < 0) ? -1 : keybuf->len;
    do {
        if      (keybuf->data[place] == type[0])   depth++;
        else if (keybuf->data[place] == type[dir]) depth--;
        if (depth == 0) return place;
    } while ((place += dir) != stop);
    return -1;
}

int handle_input_line(void)
{
    String *line;
    int result;

    SStringcpy(scratch, keybuf);
    Stringtrunc(keybuf, keyboard_pos = 0);
    pending_line = FALSE;

    if (*scratch->data == '^') {
        if (!(line = history_sub(scratch))) {
            oputs("% No match.");
            return 0;
        }
        SStringcpy(keybuf, line);
        iput(keyboard_pos = keybuf->len);
        inewline();
        Stringtrunc(keybuf, keyboard_pos = 0);
    } else
        line = scratch;

    if (kecho) tfprintf(tferr, "%s%S", kprefix ? kprefix : "", line);
    gettime(&line->time);
    record_input(line);
    readsafe = 1;
    result = macro_run(line, 0, NULL, 0, sub, "\bUSER");
    readsafe = 0;
    return result;
}

#if USE_DMALLOC
void free_keyboard(void)
{
    tfclose(tfkeyboard);
    Stringfree(keybuf);
    Stringfree(scratch);
    Stringfree(current_input);
}
#endif
