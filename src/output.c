/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.c,v 35004.87 1999/01/31 00:27:49 hawkeye Exp $ */


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
#include "expand.h"	/* current_command */
#include "parse.h"	/* expr_value_safe() */
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
#else /* !HARDCODE */
# define origin 0      /* top left corner is (0,0) */
# define TERMCODE(id, vt100, vt220, ansi)   static CONST char *(id) = NULL;
#endif /* HARDCODE */

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
TERMCODE (keypad_on,		"\033[?1h",	NULL,		NULL)
TERMCODE (keypad_off,		"\033[?1l",	NULL,		NULL)
TERMCODE (bell,			"\007",		"\007",		"\007")
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

#define true_status_pad (status_pad && *status_pad ? *status_pad : ' ')

static void  NDECL(init_term);
static int   FDECL(fbufputc,(int c));
static void  NDECL(bufflush);
static void  FDECL(tbufputs,(CONST char *str));
static void  FDECL(tdirectputs,(CONST char *str));
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
static void  FDECL(format_status_field,(StatusField *field, attr_t *attrp));
static void  FDECL(attributes_off,(attr_t attrs));
static void  FDECL(attributes_on,(attr_t attrs));
static void  FDECL(color_on,(long color));
static void  FDECL(hwrite,(Aline *line, int start, int end));
static int   FDECL(set_attr_var,(int idx, attr_t *attrp));
static void  FDECL(set_attr,(Aline *aline, char *dest, attr_t *starting,
             attr_t *current));
static int   NDECL(check_more);
static Aline *NDECL(wrapline);
static void  NDECL(output_novisual);
#ifdef SCREEN
static void  NDECL(output_noscroll);
static void  NDECL(output_scroll);
#endif

static void  FDECL((*tp),(CONST char *str));

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
static attr_t hiliteattr = 0;
static attr_t status_attr = 0;

#ifndef EMXANSI
# define has_scroll_region (set_scroll_region != NULL)
#else
# define has_scroll_region (1)
#endif


#ifdef HARDCODE
# if HARDCODE == TERM_vt100
#  define KEYCODE(vt100, vt220, ansi)   (vt100)
# else
#  if HARDCODE == TERM_vt220
#   define KEYCODE(vt100, vt220, ansi)   (vt220)
#  else
#   if HARDCODE == TERM_ansi
#    define KEYCODE(vt100, vt220, ansi)   (ansi)
#   endif
#  endif
# endif
#else
# define KEYCODE(vt100, vt220, ansi)   NULL
#endif

typedef struct Keycode {
    CONST char *name, *capname, *code;
} Keycode;

static Keycode keycodes[] = {  /* this list is sorted by tolower(name)! */
/*                                     vt100       vt220       ansi      */
/*                                     -----       -----       ----      */
/*  { "Back Tab",       "kB", KEYCODE( NULL,       NULL,       NULL ) }, */
    { "Backspace",      "kb", KEYCODE( "\010",     "\010",     "\010" ) },
/*  { "Clear All Tabs", "ka", KEYCODE( NULL,       NULL,       NULL ) }, */
    { "Clear EOL",      "kE", KEYCODE( NULL,       NULL,       NULL ) },
    { "Clear EOS",      "kS", KEYCODE( NULL,       NULL,       NULL ) },
    { "Clear Screen",   "kC", KEYCODE( NULL,       NULL,       NULL ) },
/*  { "Clear Tab",      "kt", KEYCODE( NULL,       NULL,       NULL ) }, */
    { "Delete",         "kD", KEYCODE( NULL,       "\033[3~",  NULL ) },
    { "Delete Line",    "kL", KEYCODE( NULL,       NULL,       NULL ) },
    { "Down",           "kd", KEYCODE( "\033OB",   "\033[B",   "\033[B" ) },
/*  { "End Insert",     "kM", KEYCODE( NULL,       NULL,       NULL ) }, */
    { "F0",             "k0", KEYCODE( "\033Oy",   NULL,       NULL ) },
    { "F1",             "k1", KEYCODE( "\033OP",   "\033OP",   "\033[M" ) },
    { "F10",            "k;", KEYCODE( NULL,       NULL,       NULL ) },
    { "F11",            "F1", KEYCODE( NULL,       NULL,       NULL ) },
    { "F12",            "F2", KEYCODE( NULL,       NULL,       NULL ) },
    { "F13",            "F3", KEYCODE( NULL,       NULL,       NULL ) },
    { "F14",            "F4", KEYCODE( NULL,       NULL,       NULL ) },
    { "F15",            "F5", KEYCODE( NULL,       NULL,       NULL ) },
    { "F16",            "F6", KEYCODE( NULL,       NULL,       NULL ) },
    { "F17",            "F7", KEYCODE( NULL,       NULL,       NULL ) },
    { "F18",            "F8", KEYCODE( NULL,       NULL,       NULL ) },
    { "F19",            "F9", KEYCODE( NULL,       NULL,       NULL ) },
    { "F2",             "k2", KEYCODE( "\033OQ",   "\033OQ",   "\033[N" ) },
    { "F3",             "k3", KEYCODE( "\033OR",   "\033OR",   "\033[O" ) },
    { "F4",             "k4", KEYCODE( "\033OS",   "\033OS",   "\033[P" ) },
    { "F5",             "k5", KEYCODE( "\033Ot",   "\033[17~", "\033[Q" ) },
    { "F6",             "k6", KEYCODE( "\033Ou",   "\033[18~", "\033[R" ) },
    { "F7",             "k7", KEYCODE( "\033Ov",   "\033[19~", "\033[S" ) },
    { "F8",             "k8", KEYCODE( "\033Ol",   "\033[20~", "\033[T" ) },
    { "F9",             "k9", KEYCODE( "\033Ow",   "\033[21~", "\033[U" ) },
    { "Home",           "kh", KEYCODE( NULL,       "\033[H",   "\033[H" ) },
    { "Home Down",      "kH", KEYCODE( NULL,       NULL,       NULL ) },
    { "Insert",         "kI", KEYCODE( NULL,       "\033[2~",  "\033[L" ) },
    { "Insert Line",    "kA", KEYCODE( NULL,       NULL,       NULL ) },
    { "KP1",            "K1", KEYCODE( "\033Oq",   NULL,       NULL ) },
    { "KP2",            "K2", KEYCODE( "\033Or",   NULL,       NULL ) },
    { "KP3",            "K3", KEYCODE( "\033Os",   NULL,       NULL ) },
    { "KP4",            "K4", KEYCODE( "\033Oq",   NULL,       NULL ) },
    { "KP5",            "K5", KEYCODE( "\033Op",   NULL,       NULL ) },
    { "Left",           "kl", KEYCODE( "\033On",   "\033[D",   "\033[D" ) },
    { "PgDn",           "kN", KEYCODE( NULL,       "\033[6~",  NULL ) },
    { "PgUp",           "kP", KEYCODE( NULL,       "\033[5~",  NULL ) },
    { "Right",          "kr", KEYCODE( "\033OC",   "\033[C",   "\033[C" ) },
    { "Scroll Down",    "kF", KEYCODE( NULL,       NULL,       NULL ) },
    { "Scroll Up",      "kR", KEYCODE( NULL,       NULL,       NULL ) },
/*  { "Set Tab",        "kT", KEYCODE( NULL,       NULL,       NULL ) }, */
    { "Up",             "ku", KEYCODE( "\033OA",   "\033[A",   "\033[A" ) }
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

void dobell(n)
    int n;
{
    if (beep) {
        while (n--) bufputs(bell);
        bufflush();
    }
}

/********************/

int change_term()
{
    fix_screen();
    init_term();
    setup_screen(0);
    rebind_key_macros();
    return 1;
}

/* Initialize output data. */
void init_output()
{
    CONST char *str;

    tp = tbufputs;
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
    ch_hiliteattr();
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
    keypad_on = keypad_off = NULL;
    standout = underline = reverse = flash = dim = bold = bell = NULL;
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
        keypad_on            = tgetstr("ks", &area);
        keypad_off           = tgetstr("ke", &area);

        bell		= tgetstr("bl", &area);
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
            fprintf(stderr, "(%2s) %-12s = %s\n",
                keycodes[i].capname, keycodes[i].name,
                keycodes[i].code ? ascii_to_print(keycodes[i].code) : "NULL");
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

    if (!bell) bell = "\007";

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
    set_var_by_id(VAR_wrapsize, columns - 1, NULL);
    outcount = lines;
    ix = 1;
    can_have_visual = (clear_screen || clear_to_eol) && cursor_address;
    set_var_by_id(VAR_scroll,
        has_scroll_region||(insert_line&&delete_line), NULL);
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

#ifdef TERMCAP
static int fputchar(c)   /* in case broken lib has a macro but no function */
    int c;
{
    return putchar(c);
}
#endif

static void tdirectputs(str)
    CONST char *str;
{
    if (str)
#ifdef TERMCAP
        tputs(str, 1, fputchar);
#else
        puts(str);
#endif
}

static void tbufputs(str)
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

void setup_screen(addlines)
    int addlines;  /* # of lines to scroll (-1 == default) */
{
    top_margin = 1;
    bottom_margin = lines;
    if (addlines < 0) addlines = isize;
    output_disabled++;

    if (visual && (!can_have_visual)) {
        eprintf("Visual mode is not supported on this terminal.");
        set_var_by_id(VAR_visual, 0, NULL);
    } else if (visual && (lines < 3 || columns < status_left + status_right)) {
        eprintf("Screen is too small for visual mode.");
        set_var_by_id(VAR_visual, 0, NULL);
    }
    screen_mode = visual;

    if (!visual) {
        if (paused) prompt = moreprompt;

#ifdef SCREEN
    } else {
        prompt = fgprompt();
        if (isize > lines - 2) set_var_by_id(VAR_isize, lines - 2, NULL);
        ystatus = lines - isize;
        outcount = ystatus - 1;
        if (enter_ca_mode) tp(enter_ca_mode);
    
        if (scroll && (has_scroll_region || (insert_line && delete_line))) {
            if (addlines) {
                xy(1, lines);
                crnl(addlines);
            }
        } else {
            if (addlines) clr();
            if (scroll) set_var_by_id(VAR_scroll, 0, NULL);
        }
        update_status_line();
        ix = iendx = oy = 1;
        iy = iendy = istarty = ystatus + 1;
        ipos();
#endif
    }

    if (keypad_on) tp(keypad_on);

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
    char *s, *t, save;
    int width, column, i = 0, varfound = 0;
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
        while(is_space(*s)) s++;
        if (!*s) break;
        field = XMALLOC(sizeof(*field));
        field->name = NULL;
        field->var = NULL;
        field->attrs = 0;
        field->rightjust = 0;
        field->width = 0;
        field->internal = -1;
        i++;

        if (is_quote(*s)) {                                /* string literal */
            STATIC_BUFFER(buffer);
            if (!stringliteral(buffer, &s)) {
                eprintf("%S in status_fields", buffer);
                goto ch_status_fields_error;
            }
            field->name = STRDUP(buffer->s);
        } else if (*s == '@') {                            /* internal */
            for (t = ++s; is_alnum(*s) || *s == '_'; s++);
            save = *s;
            *s = '\0';
            field->internal = enum2int(t, enum_status,
                "status_fields internal status");
            if (field->internal < 0)
                goto ch_status_fields_error;
            field->name = STRDUP(t);
            *s = save;
        } else if (is_alnum(*s) || *s == '_') {             /* variable */
            for (t = s++; is_alnum(*s) || *s == '_'; s++);
            save = *s;
            *s = '\0';
            field->name = STRDUP(t);
            *s = save;
            field->var = ffindglobalvar(field->name);
            if (!field->var) {
                eprintf("variable '%s' is not defined.", field->name);
                goto ch_status_fields_error;
            }
        } else {                                           /* blank */
            field->name = NULL;
        }

        if (*s == ':') {
            s++;
            field->width = strtoint(&s);
            if ((field->rightjust = field->width < 0))
                field->width = -field->width;
        }

        if (*s == ':') {
            for (t = s + 1; *s && !is_space(*s); s++);
            save = *s;
            *s = '\0';
            field->attrs = parse_attrs(&t);
            *s = save;
            if (field->attrs < 0)
                goto ch_status_fields_error;
        }

        if (*s && !is_space(*s)) {
            eprintf("status_fields: garbage in field %d: %.8s", i, s);
            goto ch_status_fields_error;
        }

        if (!field->width && !field->var && field->internal < 0 && field->name)
            field->width = strlen(field->name);

        if (field->width == 0) {
            if (varfound) {
                eprintf("Only one variable width field is allowed.");
                goto ch_status_fields_error;
            }
            varfound++;
        } else {
            width += field->width;
        }

        inlist(field, status_field_list, status_field_list->tail);
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
        if (field->width == 0) break;
        column += field->width;
    }
    column = 0;
    if (node) {
        for (node = status_field_list->tail; node; node = node->prev) {
            field = (StatusField*)node->datum;
            if (field->width == 0) break;
            status_right = -(field->column = (column -= field->width));
        }
    }

    update_status_line();
    return 1;

ch_status_fields_error:
    if (field) {
        if (field->name) FREE(field->name);
        FREE(field);
    }
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

static void format_status_field(field, attrp)
    StatusField *field;
    attr_t *attrp;
{
    STATIC_BUFFER(varname);
    STATIC_BUFFER(scratch);
    CONST char *expression, *old_command;
    Value *val;
    int width;

    output_disabled++;
    Stringterm(scratch, 0);
    if (field->internal >= 0 || field->var) {
        Stringcpy(varname, "status_");
        Stringcat(varname, field->var ? "var_" : "int_");
        Stringcat(varname, field->name);
        expression = getnearestvar(varname->s, NULL);
        old_command = current_command;
        if (expression) {
            current_command = varname->s;
        } else if (field->var) {
            current_command = field->name;
            expression = field->name;
        }
        val = expression ? expr_value_safe(expression) : NULL;
        Stringcpy(scratch, valstr(val));
        freeval(val);
        current_command = old_command;
    } else if (field->name) {   /* string literal */
        Stringcpy(scratch, field->name);
    }

    width = (field->width > 0) ? field->width :
        columns - status_right - status_left;
    if (width > columns - (cx - 1))
        width = columns - (cx - 1);
    if (scratch->len > width)
        Stringterm(scratch, width);

    if (field->rightjust && scratch->len < width) {          /* left pad */
        if (*attrp != status_attr) {
            if (*attrp) attributes_off(*attrp);
            if (status_attr) attributes_on(status_attr);
            *attrp = status_attr;
        }
        bufputnc(true_status_pad, width - scratch->len);
        cx += width - scratch->len;
    }

    if (scratch->len) {                                      /* value */
        if ((field->attrs | status_attr) != *attrp) {
            if (*attrp) attributes_off(*attrp);
            if ((field->attrs | status_attr))
                attributes_on(field->attrs | status_attr);
            *attrp = field->attrs | status_attr;
        }
        bufputs(scratch->s);  cx += scratch->len;
    }

    if (!field->rightjust && scratch->len < width) {         /* right pad */
        if (*attrp != status_attr) {
            if (*attrp) attributes_off(*attrp);
            if (status_attr) attributes_on(status_attr);
            *attrp = status_attr;
        }
        bufputnc(true_status_pad, width - scratch->len);
        cx += width - scratch->len;
    }

    if (field->internal == STAT_MORE)
        need_more_refresh = 0;
    if (field->internal == STAT_CLOCK) {
        struct tm *local;
        clock_update = time(NULL);
        /* note: localtime()->tm_sec won't compile if prototype is missing */
        local = localtime(&clock_update);
        clock_update += 60 - local->tm_sec;
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
    attr_t attrs;

    if (screen_mode < 1) return;

    for (node = status_field_list->head; node; node = node->next) {
        field = (StatusField*)node->datum;
        if (var && field->var != var) continue;
        if (internal >= 0 && field->internal != internal) continue;

        xy((field->column < 0 ? columns : 0) + field->column + 1,  ystatus);
        attrs = 0;
        format_status_field(field, &attrs);
        if (attrs) attributes_off(attrs);
    }

    bufflush();
    set_refresh_pending(REF_PHYSICAL);
}

int update_status_line()
{
    ListEntry *node;
    StatusField *field;
    int right = 0;
    attr_t attrs = 0;

    if (screen_mode < 1) return 1;

    xy(1, ystatus);

    for (node = status_field_list->head; node; node = node->next) {
        field = (StatusField*)node->datum;

        format_status_field(field, &attrs);
        if (cx - 1 >= columns) break;

        if (field->width == 0)
            right = 1;
    }

    if (cx - 1 < columns) {
        if (attrs != status_attr) {
            if (attrs) attributes_off(attrs);
            if (status_attr) attributes_on(status_attr);
        }
        bufputnc(true_status_pad, columns - (cx - 1));
    }
    if (attrs) attributes_off(attrs);

    bufflush();
    set_refresh_pending(REF_PHYSICAL);
    return 1;
}

/* used by %{visual}, %{isize}, SIGWINCH */
int ch_visual()
{
    static int old_isize = 0;
    int addlines;

    if (screen_mode < 0) {                /* e.g., called by init_variables() */
        addlines = -1;
    } else if (visual != screen_mode) {   /* %visual changed */
        addlines = isize;
    } else if (!visual) {                 /* other changes have no effect */
        addlines = -1;
#ifdef SCREEN
    } else if (isize != old_isize) {      /* %isize changed */
        if (ystatus > (lines - isize))        /* larger */
            addlines = ystatus - (lines - isize);
        else                                  /* smaller */
            addlines = 0;
    } else {                              /* SIGWINCH */
        /* Set ystatus to the top of the area fix_screen() must erase. */
        if (strcmp(TERM, "xterm") != 0) {
            /* not xterm: appearance is unknown, so start with a clear screen */
            addlines = 0;
            ystatus = 1;
        } else if (ystatus + isize < lines) {
            /* xterm grew: lines were added to top, so text moved down */
            addlines = 0;
            ystatus = lines - isize;
        } else if (cy <= lines) {
            /* xterm shrunk: 0 or more bottom lines were deleted */
            addlines = (ystatus + isize) - lines;
            ystatus = ystatus;
        } else {
            /* xterm shrunk: bottom lines deleted AND cursor&text moved up */
            addlines = isize - (cy - ystatus);
            ystatus = lines - (cy - ystatus);
        }
        cx = cy = -1;
#endif
    }

    if (addlines >= 0) {
        fix_screen();
        setup_screen(addlines);
        transmit_window_size();
    }
    old_isize = isize;
    return 1;
}

void fix_screen()
{
    oflush();
    if (keypad_off) tp(keypad_off);
    if (screen_mode <= 0) {
        clear_line();
#ifdef SCREEN
    } else {
        setscroll(1, lines);
        clear_lines(ystatus, lines);
        outcount = lines - 1;
        xy(1, ystatus);
        if (exit_ca_mode) tp(exit_ca_mode);
#endif
    }
    cx = cy = -1;
    bufflush();
    screen_mode = -1;
}

/* panic_fix_screen() avoids use of possibly corrupted structures. */
void panic_fix_screen()
{
    tp = tdirectputs;
    tfscreen->u.queue->head = tfscreen->u.queue->tail = NULL;
    outbuf->s = "";
    outbuf->len = 0;
    outbuf->size = 0;
    fix_screen();
}

static void clear_lines(start, end)
    int start, end;
{
    if (start > end) return;
    xy(1, start);
    if (end >= lines && clear_to_eos) {
        tp(clear_to_eos);  /* cx,cy were set by xy() */
    } else {
        clear_line();
        while (start++ < end) {
             bufputc('\n');
             clear_line();
        }
        cy = end;
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
        crnl(n);  /* DON'T: cy += n; */
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
        if (is_cntrl(c)) {
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
        hwrite(prompt, ppos, prompt->len);
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
        dobell(1);
    }
    if (place > keybuf->len) {
        place = keybuf->len;
        dobell(1);
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

void update_prompt(newprompt, display)
    Aline *newprompt;
    int display;
{
    Aline *oldprompt = prompt;

    if (oldprompt == moreprompt) return;
    prompt = newprompt;
    if ((oldprompt || prompt) && display)
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

static void hwrite(line, start, end)
    Aline *line;
    int start, end;
{
    attr_t attrs = line->attrs & F_HWRITE;
    attr_t current = 0;
    attr_t new;
    int i, ctrl;
    int col = 0;
    char c;

    if (line->attrs & F_BELL) {
        dobell(1);
    }

    if (line->attrs & F_INDENT) {
        bufputnc(' ', wrapspace);
        cx += wrapspace;
    }

    cx += end - start;

    if (!line->partials && hilite && attrs)
        attributes_on(current = attrs);

    for (i = start; i < end; ++i) {
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
        ctrl = (emulation != EMUL_RAW && is_cntrl(c));
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

int clear_more(new)
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
            if (is_alpha(str[total])) incode = FALSE;
        } else {
            if (is_print(str[total]))
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
        while (len && !is_space(str[len-1])) --len;
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
    int count = 0, offset;

    while ((line = wrapline()) != NULL) {
        offset = 0;
        if (count == 0) {
            clear_input_line();
            set_refresh_pending(REF_PHYSICAL);
        }
        count++;
        hwrite(line, offset, line->len);
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
        hwrite(line, 0, line->len);
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
        hwrite(line, 0, line->len);
        set_refresh_pending(REF_PHYSICAL);
        bufflush();
    }
}
#endif

/***********************************
 * Interfaces with rest of program *
 ***********************************/

int ch_hiliteattr()
{
    return set_attr_var(VAR_hiliteattr, &hiliteattr);
}

int ch_status_attr()
{
    if (!set_attr_var(VAR_status_attr, &status_attr)) return 0;
    update_status_line();
    return 1;
}

static int set_attr_var(idx, attrp)
    int idx;
    attr_t *attrp;
{
    CONST char *str;
    attr_t attr;

    if (!(str = getstrvar(idx))) {
        *attrp = 0;
        return 1;
    } else if ((attr = parse_attrs((char **)&str)) >= 0) {
        *attrp = attr;
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
    /* starting_attrs is set by the attrs parameter and/or codes at the
     * beginning of the line.  If no visible mid-line changes occur, there is
     * no need to allocate aline->partials (which would nearly triple the size
     * of the aline).  Note that a trailing attribute change is considered a
     * mid-line change; this is sub-optimal, but unprompt() depends on it
     * (it expects prompt->attrs to be the original starting attributes).
     */
    if (dest == aline->str) {
        /* start of visible line */
        *starting = *current;
    } else if (*starting != *current && !aline->partials) {
        /* First mid-line attr change. */
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
            if (!*s) break;            /* in case code got truncated */
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

            if (!*s) break;            /* in case code got truncated */
            if (*s == 'm') {           /* attribute command */
                attrs = new;
            } /* ignore any other CSI command */

        } else if (is_print(*s)) {
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
            if ((new & F_EXCLUSIVE) || (new & F_NONE)) attrs &= ~F_HWRITE;
            if (off) attrs &= ~new;
            else attrs |= new;
            if (new & F_BELL) aline->attrs |= F_BELL;

        } else if (is_print(*s)) {
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
        if (f->name) FREE(f->name);
        FREE(f);
    }
}
#endif

