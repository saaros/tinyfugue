/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/*****************************************************************
 * Fugue output handling
 *
 * Written by Ken Keys (Hawkeye).
 * Handles all screen-related phenomena.
 *****************************************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "world.h"
#include "socket.h"
#include "output.h"
#include "macro.h"
#include "search.h"
#include "signals.h"
#include "history.h"	/* record_*()... */
#include "keyboard.h"	/* set_ekey() */
#include "tty.h"	/* get_window_size() */


/* Terminal codes and capabilities.
 * If you're hardcoding for a different terminal, everything you need
 * to change is right here.
 */

#define DEFAULT_LINES   24
#define DEFAULT_COLUMNS 80

#ifdef HARDCODE
# define origin                    1           /* top left corner is (1,1) */
static char *clear_screen          = "\033[H\033[J";
static char *clear_to_eos          = "\033[J";
static char *clear_to_eol          = "\033[K";
static char *cursor_address        = "\033[%d;%dH";  /* (in printf format) */
static char *enter_ca_mode         = NULL;           /* enable cursor motion */
static char *exit_ca_mode          = NULL;           /* disable cursor motion */
static char *change_scroll_region  = "\033[%d;%dr";  /* (in printf format) */
static char *insert_line           = "\033[L";
static char *delete_line           = "\033[M";
static char *underline             = "\033[4m";
static char *reverse               = "\033[7m";
static char *flash                 = "\033[5m";
static char *dim                   = NULL;
static char *bold                  = "\033[1m";
static char *attr_off              = "\033[m";  /* turn off all attributes */
static char *underline_off         = NULL;      /* only needed if no attr_off */
static char *standout_off          = NULL;      /* only needed if no attr_off */
static char *carriage_return       = "\r";

#else
# define origin                    0           /* top left corner is (0,0) */
static char *clear_screen          = NULL;
static char *clear_to_eos          = NULL;
static char *clear_to_eol          = NULL;
static char *cursor_address        = NULL;  /* Move cursor        */
static char *enter_ca_mode         = NULL;  /* Cursor motion mode */
static char *exit_ca_mode          = NULL;  /* Cursor motion mode */
static char *change_scroll_region  = NULL;
static char *insert_line           = NULL;
static char *delete_line           = NULL;
static char *underline             = NULL;
static char *reverse               = NULL;
static char *flash                 = NULL;
static char *dim                   = NULL;
static char *bold                  = NULL;
static char *attr_off              = NULL;
static char *underline_off         = NULL;
static char *standout_off          = NULL;
static char *carriage_return       = NULL;
#endif

static void  NDECL(init_term);
static int   FDECL(fbufputc,(int c));
static void  NDECL(bufflush);
static void  FDECL(tp,(char *str));
static void  FDECL(xy,(int x, int y));
static void  NDECL(clear_line);
static void  FDECL(clear_lines,(int start, int end));
static void  NDECL(clear_input_window);
static void  FDECL(erase_tail,(int endx, int endy, int len));
static void  FDECL(setscroll,(int y1, int y2));
static void  FDECL(scroll_input,(int n));
static void  FDECL(ioutput,(int c));
static void  FDECL(ioutputs,(char *str, int len));
static void  FDECL(ioutall,(int kpos));
static void  FDECL(attributes_off,(int attrs));
static void  FDECL(attributes_on,(int attrs));
static void  FDECL(hwrite,(char *s, unsigned int len, int attrs, short *partials));
static void  NDECL(discard_screen_queue);
static int   NDECL(check_more);
static int   FDECL(clear_more,(int new));
static Aline *NDECL(wrapline);
static void  NDECL(output_novisual);
#ifdef SCREEN
static void  NDECL(output_noscroll);
static void  NDECL(output_scroll);
#else
# define output_noscroll()      /* not supported without TERMCAP or HARDCODE */
# define output_scroll()        /* not supported without TERMCAP or HARDCODE */
#endif

#ifdef TERMCAP
#define tpgoto(seq,x,y)  tp(tgoto(seq, (x)-1+origin, (y)-1+origin))
#else
#define tpgoto(seq,x,y)  Sprintf(outbuf,SP_APPEND,seq,(y)-1+origin,(x)-1+origin)
#endif

#define ipos()		xy(ix, iy)

#define Wrap (wrapsize ? wrapsize : columns)
#define keyboard_end (keybuf->len)

/* Buffered output */

#define bufputs(s)	Stringcat(outbuf, s)
#define bufputns(s, n)	Stringncat(outbuf, s, n)
#define bufputc(c)	Stringadd(outbuf, c)
#define bufputnc(c, n)	Stringnadd(outbuf, c, n)

/* Others */

extern int keyboard_pos;            /* position of logical cursor in keybuf */
extern Stringp keybuf;              /* input buffer */

STATIC_BUFFER(outbuf);              /* output buffer */
static int cx = -1, cy = -1;        /* Real cursor ((-1,-1)==unknown) */
static int ox, oy;                  /* Output cursor */
static int ix, iy;                  /* Input cursor */
static int ystatus;                 /* line # of status line */
static int istarty, iendy, iendx;   /* start/end of current input line */
STATIC_BUFFER(moreprompt);          /* pager prompt */
static String *prompt;              /* current prompt */
static int outcount;                /* lines remaining until more prompt */
static int paused = FALSE;          /* output paused? */
static short have_attr = 0;         /* available attributes */
static short more_attrs;            /* attributes for more prompt */
static Aline *currentline = NULL;   /* current logical line for printing */
static char *nextphys = NULL;       /* beginning of next wrapped line */

int lines   = 0;
int columns = 0;
int can_have_visual = FALSE;
int need_refresh = 0;               /* does input need refresh? */
int echoflag = TRUE;                /* echo input? */
int screen_setup = FALSE;           /* is *screen* in visual mode? */
TFILE *tfscreen;                    /* text waiting to be displayed */
TFILE *tfout;                       /* pointer to current output queue */
TFILE *tferr;                       /* pointer to current error queue */

#ifdef TERMCAP
extern int   FDECL(tgetent,(char *buf, char *name));
extern int   FDECL(tgetnum,(char *id));
extern int   FDECL(tgetflag,(char *id));
extern char *FDECL(tgetstr,(char *id, char **area));
extern char *FDECL(tgoto,(char *code, int destcol, int destline));
extern int   FDECL(tputs,(char *cp, int affcnt, int (*outc)(int)));
#endif

/****************************
 * BUFFERED OUTPUT ROUTINES *
 ****************************/

static void bufflush()
{
    if (outbuf->len) {
        write(1, outbuf->s, outbuf->len);
        Stringterm(outbuf, 0);
    }
}

static int fbufputc(c)
    int c;
{
    Stringadd(outbuf, c);
    return c;
}

void bell(n)
    int n;
{
    bufputnc('\007', n);
    bufflush();
}

/********************
 * TERMCAP ROUTINES *
 ********************/

void change_term()
{
    int old = visual;
    setvar("visual", "0", FALSE);
    init_term();
    if (old) setvar("visual", "1", FALSE);
}

/* Initialize basic output data.
 * Termcap data will be initialized when init_values() inits %{TERM}.
 */
void init_output()
{
    char *str;

    tfout = tferr = tfscreen = tfopen(NULL, "q");
    Stringcpy(moreprompt, "--More--");
    prompt = fgprompt();
    carriage_return = "\r";

    /* window size: try environment, ioctl TIOCGWINSIZ, termcap, defaults. */
    if ((str = getvar("LINES"))) lines = atoi(str);
    if ((str = getvar("COLUMNS"))) columns = atoi(str);
    if (lines <= 0 || columns <= 0) get_window_size();

    /* init_term() will be called when %{TERM} is init'ed by init_values(). */
}

static void init_term()
{
    int error = 0;
#ifdef TERMCAP
    char *standout, *str;
    char termcap_entry[1024];
    static char termcap_buffer[1024];
    char *area = termcap_buffer;

    have_attr = 0;
    can_have_visual = FALSE;
    clear_screen = clear_to_eos = clear_to_eol = NULL;
    change_scroll_region = insert_line = delete_line = NULL;
    enter_ca_mode = exit_ca_mode = cursor_address = NULL;
    standout = underline = reverse = flash = dim = bold = NULL;
    standout_off = underline_off = attr_off = NULL;

    if (!TERM || !*TERM) {
        error = 1;
    } else if (tgetent(termcap_entry, TERM) <= 0) {
        error = 2;
    } else {
        if (columns <= 0) columns = tgetnum("co");
        if (lines   <= 0) lines   = tgetnum("li");

        if (!(carriage_return = tgetstr("cr", &area))) carriage_return = "\r";
        clear_screen         = tgetstr("cl", &area);
        clear_to_eol         = tgetstr("ce", &area);
        clear_to_eos         = tgetstr("cd", &area);
        enter_ca_mode        = tgetstr("ti", &area);
        exit_ca_mode         = tgetstr("te", &area);
        cursor_address       = tgetstr("cm", &area);
        change_scroll_region = tgetstr("cs", &area);
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
            reverse = flash = dim = NULL;
            if (!underline_off) underline = NULL;
            if (!standout_off)  standout = NULL;
        }
        if (!bold && standout_off) bold = standout;

        if ((str = tgetstr("ku", &area))) set_ekey(str, "/DOKEY UP");
        if ((str = tgetstr("kd", &area))) set_ekey(str, "/DOKEY DOWN");
        if ((str = tgetstr("kr", &area))) set_ekey(str, "/DOKEY RIGHT");
        if ((str = tgetstr("kl", &area))) set_ekey(str, "/DOKEY LEFT");

        /* Many old xterm termcaps mistakenly omit "cs". */
        if (!change_scroll_region && strcmp(TERM, "xterm") == 0)
            change_scroll_region = "\033[%i%d;%dr";
    }
#endif

    if (columns <= 0) columns = DEFAULT_COLUMNS;
    if (lines   <= 0) lines   = DEFAULT_LINES;
    setivar("wrapsize", columns - 1, FALSE);
    outcount = lines;
    ix = 1;
    can_have_visual = clear_screen && cursor_address;
    setivar("scroll", change_scroll_region||(insert_line&&delete_line), FALSE);
    have_attr = F_BELL;
    if (underline) have_attr |= F_UNDERLINE;
    if (reverse)   have_attr |= F_REVERSE;
    if (flash)     have_attr |= F_FLASH;
    if (dim)       have_attr |= F_DIM;
    if (bold)      have_attr |= F_BOLD;
    if (error == 1)
        oputs("% Warning: TERM undefined");
    else if (error == 2)
        tfprintf(tferr, "%% Warning: \"%s\" terminal unsupported.", TERM);
}

static void setscroll(y1,y2)
    int y1, y2;
{
    tpgoto(change_scroll_region, (y2), (y1));
    cx = cy = -1;   /* cursor position is undefined after termcap "cs" */
}

static void xy(x,y)
    int x, y;
{
    if (x == cx && y == cy) return;                        /* no-op */
    if (cx < 0 || cy < 0) {                                /* direct movement */
        tpgoto(cursor_address, (x), (y));
    } else if (x==1 && y==cy) {                            /* optimization */
        bufputc('\r');
    } else if (x==1 && y>=cy && y<cy+5 && y<ystatus ) {    /* optimization */
        /* note: '\n' is not safe outside scroll region */
        bufputc('\r');
        bufputnc('\n', y - cy);
    } else if (y == cy && x < cx && x > cx - 7) {          /* optimization */
        bufputnc('\010', cx - x);
    } else {                                               /* direct movement */
        tpgoto(cursor_address, (x), (y));
    }
    cx = x;  cy = y;
}

void clr()
{
    if (clear_screen) {
        tp(clear_screen);
        cx = 1;  cy = 1;
    }
}

static void clear_line()
{
    if (cx != 1) bufputc('\r');
    cx = 1;
    if (clear_to_eol) {
        tp(clear_to_eol);
    } else {
        bufputnc(' ', wrapsize);  /* cx += wrapsize; */
        bufputc('\r');            /* cx = 1; */
    }
}

static void tp(str)
    char *str;
{
    if (str)
#ifdef TERMCAP
        tputs(str, 1, fbufputc);
#else
        bufputs(str);
#endif
}

/*******************
 * WINDOW HANDLING *
 *******************/

void setup_screen()
{
    if (!visual) {
        if (paused) prompt = moreprompt;
        return;
    }
#ifdef SCREEN
    prompt = fgprompt();
    if (isize <= 0) isize = 3;
    if (isize > lines - 3) isize = lines - 3;
    ystatus = lines - isize;
    outcount = ystatus - 1;
    if (enter_ca_mode) tp(enter_ca_mode);

    if (scroll) {
        if (change_scroll_region) setscroll(1, lines);
        xy(1, lines);
        bufputnc('\n', isize);  /* At bottom.  Don't increment cy. */
        if (change_scroll_region) setscroll(1, ystatus - 1);
    } else {
        clr();
        oy = 1;
    }
    status_bar(STAT_ALL);
    ix = iendx = oy = ox = 1;
    iy = iendy = istarty = ystatus + 1;
    ipos();
    screen_setup = TRUE;
#endif
    bufflush();
}

int redraw()
{
    if (screen_setup && visual) {
        clr();
        setup_screen();
        logical_refresh();
        return 1;
    } else return 0;
}

void status_bar(seg)
    int seg;
{
    extern int log_on;
    extern int mail_flag;           /* should mail flag on status bar be lit? */
    extern int active_count;        /* number of active sockets */

    if (!visual) return;

    if (seg & STAT_MORE) {
        xy(1, ystatus);
        if (paused) hwrite(moreprompt->s, moreprompt->len, more_attrs, NULL);
        else hwrite("________", moreprompt->len, F_NORM, NULL);
        bufputc('_');  cx++;
    }
    if (seg & STAT_WORLD) {
        int len, max;
        World *world;
        xy(10, ystatus);
        max = columns - 38 - 10 - 1;
        if ((world = fworld())) bufputns(world->name, max);
        len = max - (world ? strlen(world->name) : 0) + 1;
        if (len > 0) bufputnc('_', len);
        cx += max + 1;
    }
    if (seg & STAT_ACTIVE) {
        smallstr buf;
        xy(columns - 38, ystatus);
        if (active_count) {
            sprintf(buf, "(Active:%2d)_", active_count);
            bufputs(buf);
        } else bufputs("____________");
        cx += 12;
    }
    if (seg & STAT_LOGGING) {
        xy(columns - 26, ystatus);
        bufputs(log_on ? "(Log)_" : "______");  cx += 6;
    }
    if (seg & STAT_MAIL) {
        xy(columns - 20, ystatus);
        bufputs(mail_flag ? "(Mail)_" : "_______");  cx += 7;
    }
    if (seg & STAT_INSERT) {
        xy(columns - 13, ystatus);
        bufputs(insert ? "(Insert)_" : "_________");  cx += 9;
    }
    if (seg & STAT_CLOCK) {
        xy(columns - 4, ystatus);
        if (clock_flag) {
            extern TIME_T clock_update;
            clock_update = time(NULL);
            bufputs(tftime("%H:%M", clock_update));
            clock_update += 60 - atoi(tftime("%S", clock_update));
        } else {
            bufputs("_____");
        }
        cx += 5;
    }
    /* ipos(); */
    bufflush();
    need_refresh = REF_PHYSICAL;
}

void tog_insert()
{
    status_bar(STAT_INSERT);
}

void change_isize()
{
    if (visual) {
        fix_screen();
        setup_screen();
    }
}

void tog_visual()
{
    if (visual == screen_setup) return;
    if (visual && !can_have_visual) {
        setvar("visual", "0", FALSE);
        tfputs("% Visual mode not supported.", tferr);
    } else {
        if (!visual) fix_screen();
        setup_screen();
    }
}

void fix_screen()
{
    clear_input_window();
    outcount = lines - 1;
#ifdef SCREEN
    if (change_scroll_region) setscroll(1, lines);
    xy(1, ystatus);
    clear_line();
    if (exit_ca_mode) tp(exit_ca_mode);
    screen_setup = 0;
#endif
    bufflush();
}

static void clear_lines(start, end)
    int start, end;
{
    if (start > end) return;
    xy(1, start);
    if (end == lines && clear_to_eos) {
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
    /* only called if screen_setup */
    if (clear_to_eos) {
        xy(1, ystatus + 1);
        tp(clear_to_eos);
    } else {
        clear_lines(ystatus + 1, lines);
    }
    ix = iendx = 1;
    iy = iendy = istarty = ystatus + 1;
    ipos();
}

/* clear logical input line */
void clear_input_line()
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
    setscroll(ystatus + 1, lines);
    xy(1, lines);
    bufputnc('\n', n);  /* cy += n; */
    setscroll(1, ystatus - 1);
    xy(iendx = 1, iendy = lines - n + 1);
}


/***********************************************************************
 *                                                                     *
 *                        INPUT WINDOW HANDLING                        *
 *                                                                     *
 ***********************************************************************/

static void ioutput(c)
    int c;
{
    char ch = (char)c;
    ioutputs(&ch, 1);
}

/* ioutputs
 * Print string within bounds of input window.
 * precondition: iendx,iendy point to starting point for string.
 */
static void ioutputs(str, len)
    char *str;
    int len;
{
    int size;

    if (len <= 0) {
        /* do nothing */
    } else if (visual) {
        while (len) {
            if (iendy == lines && iendx > Wrap) break;
            size = Wrap - iendx + 1;
            if (size > len) size = len;
            bufputns(str, size);  cx += size;
            len -= size;
            str += size;
            iendx += size;
            if (iendx > Wrap && iendy != lines) xy(iendx = 1, ++iendy);
        }
    } else if ((size = Wrap - iendx + 1) > 0) {
        if (size > len) size = len;
        iendx += size;
        bufputns(str, size);  cx += size;
    }
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
        ppos = prompt->len + kpos;
        if (ppos < 0) ppos = 0;      /* possible if prompt->len > wrapsize */
        ioutputs(prompt->s + ppos, prompt->len - ppos);
        kpos = 0;
    }
    ioutputs(keybuf->s + kpos, keybuf->len - kpos);
}

void iputs(s)
    char *s;
{
    int i, j, end;

    if (!s[0]) return;
    if (!visual) {
        for (i = j = 0; s[j]; j++) {
            if (++iendx > Wrap) iendx = Wrap;
            if (++ix > Wrap) {
                bufputns(s + i, j - i + 1);
                bufputc('\r');  cx = 1;
                bufputc('\n');  cy++;
                iendx = ix = 1;
                i = j + 1;
            }
        }
        if (i < j) {
            bufputns(s + i, j - i);
            cx += j - i;
        }

        if (insert || ix == 1) {
            iendx = ix;
            ioutputs(keybuf->s + keyboard_pos, keyboard_end - keyboard_pos);
            bufputnc('\010', iendx - ix);  cx -= (iendx - ix);
        }
        bufflush();
        return;
    }

    /* visual */
    ipos();
    for (j = 0; s[j]; j++) {
        iendx = ix;
        iendy = iy;
        if (ix == Wrap && iy == lines) {
            if (change_scroll_region && isize > 1 && !clearfull) {
                bufputc(s[j]);  cx++;
                scroll_input(1);
                ix = 1;
                ipos();
                if (--istarty < ystatus + 1) istarty = ystatus + 1;
            } else {
                bufputc(s[j]);  cx++;
                clear_input_window();
            }
        } else ioutput(s[j]);
        ix = iendx;
        iy = iendy;
    }
    if (insert) {
        ioutputs(keybuf->s + keyboard_pos, keyboard_end - keyboard_pos);
        if (keyboard_pos != keyboard_end) ipos();
    } else {
        end = ix + keyboard_end - keyboard_pos - 1;
        if ((iendy = iy + end / Wrap) > lines) {
            iendy = lines;
            iendx = Wrap;
        } else {
            iendx = end % Wrap + 1;
        }
    }
    bufflush();
}

void inewline()
{
    ix = iendx = 1;
    if (!visual) {
        bufputc('\r');  cx = 1;
        bufputc('\n');  cy++;
        if (prompt && prompt->len > 0) need_refresh = REF_PHYSICAL;

    } else {
        if (cleardone) {
            clear_input_window();
        } else {
            iy = iendy + 1;
            if (iy > lines) {
                if (change_scroll_region && isize > 1 && !clearfull) {
                    scroll_input(1);
                    iy--;
                } else {
                    clear_input_window();
                }
            }
        }
        istarty = iendy = iy;
        need_refresh = REF_PHYSICAL;
    }
    bufflush();
}

void idel(place)
    int place;
{
    int kpos, len;
    int oiex = iendx, oiey = iendy;
    int old_pos = keyboard_pos;

    if (!echoflag && !always_echo) return;
    if ((len = place - old_pos) < 0) ix += len;
    if (!visual) {
        if (ix < 1 || need_refresh) {
            keyboard_pos = place;
            iendx = ix = Wrap;     /* ??? */
            physical_refresh();
            return;
        }
    } else {
        /* visual */
        ipos();
        while (ix < 1) {
            ix += Wrap;
            iy--;
        }
        if (iy <= ystatus) {
            kpos = place - (Wrap * isize) + (Wrap - ix) + 1;
            clear_input_window();
            ioutall(kpos);
            ix = iendx;
            iy = iendy;
            ioutputs(keybuf->s + place, keyboard_end - place);
            ipos();
            bufflush();
            return;
        }
    }
    if (len < 0) {    /* must move cursor */
        if (visual) ipos();
        else { bufputnc('\010', -len);  cx += len; }
        keyboard_pos = place;
    }
    iendx = ix;
    iendy = iy;
    ioutputs(keybuf->s + keyboard_pos, keyboard_end - keyboard_pos);
    erase_tail(oiex, oiey, len > 0 ? len : -len);
    bufflush();
}

int newpos(place)
    int place;
{
    int diff, new, kpos;

    if (place < 0) place = 0;
    if (place > keyboard_end) place = keyboard_end;

    if (!(diff = place - keyboard_pos)) {
        /* no change */
    } else if (!visual) {
        ix += diff;
        if (ix < 1 || ix > Wrap) {
            keyboard_pos = place;
            physical_refresh();
        } else {
            if (diff < 0) bufputnc('\010', keyboard_pos - place);
            else while (keyboard_pos < place) bufputc(keybuf->s[keyboard_pos++]);
            cx += diff;
            keyboard_pos = place;
        }

    /* visual */
    } else {
        int nix, niy;
        new = (iy - 1) * Wrap + (ix - 1) + diff;
        niy = new / Wrap + 1;
        nix = new % Wrap + 1;
        while (niy > lines) {
            kpos = keyboard_pos + (lines - iy + 1) * Wrap - ix + 1;
            scroll_input(1);
            ioutputs(keybuf->s + kpos, keyboard_end - kpos);
            keyboard_pos += (lines - ystatus) * Wrap;
            niy--;
        }
        if (niy < ystatus + 1) {
            kpos = keyboard_pos - (iy - niy) * Wrap - ix + 1;
            clear_input_window();
            ioutall(kpos);
            niy = ystatus + 1;
        }
        ix = nix;  iy = niy;
        ipos();
        keyboard_pos = place;
    }
    bufflush();
    return keyboard_pos;
}

/* Erase tail of logical line, and restore cursor position to ix,iy.
 * oiex,oiey is the original iendx,iendy.
 * len is the length of the tail to be erased.
 * preconditions: ix,iy are updated; real cursor is at beginning of tail.
 */
static void erase_tail(oiex, oiey, len)
    int oiex, oiey, len;
{
    if (visual && clear_to_eos && (len > 2 || cy < oiey)) {   /* optimization */
        tp(clear_to_eos);
        ipos();
    } else {
        if (len > oiex - cx + 1) len = oiex - cx + 1;
        if (clear_to_eol && len > 2) {
            tp(clear_to_eol);
        } else {
            bufputnc(' ', len);
            cx += len;
        }
        if (visual && cy != iy) clear_lines(cy + 1, oiey);
        if (visual) ipos();
        else { bufputnc('\010', cx - ix);  cx = ix; }
    }
}

int dokey_refresh()
{
    int oix = ix, oiy = iy, kpos;

    if (!visual) {
        logical_refresh();
    } else {
        kpos = keyboard_pos - (iy - istarty) * Wrap - (ix - 1);
        clear_input_line();
        ioutall(kpos);
        ix = oix;
        iy = oiy;
        ipos();
    }
    bufflush();
    return 1;
}

void physical_refresh()
{
    int curcol, kpos;

    if (visual) {
        ipos();
    } else {
        curcol = ((prompt ? prompt->len : 0) + keyboard_pos) % Wrap;
        kpos = keyboard_pos - curcol;
        clear_input_line();
        ioutall(kpos);
        ix = curcol + 1;
        bufputnc('\010', iendx - ix);  cx -= (iendx - ix);
    }
    bufflush();
    if (need_refresh == REF_PHYSICAL) need_refresh = 0;
}

void logical_refresh()
{
    int okpos;

    if (visual) {
        clear_input_line();
        okpos = keyboard_pos;
        keyboard_pos = keyboard_end;
        if (prompt) iputs(prompt->s);
        if (echoflag || always_echo) iputs(keybuf->s);
        newpos(okpos);
    } else {
        physical_refresh();
    }
    need_refresh = 0;
}

void refresh_prompt(newprompt)
    String *newprompt;
{
    /* To do: don't print the new prompt if it compares equal to the old. */
    if (prompt != moreprompt) {
        prompt = newprompt;
        logical_refresh();
    }
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
    int attrs;
{
    char *colorcode;

    if (attrs & ~F_COLOR) {
        if (attr_off) tp(attr_off);
        else {
            if (underline_off) tp(underline_off);
            if (standout_off)  tp(standout_off);
        }
    }
    if ((attrs & F_COLOR) && (colorcode = getvar("end_color"))) {
        bufputs(print_to_ascii(colorcode));
    }
}

static void attributes_on(attrs)
    int attrs;
{
    if (attrs & F_HILITE) attrs |= hiliteattr;
    if ((attrs & F_BELL) && beep) bell(1);
    if (have_attr & attrs & F_UNDERLINE) tp(underline);
    if (have_attr & attrs & F_REVERSE)   tp(reverse);
    if (have_attr & attrs & F_FLASH)     tp(flash);
    if (have_attr & attrs & F_DIM)       tp(dim);
    if (have_attr & attrs & F_BOLD)      tp(bold);
    if (attrs & F_COLOR) {
        char *colorcode;
        extern char *enum_color[];
        STATIC_BUFFER(buf);
        Sprintf(buf, 0, "start_color_%s", enum_color[attrs & F_COLOR]);
        if ((colorcode = getvar(buf->s)))
            bufputs(print_to_ascii(colorcode));
    }
}

static void hwrite(s, len, attrs, partials)
    char *s;
    unsigned int len;
    int attrs;
    short *partials;
{
    short current;

    if (attrs & F_INDENT) {
        bufputnc(' ', wrapspace);  cx += wrapspace;
    }

    attrs &= F_HWRITE;
    if (hilite && attrs) attributes_on(attrs);

    current = attrs;
    if (!partials || !hilite) {
        bufputns(s, len);  cx += len;
    } else {
        int start, end;
        for (start = 0; ; start = end) {
            for (end = start; end < len; ++end)
                if ((partials[end] | attrs) != current) break;
            bufputns(s + start, end - start); cx += (end - start);
            if (current) attributes_off(current);
            if (end == len) break;
            current = partials[end] | attrs;
            attributes_on(current);
        }
    }

    if ((attrs | current) && hilite) attributes_off(attrs | current);
}

void reset_outcount()
{
    outcount = visual ? (scroll ? (ystatus - 1) : outcount) : lines - 1;
}

/* return TRUE if okay to print */
static int check_more()
{
    if (paused) return FALSE;
    if (!more || ox != 1) return TRUE;
    if (outcount-- > 0) return TRUE;

    paused = TRUE;
    if ((more_attrs = do_hook(H_MORE, NULL, "")) == 0)
        more_attrs = F_BOLD | F_REVERSE;
    if (visual) {
        status_bar(STAT_MORE);
    } else {
        prompt = moreprompt;
        logical_refresh();
    }
    return FALSE;
}

static int clear_more(new)
    int new;
{
    if (!paused) return 0;
    paused = FALSE;
    outcount = new;
    if (visual) {
        status_bar(STAT_MORE);
        if (!scroll) outcount = ystatus - 1;
    } else {
        prompt = fgprompt();
        clear_input_line();
    }
    need_refresh = REF_PHYSICAL;
    oflush();
    return 1;
}

void tog_more()
{
    if (!more) {
        reset_outcount();
        clear_more(outcount);
    } else if (!scroll) {
        clear_lines(1, ystatus - 1);
        outcount = ystatus - 1;
        oy = 1;
    }
}

int dokey_page()
{
    return clear_more(visual ? ystatus - 1 : lines - 1);
}

int dokey_hpage() 
{
    return clear_more((visual ? ystatus - 1 : lines - 1) / 2);
}

int dokey_line()
{
    return clear_more(1);
}

int dokey_flush()
{
    if (!paused) return 0;
    discard_screen_queue();
    return 1;
}

static void discard_screen_queue()
{
    outcount = visual ? ystatus - 1 : lines - 1;
    free_queue(tfscreen->u.queue);
    if (currentline) {
        free_aline(currentline);
        currentline = NULL;
        nextphys = NULL;
    }
    clear_more(outcount);
    screenout(new_aline("--- Output discarded ---", F_NEWLINE));
}

/* wrapline
 * Return a pointer to a static Aline containing the next physical line
 * to be printed.
 */
static Aline *wrapline()
{
    static int firstline = TRUE;
    static Aline *dead = NULL;
    static Aline result;

    if (dead) { free_aline(dead); dead = NULL; }

    while (!currentline || ((currentline->attrs & F_GAG) && gag)) {
        if (currentline) free_aline(currentline);
        if (!(currentline = dequeue(tfscreen->u.queue))) return NULL;
    }
    if (!check_more()) return NULL;
    if (!nextphys) {
        nextphys = currentline->str;
        result.attrs = currentline->attrs;
        result.partials = currentline->partials;
        firstline = TRUE;
    }

    if (!firstline) result.attrs &= ~F_BELL;
    if (!firstline && wrapflag && wrapspace < Wrap)
        result.attrs |= F_INDENT;
    result.str = wrap(&nextphys, &result.len, &firstline);
    if (currentline->partials) {
        result.partials = currentline->partials + (result.str - currentline->str);
    }
    if (!*nextphys) {
        dead = currentline;   /* remember so we can free it next time */
        nextphys = NULL;
        currentline = NULL;
    }
    return &result;
}

/* wrap
 * Returns original *ptr.  After call, *ptr points to begining of next physical
 * line, *lenp contains the length to be displayed, and *firstp is a flag.
 */ 
char *wrap(ptr, lenp, firstp)
    char **ptr;
    unsigned int *lenp;
    int *firstp;
{
    char *start, *max;

    max = *ptr + Wrap;
    if (!*firstp && wrapflag && wrapspace < Wrap) max -= wrapspace;

    for (start = *ptr; **ptr && *ptr < max && **ptr != '\n'; ++*ptr);
    if (!**ptr) {
        *lenp = *ptr - start;
        *firstp = TRUE;
    } else if (**ptr == '\n') {
        *lenp = (*ptr)++ - start;
        *firstp = TRUE;
    } else {
        --*ptr;
        if (wrapflag)
            while (*ptr != start && !isspace(**ptr)) --*ptr;
        *lenp = (*ptr == start) ? ((*ptr = max) - start) : (++*ptr - start);
        *firstp = FALSE;
    }
    return start;
}


/****************
 * Main drivers *
 ****************/

void globalout(aline)
    Aline *aline;
{
    record_global(aline);
    screenout(aline);
}

/* write to tfscreen (no history) */
void screenout(aline)
    Aline *aline;
{
    if (!currentline && !tfscreen->u.queue->head) {
        /* shortcut if screen queue is empty */
        (currentline = aline)->links++;
    } else {
        aline->links++;
        enqueue(tfscreen->u.queue, aline);
    }
    if (!paused && !interrupted()) oflush();
}

void oflush()
{
    if (!screen_setup) output_novisual();
    else if (scroll) output_scroll();
    else output_noscroll();
}

static void output_novisual()
{
    Aline *line;
    int first = 1;

    while ((line = wrapline()) != NULL) {
        if (first && ix != 1) {
            clear_input_line();
            need_refresh = REF_PHYSICAL;
        }
        first = 0;
        hwrite(line->str, line->len, line->attrs, line->partials);
        if (line->attrs & F_NEWLINE) {
            bufputc('\r');  cx = 1;
            bufputc('\n');  cy++;
            ox = 1;
        } else ox += line->len;
        bufflush();
    }
}

#ifdef SCREEN
static void output_noscroll()
{
    Aline *line;

    while ((line = wrapline()) != NULL) {
        if (ox == 1) {
            xy(1, (oy + 1) % (ystatus - 1) + 1);
            clear_line();
        }
        xy(ox, oy);
        need_refresh = REF_PHYSICAL;
        hwrite(line->str, line->len, line->attrs, line->partials);
        if (line->attrs & F_NEWLINE) {
            ox = 1;
            oy = oy % (ystatus - 1) + 1;
        }
        else ox += line->len;
        bufflush();
    }
}

static void output_scroll()
{
    Aline *line;

    while ((line = wrapline()) != NULL) {
        if (change_scroll_region) {
            if (cy != ystatus - 1) xy(columns, ystatus - 1);
            bufputc('\n');  /* At bottom.  Don't increment cy. */
            bufputc('\r');  cx = 1;
        } else {
            xy(1, 1);
            tp(delete_line);
            xy(1, ystatus - 1);
            tp(insert_line);
        }
        hwrite(line->str, line->len, line->attrs, line->partials);
        if (line->attrs & F_NEWLINE) ox = 1;
        else ox += line->len;
        need_refresh = REF_PHYSICAL;
        bufflush();
    }
}
#endif

/***********************************
 * Interfaces with rest of program *
 ***********************************/

int getwrap()
{
    return (Wrap);
}

void ch_clock()
{
#ifdef HAVE_STRFTIME
    status_bar(STAT_CLOCK);
#else
    if (clock_flag) {
        oputs("% clock not supported.");
        setvar("clock", "off", FALSE);
    }
#endif
}

void ch_hilite()
{
    char *str;

    if (!(str = getvar("hiliteattr"))) return;
    if ((hiliteattr = parse_attrs(&str)) < 0) {
        hiliteattr = 0;
    }
}


#ifdef DMALLOC
void free_term()
{
    tfclose(tfscreen);
}
#endif
