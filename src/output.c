/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.c,v 35004.56 1997/11/20 07:16:32 hawkeye Exp $ */


/*****************************************************************
 * Fugue output handling
 *
 * Written by Ken Keys (Hawkeye).
 * Handles all screen-related phenomena.
 *****************************************************************/

#define TERM_vt100	1
#define TERM_vt220	2
#define TERM_ansi	3

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "socket.h"	/* fgprompt(), fgname() */
#include "output.h"
#include "macro.h"	/* add_ibind(), rebind_key_macros() */
#include "search.h"
#include "tty.h"	/* init_tty(), get_window_wize() */
#include "variable.h"
#include "expand.h"	/* evalexpr() */
#include "keyboard.h"	/* keyboard_pos */

#ifdef EMXANSI
# define INCL_VIO
# include <os2.h>
#endif

/* Terminal codes and capabilities.
 * Visual mode requires at least clear_screen and cursor_address.  The other
 *   codes are good to have, but are not strictly necessary.
 * Scrolling in visual mode requires set_scroll_region (preferred), or
 *   insert_line and delete_line (may appear jumpy), or EMXANSI.
 * Fast insert in visual mode requires insert_start and insert_end (preferred),
 *   or insert_char.
 * Fast delete in visual mode requires delete_char.
 * Attributes require the appropriate attribute code and attr_off; but if
 *   attr_off is not defined, underline and standout (replacement for bold)
 *   can still be used if underline_off and standout_off are defined.
 */

#define DEFAULT_LINES   24
#define DEFAULT_COLUMNS 80

#ifdef HARDCODE
# define origin 1      /* top left corner is (1,1) */
# if HARDCODE == TERM_vt100
#  define TERMCODE(id, vt100, vt220, ansi)   static CONST char *(id) = (vt100);
# else
#  if HARDCODE == TERM_vt220
#   define TERMCODE(id, vt100, vt220, ansi)   static CONST char *(id) = (vt220);
#  else
#   if HARDCODE == TERM_ansi
#    define TERMCODE(id, vt100, vt220, ansi)   static CONST char *(id) = (ansi);
#   endif
#  endif
# endif
#else
# define origin 0      /* top left corner is (0,0) */
# define TERMCODE(id, vt100, vt220, ansi)   static CONST char *(id) = NULL;
#endif

/*				vt100		vt220		ansi */
/*				-----		-----		---- */
#ifdef __CYGWIN32__  /* "\033[J" is broken in CYGWIN32 b18. */
TERMCODE (clear_screen,		NULL,		NULL,		NULL)
TERMCODE (clear_to_eos,		NULL,		NULL,		NULL)
#else
TERMCODE (clear_screen,		"\033[H\033[J", "\033[H\033[J", "\033[H\033[J")
TERMCODE (clear_to_eos,		"\033[J",	"\033[J",	"\033[J")
#endif
TERMCODE (clear_to_eol,		"\033[K",	"\033[K",	"\033[K")
TERMCODE (cursor_address,	"\033[%d;%dH",	"\033[%d;%dH",	"\033[%d;%dH")
TERMCODE (enter_ca_mode,	NULL,		NULL,		NULL)
TERMCODE (exit_ca_mode,		NULL,		NULL,		NULL)
TERMCODE (set_scroll_region,	"\033[%d;%dr",	"\033[%d;%dr",	NULL)
TERMCODE (insert_line,		NULL,		"\033[L",	"\033[L")
TERMCODE (delete_line,		NULL,		"\033[M",	"\033[M")
TERMCODE (delete_char,		NULL,		"\033[P",	"\033[P")
#ifdef __CYGWIN32__  /* "\033[@" is broken in CYGWIN32 b18. */
TERMCODE (insert_char,		NULL,		NULL,		NULL)
#else
TERMCODE (insert_char,		NULL,		"\033[@",	"\033[@")
#endif
TERMCODE (insert_start,		NULL,		NULL,		"\033[4h")
TERMCODE (insert_end,		NULL,		NULL,		"\033[4l")
TERMCODE (underline,		"\033[4m",	"\033[4m",	"\033[4m")
TERMCODE (reverse,		"\033[7m",	"\033[7m",	"\033[7m")
TERMCODE (flash,		"\033[5m",	"\033[5m",	"\033[5m")
TERMCODE (dim,			NULL,		NULL,		NULL)
TERMCODE (bold,			"\033[1m",	"\033[1m",	"\033[1m")
TERMCODE (attr_off,		"\033[m",	"\033[m",	"\033[m")
/* these are only used if others are missing */
TERMCODE (underline_off,	NULL,		NULL,		NULL)
TERMCODE (standout,		NULL,		NULL,		NULL)
TERMCODE (standout_off,		NULL,		NULL,		NULL)

#ifdef HARDCODE
# define key_ku "\033[A"
# define key_kd "\033[B"
# define key_kr "\033[C"
# define key_kl "\033[D"
#else
# define key_ku
# define key_kd
# define key_kr
# define key_kl
#endif

/* end HARDCODE section */


typedef struct status_field {
    char *name;
    int internal;
    Var *var;
    int width;
    int rightjust;
    int column;
    attr_t attrs;
} StatusField;

static void  NDECL(init_term);
static int   FDECL(fbufputc,(int c));
static void  NDECL(bufflush);
static void  FDECL(tp,(CONST char *str));
static void  FDECL(xy,(int x, int y));
static void  NDECL(clr);
static void  NDECL(clear_line);
static void  NDECL(clear_input_line);
static void  FDECL(clear_lines,(int start, int end));
static void  NDECL(clear_input_window);
static void  FDECL(setscroll,(int top, int bottom));
static void  FDECL(scroll_input,(int n));
static void  FDECL(ictrl_put,(CONST char *s, int n));
static int   FDECL(ioutputs,(CONST char *str, int len));
static void  FDECL(ioutall,(int kpos));
static void  FDECL(format_status_field,(StatusField *field));
static void  FDECL(attributes_off,(attr_t attrs));
static void  FDECL(attributes_on,(attr_t attrs));
static void  FDECL(color_on,(long color));
static void  FDECL(hwrite,(Aline *line, int offset));
static void  FDECL(set_attr,(Aline *aline, char *dest, attr_t *starting,
             attr_t *current));
static int   NDECL(check_more);
static int   FDECL(clear_more,(int new));
static Aline *NDECL(wrapline);
static void  NDECL(output_novisual);
#ifdef SCREEN
static void  NDECL(output_noscroll);
static void  NDECL(output_scroll);
#endif

#ifdef TERMCAP
#define tpgoto(seq,x,y)  tp(tgoto(seq, (x)-1+origin, (y)-1+origin))
#else
#define tpgoto(seq,x,y)  Sprintf(outbuf,SP_APPEND,seq,(y)-1+origin,(x)-1+origin)
#endif

#define ipos()		xy(ix, iy)

/* Buffered output */

#define bufputs(s)		Stringcat(outbuf, s)
#define bufputns(s, n)		Stringncat(outbuf, s, n)
#define bufputc(c)		Stringadd(outbuf, c)
#define bufputnc(c, n)		Stringnadd(outbuf, c, n)
#define bufprintf1(f, p)	Sprintf(outbuf, SP_APPEND, f, p)

#ifdef EMXANSI /* OS2 */
   static void FDECL(crnl,(int n));  
#else
# ifdef USE_SGTTY  /* CRMOD is off (tty.c) */
#  define crnl(count)  do { bufputc('\r'); bufputnc('\n', count); } while (0)
# else             /* ONLCR is on (tty.c) */
#  define crnl(count)  bufputnc('\n', count)
# endif
#endif


/* Others */

#define Wrap (wrapsize ? wrapsize : columns)
#define more_attrs (F_BOLD | F_REVERSE)
#define moremin 1
#define morewait 50

TIME_T clock_update = 0;            /* when clock needs to be updated */

STATIC_BUFFER(outbuf);              /* output buffer */
static int top_margin = -1, bottom_margin = -1;	/* scroll region */
static int cx = -1, cy = -1;        /* Real cursor ((-1,-1)==unknown) */
static int ox = 1, oy = 1;          /* Output cursor */
static int ix, iy;                  /* Input cursor */
static int ystatus;                 /* line # of status bar */
static int istarty, iendy, iendx;   /* start/end of current input line */
static Aline *moreprompt;           /* pager prompt */
static Aline *prompt;               /* current prompt */
static int outcount;                /* lines remaining until more prompt */
static attr_t have_attr = 0;        /* available attributes */
static Aline *currentline = NULL;   /* current logical line for printing */
static int wrap_offset;             /* physical offset into currentline */
static int screen_mode = -1;        /* -1=unset, 0=nonvisual, 1=visual */
static int output_disabled = 1;     /* is it safe to oflush()? */
static int can_have_visual = FALSE;
static List status_field_list[1];
static int status_left = 0, status_right = 0;  /* size of status line pieces */

#ifndef EMXANSI
# define has_scroll_region (set_scroll_region != NULL)
#else
# define has_scroll_region (1)
#endif

typedef struct Keycode {
    CONST char *name, *capname, *code;
} Keycode;

static Keycode keycodes[] = {  /* this list is sorted! */
/*  { "Back Tab",	"kB", NULL }, */
    { "Backspace",	"kb", NULL },
/*  { "Clear All Tabs",	"ka", NULL }, */
    { "Clear EOL",	"kE", NULL },
    { "Clear EOS",	"kS", NULL },
    { "Clear Screen",	"kC", NULL },
/*  { "Clear Tab",	"kt", NULL }, */
    { "Delete",		"kD", NULL },
    { "Delete Line",	"kL", NULL },
    { "Down",		"kd", key_kd },
/*  { "End Insert",	"kM", NULL }, */
    { "F0",		"k0", NULL },
    { "F1",		"k1", NULL },
    { "F10",		"k;", NULL },
    { "F11",		"F1", NULL },
    { "F12",		"F2", NULL },
    { "F13",		"F3", NULL },
    { "F14",		"F4", NULL },
    { "F15",		"F5", NULL },
    { "F16",		"F6", NULL },
    { "F17",		"F7", NULL },
    { "F18",		"F8", NULL },
    { "F19",		"F9", NULL },
    { "F2",		"k2", NULL },
    { "F3",		"k3", NULL },
    { "F4",		"k4", NULL },
    { "F5",		"k5", NULL },
    { "F6",		"k6", NULL },
    { "F7",		"k7", NULL },
    { "F8",		"k8", NULL },
    { "F9",		"k9", NULL },
    { "Home",		"kh", NULL },
    { "Home Down",	"kH", NULL },
    { "Insert",		"kI", NULL },
    { "Insert Line",	"kA", NULL },
    { "KP1",		"K1", NULL },
    { "KP2",		"K2", NULL },
    { "KP3",		"K3", NULL },
    { "KP4",		"K4", NULL },
    { "KP5",		"K5", NULL },
    { "Left",		"kl", key_kl },
    { "PgDn",		"kN", NULL },
    { "PgUp",		"kP", NULL },
    { "Right",		"kr", key_kr },
    { "Scroll Down",	"kF", NULL },
    { "Scroll Up",	"kR", NULL },
/*  { "Set Tab",	"kT", NULL }, */
    { "Up",		"ku", key_ku }
};

#define N_KEYCODES	(sizeof(keycodes)/sizeof(Keycode))

int lines   = DEFAULT_LINES;
int columns = DEFAULT_COLUMNS;
int need_refresh = 0;               /* does input need refresh? */
int need_more_refresh = 0;          /* does visual more prompt need refresh? */
int sockecho = TRUE;                /* echo input? */
unsigned int tfscreen_size;         /* # of lines in tfscreen */
int paused = FALSE;                 /* output paused */

#ifdef TERMCAP
extern int   FDECL(tgetent,(char *buf, CONST char *name));
extern int   FDECL(tgetnum,(CONST char *id));
extern char *FDECL(tgetstr,(CONST char *id, char **area));
extern char *FDECL(tgoto,(CONST char *code, int destcol, int destline));
extern int   FDECL(tputs,(CONST char *cp, int affcnt, int (*outc)(int)));
#endif

/****************************
 * BUFFERED OUTPUT ROUTINES *
 ****************************/

static void bufflush()
{
    if (outbuf->len) {
        write(STDOUT_FILENO, outbuf->s, outbuf->len);
        Stringterm(outbuf, 0);
    }
}

static int fbufputc(c)
    int c;
{
    Stringadd(outbuf, c);
    return c;
}

#ifdef EMXANSI
void crnl(n)
    int n;
{
    int off = (cy + n) - bottom_margin;
    if (off < 0 || !visual) off = 0;
    bufputnc('\n', n - off);
    if (off) {
        bufflush();
        VioScrollUp(top_margin-1, 0, bottom_margin-1, columns, off, " \x07", 0);
        bufputc('\r');
    }
}
#endif

void bell(n)
    int n;
{
    if (beep) {
        bufputnc('\007', n);
        bufflush();
    }
}

/********************/

int change_term()
{
    int old = visual;
    if (old == 1) setvar("visual", "0", FALSE);
    init_term();
    if (old == 1) setvar("visual", "1", FALSE);
    rebind_key_macros();
    return 1;
}

/* Initialize output data. */
void init_output()
{
    CONST char *str;

    init_list(status_field_list);
    init_tty();

    /* Window size: clear defaults, then try:
     * environment, ioctl TIOCGWINSZ, termcap, defaults.
     */
    lines = ((str = getvar("LINES"))) ? atoi(str) : 0;
    columns = ((str = getvar("COLUMNS"))) ? atoi(str) : 0;
    if (lines <= 0 || columns <= 0) get_window_size();
    ystatus = lines - isize;

    prompt = fgprompt();
    (moreprompt = new_aline("--More--", more_attrs))->links++;

    init_term();
    ch_hilite();
    setup_screen(-1);
    output_disabled = 0;
}

/********************
 * TERMCAP ROUTINES *
 ********************/

static void init_term()
{
#ifdef TERMCAP
    int i;
    /* Termcap entries are supposed to fit in 1024 bytes.  But, if a 'tc'
     * field is present, some termcap libraries will just append the second
     * entry to the original.  Also, some overzealous versions of tset will
     * also expand 'tc', so there might be *2* entries appended to the
     * original.  And, linux termcap adds 'li' and 'co' fields, so it could
     * get even longer.  To top it all off, some termcap libraries don't
     * do any length checking in tgetent().  We should be safe with 4096.
     */
    char termcap_entry[4096];
    /* termcap_buffer will hold at most 1 copy of any field; 1024 is enough. */
    static char termcap_buffer[1024];
    char *area = termcap_buffer;

    have_attr = 0;
    can_have_visual = FALSE;
    clear_screen = clear_to_eos = clear_to_eol = NULL;
    set_scroll_region = insert_line = delete_line = NULL;
    delete_char = insert_char = insert_start = insert_end = NULL;
    enter_ca_mode = exit_ca_mode = cursor_address = NULL;
    standout = underline = reverse = flash = dim = bold = NULL;
    standout_off = underline_off = attr_off = NULL;

    if (!TERM || !*TERM) {
        tfprintf(tferr, "%% Warning: TERM undefined.");
    } else if (tgetent(termcap_entry, TERM) <= 0) {
        tfprintf(tferr, "%% Warning: \"%s\" terminal unsupported.", TERM);
    } else {
        if (columns <= 0) columns = tgetnum("co");
        if (lines   <= 0) lines   = tgetnum("li");

        clear_screen         = tgetstr("cl", &area);
        clear_to_eol         = tgetstr("ce", &area);
        clear_to_eos         = tgetstr("cd", &area);
        enter_ca_mode        = tgetstr("ti", &area);
        exit_ca_mode         = tgetstr("te", &area);
        cursor_address       = tgetstr("cm", &area);
        set_scroll_region    = tgetstr("cs", &area);
        delete_char          = tgetstr("dc", &area);
        insert_char          = tgetstr("ic", &area);
        insert_start         = tgetstr("im", &area);
        insert_end           = tgetstr("ei", &area);
        insert_line          = tgetstr("al", &area);
        delete_line          = tgetstr("dl", &area);

        underline	= tgetstr("us", &area);
        reverse		= tgetstr("mr", &area);
        flash		= tgetstr("mb", &area);
        dim		= tgetstr("mh", &area);
        bold		= tgetstr("md", &area);
        standout	= tgetstr("so", &area);
        underline_off	= tgetstr("ue", &area);
        standout_off	= tgetstr("se", &area);
        attr_off	= tgetstr("me", &area);

        if (!attr_off) {
            /* can't exit all attrs, but maybe can exit underline/standout */
            reverse = flash = dim = bold = NULL;
            if (!underline_off) underline = NULL;
            if (!standout_off) standout = NULL;
        }

        for (i = 0; i < N_KEYCODES; i++) {
            keycodes[i].code = tgetstr(keycodes[i].capname, &area);
#if 0
            fprintf(stderr, "(%2s) %-8s = %s\n",
                keycodes[i].capname, keycodes[i].name,
                ascii_to_print(keycodes[i].code));
#endif
        }

        if (strcmp(TERM, "xterm") == 0) {
            /* Don't use secondary xterm buffer. */
            enter_ca_mode = exit_ca_mode = NULL;
            /* Many old xterm termcaps mistakenly omit "cs". */
            if (!set_scroll_region && strcmp(TERM, "xterm") == 0)
                set_scroll_region = "\033[%i%d;%dr";
        }
    }

#ifdef EMXANSI
    VioSetAnsi(1,0);                   /* ensure ansi-mode */
#endif /* EMXANSI */

#endif /* TERMCAP */

#if 1
    /* The insert_start code in iput() is apparently buggy.  Until it is
     * fixed, we ignore the insert_start capability. */
    insert_start = NULL;
#endif
    if (!insert_end) insert_start = NULL;

    if (columns <= 0) columns = DEFAULT_COLUMNS;
    if (lines   <= 0) lines   = DEFAULT_LINES;
    setivar("wrapsize", columns - 1, FALSE);
    outcount = lines;
    ix = 1;
    can_have_visual = (clear_screen || clear_to_eol) && cursor_address;
    setivar("scroll", has_scroll_region||(insert_line&&delete_line), FALSE);
    have_attr = F_BELL;
    if (underline) have_attr |= F_UNDERLINE;
    if (reverse)   have_attr |= F_REVERSE;
    if (flash)     have_attr |= F_FLASH;
    if (dim)       have_attr |= F_DIM;
    if (bold)      have_attr |= F_BOLD;
    if (standout)  have_attr |= F_BOLD;
}

static void setscroll(top,bottom)
    int top, bottom;
{
    if (top_margin == top && bottom_margin == bottom) return; /* optimization */
#ifdef EMXANSI
    bufflush();
#else
    if (!set_scroll_region) return;
    tpgoto(set_scroll_region, (bottom), (top));
    cx = cy = -1;   /* cursor position is undefined */
#endif
    bottom_margin = bottom;
    top_margin = top;
    if (top != 1 || bottom != lines) set_refresh_pending(REF_PHYSICAL);
}

static void xy(x,y)
    int x, y;
{
    if (x == cx && y == cy) return;                    /* already there */
    if (cy < 0 || cx < 0 || cx > columns) {            /* direct movement */
        tpgoto(cursor_address, x, y);
    } else if (x == 1 && y == cy) {                    /* optimization */
        bufputc('\r');
    } else if (x == 1 && y > cy && y < cy + 5 &&       /* optimization... */
      cy >= top_margin && y <= bottom_margin) {        /* if '\n' is safe */
        /* Some broken emulators (including CYGWIN32 b18) lose
         * attributes when \r\n is printed, so we print \n\r instead.
         */
        bufputnc('\n', y - cy);
        if (cx != 1) bufputc('\r');
    } else if (y == cy && x < cx && x > cx - 7) {      /* optimization */
        bufputnc('\010', cx - x);
    } else {                                           /* direct movement */
        tpgoto(cursor_address, x, y);
    }
    cx = x;  cy = y;
}

static void clr()
{
    if (clear_screen)
        tp(clear_screen);
    else {
        clear_lines(1, lines);
        xy(1, 1);
    }
    cx = 1;  cy = 1;
}

static void clear_line()
{
    if (cx != 1) bufputc('\r');
    cx = 1;
    if (clear_to_eol) {
        tp(clear_to_eol);
    } else {
        bufputnc(' ', Wrap);
        bufputc('\r');
    }
}

static void tp(str)
    CONST char *str;
{
    if (str)
#ifdef TERMCAP
        tputs(str, 1, fbufputc);
#else
        bufputs(str);
#endif
}

CONST char *get_keycode(name)
    CONST char *name;
{
    Keycode *keycode;

    keycode = (Keycode *)binsearch(name, keycodes, N_KEYCODES,
        sizeof(Keycode), cstrstructcmp);
    return !keycode ? NULL : keycode->code ? keycode->code : "";
}

/*******************
 * WINDOW HANDLING *
 *******************/

void setup_screen(clearlines)
    int clearlines;  /* # of lines to scroll input window (-1 == default) */
{
    top_margin = 1;
    bottom_margin = lines;
    screen_mode = visual;
    output_disabled++;

    if (!visual) {
        if (paused) prompt = moreprompt;

#ifdef SCREEN
    } else {
        prompt = fgprompt();
        if (isize > lines - 2) setivar("isize", lines - 2, FALSE);
        ystatus = lines - isize;
        outcount = ystatus - 1;
        if (enter_ca_mode) tp(enter_ca_mode);
    
        if (scroll && (has_scroll_region || (insert_line && delete_line))) {
            xy(1, lines);
            if (clearlines) crnl(clearlines > 0 ? clearlines : isize);
        } else {
            if (clearlines) clr();
            if (scroll) setivar("scroll", 0, FALSE);
        }
        update_status_line();
        ix = iendx = oy = 1;
        iy = iendy = istarty = ystatus + 1;
        ipos();
#endif
    }

    set_refresh_pending(REF_LOGICAL);
    output_disabled--;
}

int redraw()
{
    if (!visual) {
        bufputnc('\n', lines);     /* scroll region impossible */
    } else {
        clr();
    }
    setup_screen(0);
    return 1;
}

int ch_status_fields()
{
    char *name, *s, *t;
    int width, column, varfound = 0;
    STATIC_BUFFER(scratch);
    ListEntry *last = status_field_list->tail;
    StatusField *field;
    ListEntry *node;

    /* validate and insert new fields */
    Stringcpy(scratch, status_fields ? status_fields : "");
    s = scratch->s;
    width = 0;
    while (1) {
        field = NULL;
        name = stringarg(&s, NULL);
        if (!*name) break;
        field = XMALLOC(sizeof(*field));
        field->var = NULL;
        field->attrs = 0;
        field->rightjust = 0;
        field->width = -1;
        field->internal = -1;
        if ((t = strchr(name, ':'))) {
            *t++ = '\0';
            if (isdigit(*t)) {
                field->width = atoi(t);
                if ((field->rightjust = field->width < 0))
                    field->width = -field->width;
                while (isdigit(*t)) t++;
            }
            if (*t == ':') {
                t++;
                if ((field->attrs = parse_attrs(&t)) < 0)
                    goto ch_status_fields_error;
            }
            if (*t) {
                eprintf("Field %s followed by garbage: %s", name, t);
                goto ch_status_fields_error;
            }
        }

        if (*name == '@') {                                /* internal */
            name++;
            field->internal = enum2int(name, enum_status, "status_fields");
            if (field->internal < 0)
                goto ch_status_fields_error;
            field->name = STRDUP(name);
        } else if (is_quote(*name)) {                      /* string literal */
            STATIC_BUFFER(buffer);
            if (!stringliteral(buffer, &name)) {
                eprintf("%S in status_fields", buffer);
                goto ch_status_fields_error;
            }
            field->name = STRDUP(buffer->s);
            if (field->width < 0)
                field->width = strlen(field->name);
        } else if (*name) {                                /* variable */
            field->var = ffindglobalvar(name);
            if (!field->var) {
                eprintf("ignoring nonexistant variable %s.", name);
                continue;
            }
            field->name = STRDUP(name);
        } else {                                           /* blank */
            field->name = NULL;
        }
        inlist(field, status_field_list, status_field_list->tail);

        if (field->width < 0) {
            if (varfound) {
                eprintf("Only one variable width field is allowed.");
                goto ch_status_fields_error;
            }
            varfound++;
        } else {
            width += field->width;
        }
    }
    if (width > columns) {
        eprintf("status width %d would be larger than screen", width);
        goto ch_status_fields_error;
    }

    /* delete old fields and clean up referents */
    for (node = last; node; node = last) {
        last = node->prev;
        field = (StatusField*)unlist(node, status_field_list);
        if (field->var)
            field->var->status = NULL;
        if (field->name)
            FREE(field->name);
        FREE(field);
    }

    /* update new fields */
    for (node = status_field_list->head; node; node = node->next) {
        field = (StatusField*)node->datum;
        if (field->var)
            field->var->status = field;
    }

    clock_update = -1;
    status_left = status_right = 0;
    column = 0;
    for (node = status_field_list->head; node; node = node->next) {
        field = (StatusField*)node->datum;
        status_left = field->column = column;
        if (field->width < 0) break;
        column += field->width;
    }
    column = 0;
    for (node = status_field_list->tail; node; node = node->prev) {
        field = (StatusField*)node->datum;
        if (field->width < 0) break;
        field->column = column - field->width;
        status_right = -(column -= field->width);
    }

    update_status_line();
    return 1;

ch_status_fields_error:
    if (field)
        FREE(field);
    /* delete new fields */
    if (last) {
        for (node = last->next; node; node = last) {
            last = node->next;
            field = (StatusField*)unlist(node, status_field_list);
            if (field->name) FREE(field->name);
            FREE(field);
        }
    }
    return 0;
}

static void format_status_field(field)
    StatusField *field;
{
    STATIC_BUFFER(varname);
    STATIC_BUFFER(scratch);
    CONST char *expression, *old_command;
    int width;

    output_disabled++;
    Stringterm(scratch, 0);
    if (field->internal >= 0 || field->var) {
        Stringcpy(varname, "status_");
        Stringcat(varname, field->var ? "var_" : "int_");
        Stringcat(varname, field->name);
        expression = getnearestvar(varname->s, NULL);
        if (expression) {
            old_command = current_command;
            current_command = varname->s;
            evalexpr(scratch, expression);
            current_command = old_command;
        } else if (field->var) {
            evalexpr(scratch, field->name);
        }
    } else if (field->name) {   /* string literal */
        Stringcpy(scratch, field->name);
    }

    width = (field->width >= 0) ? field->width :
        columns - status_right - status_left;
    if (width > columns - (cx - 1))
        width = columns - (cx - 1);
    if (scratch->len > width)
        Stringterm(scratch, width);

    if (field->rightjust && scratch->len < width) {
        bufputnc('_', width - scratch->len);  cx += width - scratch->len;
    }
    if (field->attrs) attributes_on(field->attrs);
    bufputs(scratch->s);  cx += scratch->len;
    if (field->attrs) attributes_off(field->attrs);
    if (!field->rightjust && scratch->len < width) {
        bufputnc('_', width - scratch->len);  cx += width - scratch->len;
    }

    if (field->internal == STAT_MORE)
        need_more_refresh = 0;
    if (field->internal == STAT_CLOCK) {
        clock_update = time(NULL);
        clock_update += 60 - (localtime(&clock_update))->tm_sec;
    }

    if (field->var && !field->var->status)   /* var was unset */
        field->var = NULL;
    output_disabled--;
}

void update_status_field(var, internal)
    Var *var;
    int internal;
{
    ListEntry *node;
    StatusField *field;

    if (screen_mode < 1) return;

    for (node = status_field_list->head; node; node = node->next) {
        field = (StatusField*)node->datum;
        if (var && field->var != var) continue;
        if (internal >= 0 && field->internal != internal) continue;

        xy((field->column < 0 ? columns : 0) + field->column + 1,  ystatus);
        format_status_field(field);
    }

    bufflush();
    set_refresh_pending(REF_PHYSICAL);
}

void update_status_line()
{
    ListEntry *node;
    StatusField *field;
    int right = 0;

    if (screen_mode < 1) return;

    xy(1, ystatus);

    for (node = status_field_list->head; node; node = node->next) {
        field = (StatusField*)node->datum;

        format_status_field(field);
        if (cx - 1 >= columns) break;

        if (field->width < 0)
            right = 1;
    }

    if (cx - 1 < columns)
        bufputnc('_', columns - (cx - 1));

    bufflush();
    set_refresh_pending(REF_PHYSICAL);
}

/* used by %{visual}, %{isize}, SIGWINCH */
int ch_visual()
{
    if (screen_mode < 0) {  /* e.g., called from init_variables() */
        /* do nothing */
    } else if (visual && (!can_have_visual)) {
        eprintf("Visual mode is not supported on this terminal.");
        setvar("visual", "0", FALSE);
    } else if (visual && (lines < 3 || columns < status_left + status_right)) {
        eprintf("Screen is too small for visual mode.");
        setvar("visual", "0", FALSE);
        /* return 0 would work if called from setvar(), but not SIGWINCH. */
    } else {
        int addlines = isize;
        cx = cy = -1;                       /* in case of resize */
        if (lines != bottom_margin) {       /* input window moved */
            ystatus = lines - isize;
            addlines = 0;
        } else if (ystatus < (lines - isize)) {   /* input window got smaller */
            addlines = 0;
        } else if (ystatus > (lines - isize)) {   /* input window got larger */
            addlines = ystatus - (lines - isize);
        }
        fix_screen();
        setup_screen(addlines > 0 ? addlines : 0);
        transmit_window_size();
    }
    return 1;
}

void fix_screen()
{
    oflush();
    if (screen_mode <= 0) {
        clear_line();
    } else {
        clear_lines(ystatus, lines);
        outcount = lines - 1;
#ifdef SCREEN
        setscroll(1, lines);
        xy(1, ystatus);
        clear_line();
        if (exit_ca_mode) tp(exit_ca_mode);
#endif
    }
    cx = cy = -1;
    bufflush();
    screen_mode = -1;
}

/* panic_fix_screen() replaces outbuf with a local buffer, so it's safe
 * to use even if outbuf was corrupted.
 */
void panic_fix_screen()
{
    smallstr buf;

    tfscreen->u.queue->head = tfscreen->u.queue->tail = NULL;
    outbuf->s = buf;
    outbuf->len = 0;
    outbuf->size = sizeof(buf);
    fix_screen();
}

static void clear_lines(start, end)
    int start, end;
{
    if (start > end) return;
    xy(1, start);
    if (end >= lines && clear_to_eos) {
        tp(clear_to_eos);
    } else {
        clear_line();
        while (start++ < end) {
             bufputc('\n');  cy++;
             clear_line();
        }
    }
}

/* clear entire input window */
static void clear_input_window()
{
    /* only called in visual mode */
    clear_lines(ystatus + 1, lines);
    ix = iendx = 1;
    iy = iendy = istarty = ystatus + 1;
    ipos();
}

/* clear logical input line */
static void clear_input_line()
{
    if (!visual) clear_line();
    else clear_lines(istarty, iendy);
    ix = iendx = 1;
    iy = iendy = istarty;
    if (visual) ipos();
}

/* affects iendx, iendy, istarty.  No effect on ix, iy. */
static void scroll_input(n)
    int n;
{
    if (n > isize) {
        clear_lines(ystatus + 1, lines);
        iendy = ystatus + 1;
    } else if (delete_line) {
        xy(1, ystatus + 1);
        for (iendy = lines + 1; iendy > lines - n + 1; iendy--)
            tp(delete_line);
    } else if (has_scroll_region) {
        setscroll(ystatus + 1, lines);
        xy(1, lines);
        crnl(n);  /* cy += n; */
        iendy = lines - n + 1;
    }
    xy(iendx = 1, iendy);
}


/***********************************************************************
 *                                                                     *
 *                        INPUT WINDOW HANDLING                        *
 *                                                                     *
 ***********************************************************************/

/* ictrl_put
 * display n characters of s, with control characters converted to printable
 * bold reverse (so the physical width is also n).
 */
static void ictrl_put(s, n)
    CONST char *s;
    int n;
{
    int attrflag;
    char c;

    for (attrflag = 0; n > 0; s++, n--) {
        c = unmapchar(localize(*s));
        if (iscntrl(c)) {
            if (!attrflag)
                attributes_on(F_BOLD | F_REVERSE), attrflag = 1;
            bufputc(CTRL(c));
        } else {
            if (attrflag)
                attributes_off(F_BOLD | F_REVERSE), attrflag = 0;
            bufputc(c);
        }
    }
    if (attrflag) attributes_off(F_BOLD | F_REVERSE);
}

/* ioutputs
 * Print string within bounds of input window.  len is the number of
 * characters to print; return value is the number actually printed,
 * which may be less than len if string doesn't fit in the input window.
 * precondition: iendx,iendy and real cursor are at the output position.
 */
static int ioutputs(str, len)
    CONST char *str;
    int len;
{
    int space, written;

    for (written = 0; len > 0; len -= space) {
        if ((space = Wrap - iendx + 1) <= 0) {
            if (!visual || iendy == lines) break;   /* at end of window */
            if (visual) xy(iendx = 1, ++iendy);
            space = Wrap;
        }
        if (space > len) space = len;
        ictrl_put(str, space);  cx += space;
        str += space;
        written += space;
        iendx += space;
    }
    return written;
}

/* ioutall
 * Performs ioutputs() for input buffer starting at kpos.
 * A negative kpos means to display that much of the end of the prompt.
 */
static void ioutall(kpos)
    int kpos;
{
    int ppos;

    if (kpos < 0) {                  /* posible only if there's a prompt */
        kpos = -(-kpos % Wrap);
        ppos = prompt->len + kpos;
        if (ppos < 0) ppos = 0;
        hwrite(prompt, ppos);
        iendx = -kpos + 1;
        kpos = 0;
    }
    if (sockecho)
        ioutputs(keybuf->s + kpos, keybuf->len - kpos);
}

void iput(len)
    int len;
{
    CONST char *s;
    int count, scrolled = 0, oiex = iendx, oiey = iendy;

    s = keybuf->s + keyboard_pos - len;

    if (!sockecho) return;
    if (visual) physical_refresh();

    if (keybuf->len - keyboard_pos > 8 &&     /* faster than redisplay? */
        visual && insert && clear_to_eol &&
        (insert_char || insert_start) &&      /* can insert */
        cy + (cx + len) / Wrap <= lines &&    /* new text will fit in window */
        Wrap - len > 8)                       /* faster than redisplay? */
    {
        /* fast insert */
        iy = iy + (ix - 1 + len) / Wrap;
        ix = (ix - 1 + len) % Wrap + 1;
        iendy = iendy + (iendx - 1 + len) / Wrap;
        iendx = (iendx - 1 + len) % Wrap + 1;
        if (iendy > lines) { iendy = lines; iendx = Wrap + 1; }
        if (cx + len <= Wrap) {
            if (insert_start) tp(insert_start);
            else for (count = len; count; count--) tp(insert_char);
            ictrl_put(s, len);
            s += Wrap - (cx - 1);
            cx += len;
        } else {
            ictrl_put(s, Wrap - (cx - 1));
            s += Wrap - (cx - 1);
            cx = Wrap + 1;
            if (insert_start) tp(insert_start);
        }
        while (s < keybuf->s + keybuf->len) {
            if (Wrap < columns && cx <= Wrap) {
                xy(Wrap + 1, cy);
                tp(clear_to_eol);
            }
            if (cy == lines) break;
            xy(1, cy + 1);
            if (!insert_start)
                for (count = len; count; count--) tp(insert_char);
            ictrl_put(s, len);  cx += len;
            s += Wrap;
        }
        if (insert_start) tp(insert_end);
        ipos();
        bufflush();
        return;
    }

    iendx = ix;
    iendy = iy;

    /* Display as much as possible.  If it doesn't fit, scroll and repeat
     * until the whole string has been displayed.
     */
    while (count = ioutputs(s, len), s += count, (len -= count) > 0) {
        scrolled++;
        if (!visual) {
            crnl(1);  cx = 1;
            iendx = ix = 1;
        } else if (scroll && !clearfull) {
            scroll_input(1);
            if (istarty > ystatus + 1) istarty--;
        } else {
            clear_input_window();
        }
    }
    ix = iendx;
    iy = iendy;

    if (insert || scrolled) {
        /* we must (re)display tail */
        ioutputs(keybuf->s + keyboard_pos, keybuf->len - keyboard_pos);
        if (visual) ipos();
        else { bufputnc('\010', iendx - ix);  cx -= (iendx - ix); }

    } else if ((iendy - oiey) * Wrap + iendx - oiex < 0) {
        /* if we didn't write past old iendx/iendy, restore them */
        iendx = oiex;
        iendy = oiey;
    }

    bufflush();
}

void inewline()
{
    ix = iendx = 1;
    if (!visual) {
        crnl(1);  cx = 1; cy++;
        if (prompt) set_refresh_pending(REF_PHYSICAL);

    } else {
        if (cleardone) {
            clear_input_window();
        } else {
            iy = iendy + 1;
            if (iy > lines) {
                if (scroll && !clearfull) {
                    scroll_input(1);
                    iy--;
                } else {
                    clear_input_window();
                }
            }
        }
        istarty = iendy = iy;
        set_refresh_pending(prompt ? REF_LOGICAL : REF_PHYSICAL);
    }

    bufflush();
}

/* idel() assumes place is in bounds and != keyboard_pos. */
void idel(place)
    int place;
{
    int len;
    int oiey = iendy;

    if ((len = place - keyboard_pos) < 0) keyboard_pos = place;
    if (!sockecho) return;
    if (len < 0) ix += len;
    
    if (!visual) {
        if (ix < 1 || need_refresh) {
            physical_refresh();
            return;
        }
        if (len < 0) { bufputnc('\010', -len);  cx += len; }

    } else {
        /* visual */
        if (ix < 1) {
            iy -= ((-ix) / Wrap) + 1;
            ix = Wrap - ((-ix) % Wrap);
        }
        if (iy <= ystatus) {
            logical_refresh();
            return;
        }
        physical_refresh();
    }

    if (len < 0) len = -len;

    if (visual && delete_char &&
        keybuf->len - keyboard_pos > 3 && len < Wrap/3)
    {
        /* hardware method */
        int i, space, pos;

        iendy = iy;
        if (ix + len <= Wrap) {
            for (i = len; i; i--) tp(delete_char);
            iendx = Wrap + 1 - len;
        } else {
            iendx = ix;
        }
        pos = keyboard_pos - ix + iendx;

        while (pos < keybuf->len) {
            if ((space = Wrap - iendx + 1) <= 0) {
                if (iendy == lines) break;   /* at end of window */
                xy(iendx = 1, ++iendy);
                for (i = len; i; i--) tp(delete_char);
                space = Wrap - len;
                if (space > keybuf->len - pos) space = keybuf->len - pos;
            } else {
                xy(iendx, iendy);
                if (space > keybuf->len - pos) space = keybuf->len - pos;
                ictrl_put(keybuf->s + pos, space);  cx += space;
            }
            iendx += space;
            pos += space;
        }

        /* erase tail */
        if (iendy < oiey) {
            crnl(1); cx = 1; cy++;
            clear_line();
        }

    } else {
        /* redisplay method */
        iendx = ix;
        iendy = iy;
        ioutputs(keybuf->s + keyboard_pos, keybuf->len - keyboard_pos);

        /* erase tail */
        if (len > Wrap - cx + 1) len = Wrap - cx + 1;
        if (visual && clear_to_eos && (len > 2 || cy < oiey)) {
            tp(clear_to_eos);
        } else if (clear_to_eol && len > 2) {
            tp(clear_to_eol);
            if (visual && cy < oiey) clear_lines(cy + 1, oiey);
        } else {
            bufputnc(' ', len);  cx += len;
            if (visual && cy < oiey) clear_lines(cy + 1, oiey);
        }
    }
    
    /* restore cursor */
    if (visual) ipos();
    else { bufputnc('\010', cx - ix);  cx = ix; }

    bufflush();
}

int igoto(place)
    int place;
{
    int diff, new;

    if (place < 0) {
        place = 0;
        bell(1);
    }
    if (place > keybuf->len) {
        place = keybuf->len;
        bell(1);
    }
    diff = place - keyboard_pos;
    keyboard_pos = place;

    if (!sockecho || !diff) {
        /* no physical change */

    } else if (!visual) {
        ix += diff;
        if (ix < 1) {
            physical_refresh();
        } else if (ix > Wrap) {
            crnl(1);  cx = 1;  /* old text scrolls up, for continutity */
            physical_refresh();
        } else {
            cx += diff;
            if (diff < 0)
                bufputnc('\010', -diff);
            else 
                ictrl_put(keybuf->s + place - diff, diff);
        }

    /* visual */
    } else {
        new = (ix - 1) + diff;
        iy += ndiv(new, Wrap);
        ix = nmod(new, Wrap) + 1;

        if ((iy > lines) && (iy - lines < isize) && scroll) {
            scroll_input(iy - lines);
            ioutall(place - (ix - 1) - (iy - lines - 1) * Wrap);
            iy = lines;
            ipos();
        } else if ((iy < ystatus + 1) || (iy > lines)) {
            logical_refresh();
        } else {
            ipos();
        }
    }

    bufflush();
    return keyboard_pos;
}

void do_refresh()
{
    if (visual && need_more_refresh) update_status_field(NULL, STAT_MORE);
    if (need_refresh >= REF_LOGICAL) logical_refresh();
    else if (need_refresh >= REF_PHYSICAL) physical_refresh();
}

void physical_refresh()
{
    if (visual) {
        setscroll(1, lines);
        ipos();
    } else {
        clear_input_line();
        ix = ((prompt?prompt->len:0) + (sockecho?keyboard_pos:0)) % Wrap + 1;
        ioutall((sockecho?keyboard_pos:0) - (ix - 1));
        bufputnc('\010', iendx - ix);  cx -= (iendx - ix);
    }
    bufflush();
    if (need_refresh <= REF_PHYSICAL) need_refresh = 0;
}

void logical_refresh()
{
    int kpos, nix, niy;

    if (!visual)
        oflush();  /* no sense refreshing if there's going to be output after */

    kpos = prompt ? -(prompt->len % Wrap) : 0;
    nix = ((sockecho ? keyboard_pos : 0) - kpos) % Wrap + 1;

    if (visual) {
        setscroll(1, lines);
        niy = istarty + (keyboard_pos - kpos) / Wrap;
        if (niy <= lines) {
            clear_input_line();
        } else {
            clear_input_window();
            kpos += (niy - lines) * Wrap;
            niy = lines;
        }
        ioutall(kpos);
        ix = nix;
        iy = niy;
        ipos();
    } else {
        clear_input_line();
        ioutall(kpos);
        kpos += Wrap;
        while ((sockecho && kpos <= keyboard_pos) || kpos < 0) {
            crnl(1);  cx = 1;
            iendx = 1;
            ioutall(kpos);
            kpos += Wrap;
        }
        ix = nix;
        bufputnc('\010', iendx - nix);  cx -= (iendx - nix);
    }
    bufflush();
    if (need_refresh <= REF_LOGICAL) need_refresh = 0;
}

void update_prompt(newprompt)
    Aline *newprompt;
{
    Aline *oldprompt = prompt;

    if (oldprompt == moreprompt) return;
    prompt = newprompt;
    if (oldprompt || prompt)
        set_refresh_pending(REF_LOGICAL);
}


/*****************************************************
 *                                                   *
 *                  OUTPUT HANDLING                  *
 *                                                   *
 *****************************************************/

/*************
 * Utilities *
 *************/

static void attributes_off(attrs)
    attr_t attrs;
{
    CONST char *cmd;

    if (attrs & F_HILITE) attrs |= hiliteattr;
    if (have_attr & attrs & F_SIMPLE) {
        if (attr_off) tp(attr_off);
        else {
            if (have_attr & attrs & F_UNDERLINE) tp(underline_off);
            if (have_attr & attrs & F_BOLD     ) tp(standout_off);
        }
    }
    if ((attrs & F_COLORS) && (cmd = getvar("end_color"))) {
        bufputs(print_to_ascii(cmd));
    }
}

static void attributes_on(attrs)
    attr_t attrs;
{
    if (attrs & F_HILITE)
        attrs |= hiliteattr;

    /* Some emulators only show the last, so we put the most important last. */
    if (have_attr & attrs & F_DIM)       tp(dim);
    if (have_attr & attrs & F_BOLD)      tp(bold ? bold : standout);
    if (have_attr & attrs & F_UNDERLINE) tp(underline);
    if (have_attr & attrs & F_REVERSE)   tp(reverse);
    if (have_attr & attrs & F_FLASH)     tp(flash);

    if (attrs & F_FGCOLOR)  color_on(attr2fgcolor(attrs));
    if (attrs & F_BGCOLOR)  color_on(attr2bgcolor(attrs));
}

static void color_on(color)
    long color;
{
    CONST char *cmd;
    smallstr buf;

    sprintf(buf, "start_color_%s", enum_color[color]);
    if ((cmd = getvar(buf))) {
        bufputs(print_to_ascii(cmd));
    } else {
        sprintf(buf, "start_color_%ld", color);
        if ((cmd = getvar(buf))) {
            bufputs(print_to_ascii(cmd));
        }
    }
}

static void hwrite(line, offset)
    Aline *line;
    int offset;
{
    attr_t attrs = line->attrs & F_HWRITE;
    attr_t current = 0;
    attr_t new;
    int i, ctrl;
    int col = 0;
    char c;

    if (line->attrs & F_BELL) {
        bell(1);
    }

    if (line->attrs & F_INDENT) {
        bufputnc(' ', wrapspace);
        cx += wrapspace;
    }

    cx += line->len - offset;

    if (!line->partials && hilite && attrs)
        attributes_on(current = attrs);

    for (i = offset; i < line->len; ++i) {
        new = attrs;
        if (line->partials) {
            /* turn off previous color bits before setting new ones */
            if (new & line->partials[i] & F_FGCOLOR)
                new &= ~F_FGCOLORMASK;
            if (new & line->partials[i] & F_BGCOLOR)
                new &= ~F_BGCOLORMASK;
            new |= line->partials[i];
        }
        c = unmapchar(localize(line->str[i]));
        ctrl = (emulation != EMUL_RAW && iscntrl(c));
        if (ctrl)
            new |= F_BOLD | F_REVERSE;
        if (new != current) {
            if (current) attributes_off(current);
            current = new;
            if (current) attributes_on(current);
        }
        if (c == '\t') {
            bufputnc(' ', tabsize - col % tabsize);
            col += tabsize - col % tabsize;
        } else {
            bufputc(ctrl ? CTRL(c) : c);
            col++;
        }
    }
    if (current) attributes_off(current);
}

void reset_outcount()
{
    outcount = visual ? (scroll ? (ystatus - 1) : outcount) : lines - 1;
}

/* return TRUE if okay to print */
static int check_more()
{
    if (!paused && more && !no_tty && outcount-- <= 0) {
        /* status bar is updated in oflush() to avoid scroll region problems */
        paused = 1;
        do_hook(H_MORE, NULL, "");
    }
    return !paused;
}

static int clear_more(new)
    int new;
{
    if (!paused) return 0;
    paused = 0;
    outcount = new;
    if (visual) {
        update_status_field(NULL, STAT_MORE);
        if (!scroll) outcount = ystatus - 1;
    } else {
        prompt = fgprompt();
        clear_input_line();
    }
    set_refresh_pending(REF_PHYSICAL);
    return 1;
}

int tog_more()
{
    if (!more) clear_more(outcount);
    else reset_outcount();
    return 1;
}

int dokey_page()
{
    return clear_more((visual ? ystatus : lines) - 1);
}

int dokey_hpage() 
{
    return clear_more(((visual ? ystatus : lines) - 1) / 2);
}

int dokey_line()
{
    return clear_more(1);
}

int screen_flush(selective)
    int selective;
{
    ListEntry *node, *next;

#define interesting(alin)  ((alin)->attrs & F_SIMPLE || (alin)->partials)

    if (!paused) return 0;
    outcount = visual ? ystatus - 1 : lines - 1;
    for (node = tfscreen->u.queue->head; node; node = next) {
        next = node->next;
        if (!selective || !interesting((Aline*)node->datum)) {
            free_aline((Aline*)unlist(node, tfscreen->u.queue));
            tfscreen_size--;
        }
    }
    if (currentline && (!selective || !interesting(currentline))) {
        free_aline(currentline);
        currentline = NULL;
        wrap_offset = 0;
    }
    clear_more(outcount);
    screenout(new_aline("--- Output discarded ---", 0));
    return 1;
}

/* wrapline
 * Return a pointer to a static Aline containing the next physical line
 * to be printed.
 */
static Aline *wrapline()
{
    int remaining;
    static Aline *dead = NULL;
    static Aline result;
    /* result is not a "normal" Aline: it's static, its fields are actually
     * pointers into the fields of another Aline, and result.str[result.len]
     * is not neccessarily '\0'.
     */

    if (dead) { free_aline(dead); dead = NULL; }

    while (!currentline || ((currentline->attrs & F_GAG) && gag)) {
        if (currentline) free_aline(currentline);
        if (!(currentline = dequeue(tfscreen->u.queue))) return NULL;
        tfscreen_size--;
    }
    if (!check_more()) return NULL;

    remaining = currentline->len - wrap_offset;
    result.str = currentline->str + wrap_offset;
    result.attrs = currentline->attrs;
    result.partials = currentline->partials;
    if (result.partials) result.partials += wrap_offset;
    if (wrap_offset) {
        result.attrs &= ~F_BELL;
        if (wrapflag && wrapspace < Wrap)
            result.attrs |= F_INDENT;
    }
    result.len = wraplen(result.str, remaining, wrapflag && wrap_offset);
    wrap_offset += result.len;

    if (wrap_offset == currentline->len) {
        dead = currentline;   /* remember so we can free it next time */
        currentline = NULL;
        wrap_offset = 0;
    }
    return &result;
}

/* returns length of prefix of str that will fit in {wrapsize} */
int wraplen(str, len, indent)
    CONST char *str;
    int len;
    int indent; /* flag */
{
    int total, max;

    if (emulation == EMUL_RAW) return len;

    max = (indent && wrapspace < Wrap) ? (Wrap - wrapspace) : Wrap;

#if 0
    /* Don't count nonprinting chars or "ansi" display codes (anything
     * starting with ESC and ending with a letter, for our purposes).
     */
    for (visible = total = 0; total < len && visible < max; total++) {
        if (incode) {
            if (isalpha(str[total])) incode = FALSE;
        } else {
            if (isprint(str[total]))
                visible++;
            incode = (str[total] == '\33');
        }
    }
#else
    /* Nonprinting characters from server were already stripped; others will be
     * displayed in bold-reverse notation, so should be counted.
     */
    total = (len < max) ? len : max;
#endif
    if (total == len) return len;
    len = total;
    if (wrapflag)
        while (len && !isspace(str[len-1])) --len;
    return len ? len : total;
}


/****************
 * Main drivers *
 ****************/

/* write to tfscreen (no history) */
void screenout(aline)
    Aline *aline;
{
    if (!hilite)
        aline->attrs &= ~F_HWRITE;
    if (aline->attrs & F_GAG && gag)
        return;
    if (!output_disabled && !currentline && !tfscreen->u.queue->head) {
        /* shortcut if screen queue is empty (a common case) */
        (currentline = aline)->links++;
    } else {
        aline->links++;
        enqueue(tfscreen->u.queue, aline);
        tfscreen_size++;
        oflush();
    }
}

void oflush()
{
    static int lastsize;
    int waspaused;

    if (output_disabled) return;

    if (!(waspaused = paused)) {
        lastsize = 0;
        if (screen_mode < 1) output_novisual();
#ifdef SCREEN
        else if (scroll) output_scroll();
        else output_noscroll();
#endif
    }

    if (paused) {
        if (!visual) {
            if (!waspaused) {
                prompt = moreprompt;
                set_refresh_pending(REF_LOGICAL);
            }
        } else if (moresize / morewait > lastsize / morewait) {
            update_status_field(NULL, STAT_MORE);
        } else if (lastsize != moresize) {
            need_more_refresh = 1;
        }
        lastsize = moresize;
    }
}

static void output_novisual()
{
    Aline *line;
    int count = 0;

    while ((line = wrapline()) != NULL) {
        if (count == 0 && (keybuf->len || ix > 1)) {
            clear_input_line();
            set_refresh_pending(REF_PHYSICAL);
        }
        count++;
        hwrite(line, 0);
        crnl(1);  cx = 1; cy++;
        bufflush();
    }
}

#ifdef SCREEN
static void output_noscroll()
{
    Aline *line;

    while ((line = wrapline()) != NULL) {
        setscroll(1, lines);   /* needed after scroll_input(), etc. */
        xy(1, (oy + 1) % (ystatus - 1) + 1);
        clear_line();
        xy(ox, oy);
        set_refresh_pending(REF_PHYSICAL);
        hwrite(line, 0);
        oy = oy % (ystatus - 1) + 1;
        bufflush();
    }
}

static void output_scroll()
{
    Aline *line;

    while ((line = wrapline()) != NULL) {
        if (has_scroll_region) {
            setscroll(1, ystatus - 1);
            if (cy != ystatus - 1) xy(columns, ystatus - 1);
            crnl(1);
            /* Some brain damaged emulators lose attributes under cursor
             * when that '\n' is printed.  Too bad. */
        } else {
            xy(1, 1);
            tp(delete_line);
            xy(1, ystatus - 1);
            tp(insert_line);
        }
        hwrite(line, 0);
        set_refresh_pending(REF_PHYSICAL);
        bufflush();
    }
}
#endif

/***********************************
 * Interfaces with rest of program *
 ***********************************/

int ch_hilite()
{
    CONST char *str;
    attr_t attrs;

    if (!(str = getstrvar(VAR_hiliteattr))) {
        intvar(VAR_hiliteattr) = 0;
        return 1;
    } else if ((attrs = parse_attrs((char **)&str)) >= 0) {
        intvar(VAR_hiliteattr) = attrs;
        return 1;
    } else {
        return 0;
    }
}

static void set_attr(aline, dest, starting, current)
    Aline *aline;
    char *dest;
    attr_t *starting, *current;
{
    int i;
    /* starting_attrs is set by the attrs parameter and/or
     * codes at the beginning of the line.  If no visible mid-line
     * changes occur, there is no need to allocate aline->partials
     * (which would nearly triple the size of the aline).
     */
    if (dest == aline->str) {
        /* start of visible line */
        *starting = *current;
    } else if (*starting != *current && !aline->partials) {
        /* first mid-line attr change */
        aline->partials = (short*)XMALLOC(sizeof(short)*aline->len);
        for (i = 0; i < dest - aline->str; ++i)
            aline->partials[i] = *starting;
    }
    if (aline->partials)
        aline->partials[dest - aline->str] = *current;
}

#define ANSI_CSI        (char)0233    /* ANSI terminal Command Sequence Intro */

/* Interpret embedded codes from a subset of ansi codes:
 * ansi attribute/color codes are converted to tf partial or line attrs;
 * all other codes are ignored.
 * (tabs and backspaces were handled in handle_socket_input())
 */
attr_t handle_ansi_attr(aline, attrs)
    Aline *aline;
    attr_t attrs;
{
    char *s, *t;
    int i;
    attr_t new;
    attr_t starting_attrs = attrs;

    for (s = t = aline->str; *s; s++) {
        if (*s == ANSI_CSI || (s[0] == '\033' && s[1] == '[' && s++)) {
            new = attrs;
            do {
                s++;
                i = strtoint(&s);
                if (!i) new = 0;
                else if (i >= 30 && i <= 37)
                    new = (new & ~F_FGCOLORMASK) | color2attr(i - 30);
                else if (i >= 40 && i <= 47)
                    new = (new & ~F_BGCOLORMASK) | color2attr(i - 40 + 16);
                else switch (i) {
                    case 1:   new |= F_BOLD;        break;
                    case 4:   new |= F_UNDERLINE;   break;
                    case 5:   new |= F_FLASH;       break;
                    case 7:   new |= F_REVERSE;     break;
                    case 22:  new &= ~F_BOLD;       break;
                    case 24:  new &= ~F_UNDERLINE;  break;
                    case 25:  new &= ~F_FLASH;      break;
                    case 27:  new &= ~F_REVERSE;    break;
                    default:  /* ignore it */       break;
                }
            } while (s[0] == ';' && s[1]);

            if (*s == 'm') {           /* attribute command */
                attrs = new;
            } /* ignore any other CSI command */

        } else if (isprint(*s)) {
            set_attr(aline, t, &starting_attrs, &attrs);
            *t++ = *s;

        } else if (*s == '\07') {
            aline->attrs |= F_BELL;
        }
    }

    *t = '\0';
    aline->len = t - aline->str;
    if (!aline->partials) {
        /* No mid-line changes, so apply starting_attrs to entire line */
        aline->attrs |= starting_attrs;
    }

    return attrs;
}

/* Convert embedded '@' codes to internal partial or line attrs. */
attr_t handle_inline_attr(aline, attrs)
    Aline *aline;
    attr_t attrs;
{
    char *s, *t, *end;
    int off;
    attr_t new;
    attr_t starting_attrs = attrs;

    for (s = t = aline->str; *s; s++) {
        if (s[0] == '@' && s[1] == '{') {
            s+=2;
            if ((off = (*s == '~'))) s++;
            end = strchr(s, '}');
            if (!end) {
                eprintf("unmatched @{");
                return -1;
            }
            *end = '\0';
            new = parse_attrs(&s);
            *end = '}';
            if (new < 0) return new;
            s = end;
            if (new & F_FGCOLOR) attrs &= ~F_FGCOLORMASK;
            if (new & F_BGCOLOR) attrs &= ~F_BGCOLORMASK;
            if (new == F_NORM) attrs &= ~F_HWRITE;
            else if (off) attrs &= ~new;
            else attrs |= new;
            if (new & F_BELL) aline->attrs |= F_BELL;

        } else if (isprint(*s)) {
            set_attr(aline, t, &starting_attrs, &attrs);
            if (s[0] == '@' && s[1] == '@')
                s++;
            *t++ = *s;
        }
    }

    *t = '\0';
    aline->len = t - aline->str;
    if (!aline->partials) {
        /* No mid-line changes, so apply starting_attrs to entire line */
        aline->attrs |= starting_attrs;
    }

    return attrs;
}

#ifdef DMALLOC
void free_output()
{
    StatusField *f;

    tfclose(tfscreen);
    Stringfree(outbuf);
    free_aline(moreprompt);
    while (status_field_list->head) {
        f = (StatusField*)unlist(status_field_list->head, status_field_list);
        FREE(f->name);
        FREE(f);
    }
}
#endif

