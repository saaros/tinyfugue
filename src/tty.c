/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tty.c,v 35004.9 1997/04/02 23:50:21 hawkeye Exp $ */

/*
 * TTY driver routines.
 */

#include "config.h"
#include <errno.h>
extern int errno;
#include "port.h"

#ifdef EMXANSI
# define INCL_VIO
# include <os2.h>
#endif

#ifdef USE_TERMIOS                    /* POSIX is the way to go. */
# include <termios.h>
# ifndef TIOCGWINSZ
#  include <sys/ioctl.h>              /* BSD needs this for TIOCGWINSZ */
# endif
# define tty_struct struct termios
# define insetattr(buf) (tcsetattr(STDIN_FILENO, TCSAFLUSH, (buf)))
# define ingetattr(buf) (tcgetattr(STDIN_FILENO, (buf)))
# define insetattr_error "tcsetattr"
# define ingetattr_error "tcgetattr"
#endif

#ifdef USE_TERMIO      /* with a few macros, this looks just like USE_TERMIOS */
# ifdef hpux                                   /* hpux's termio is different. */
#  undef USE_TERMIO
#  define USE_HPUX_TERMIO
#  include <sys/ioctl.h>
#  include <termio.h>
#  include <bsdtty.h>
# else
#  define USE_TERMIOS
#  include <termio.h>
# endif
# define tty_struct struct termio
# define insetattr(buf) (ioctl(STDIN_FILENO, TCSETAF, (buf)))
# define ingetattr(buf) (ioctl(STDIN_FILENO, TCGETA, (buf)))
# define insetattr_error "TCSETAF ioctl"
# define ingetattr_error "TCGETA ioctl"
#endif

#ifdef NEED_PTEM_H                   /* Xenix, maybe others */
# include <sys/types.h>              /* Needed for sys/stream.h, which is... */
# include <sys/stream.h>             /* needed for sys/ptem.h, which is... */
# include <sys/ptem.h>               /* needed for struct winsize.  Ugh. */
#endif

#ifdef USE_SGTTY
# include <sys/ioctl.h>
# include <sgtty.h>                  /* BSD's old "new" terminal driver. */
# define tty_struct struct sgttyb
# define insetattr(buf) (ioctl(STDIN_FILENO, TIOCSETP, (buf)))
# define ingetattr(buf) (ioctl(STDIN_FILENO, TIOCGETP, (buf)))
# define insetattr_error "TIOCSETP ioctl"
# define ingetattr_error "TIOCGETP ioctl"
#endif

static tty_struct old_tty;
static int is_custom_tty = 0;        /* is tty in customized mode? */

int no_tty = 1;

#include "tf.h"
#include "dstring.h"	/* for util.h, output.h */
#include "util.h"	/* for macro.h */
#include "tty.h"
#include "output.h"	/* ch_visual() */
#include "macro.h"	/* add_ibind() */
#include "search.h"	/* for variable.h */
#include "variable.h"	/* setivar() */

#define DEFAULT_COLUMNS 80

void init_tty()
{
#ifdef USE_HPUX_TERMIO
    struct ltchars chars;
#endif
#ifdef USE_SGTTY
    struct ltchars chars;
#endif

    char bs[2], dline[2], bword[2], refresh[2], lnext[2];
    bs[0] = dline[0] = bword[0] = refresh[0] = lnext[0] = '\0';

    no_tty = !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO);
    cbreak_noecho_mode();

#ifdef USE_TERMIOS
    *bs = old_tty.c_cc[VERASE];
    *dline = old_tty.c_cc[VKILL];
# ifdef VWERASE /* Not POSIX, but many systems have it. */
    *bword = old_tty.c_cc[VWERASE];
# endif
# ifdef VREPRINT /* Not POSIX, but many systems have it. */
    *refresh = old_tty.c_cc[VREPRINT];
# endif
# ifdef VLNEXT /* Not POSIX, but many systems have it. */
    *lnext = old_tty.c_cc[VLNEXT];
# endif
#endif

#ifdef USE_HPUX_TERMIO
    *bs = old_tty.c_cc[VERASE];
    *dline = old_tty.c_cc[VKILL];
    if (ioctl(STDIN_FILENO, TIOCGLTC, &chars) < 0) perror("TIOCGLTC ioctl");
    else {
        *bword = chars.t_werasc;
        *refresh = chars.t_rprntc;
        /* *lnext = chars.t_lnextc; */  /* ?? Screw it, use default. */
    }
#endif

#ifdef USE_SGTTY
    *bs = old_tty.sg_erase;
    *dline = old_tty.sg_kill;
    if (ioctl(STDIN_FILENO, TIOCGLTC, &chars) < 0) perror("TIOCGLTC ioctl");
    else {
        *bword = chars.t_werasc;
        *refresh = chars.t_rprntc;
        *lnext = chars.t_lnextc;
    }
#endif

    bs[1] = dline[1] = bword[1] = refresh[1] = lnext[1] = '\0';
    /* Note that some systems use \0 to disable, others use \377; we must
     * check both.  Also, some seem to leave garbage in some of the fields,
     * so we'll ignore anything that isn't a control character.
     */
    if (iscntrl(*bs)      && *bs       && *bs != '\b' && *bs != '\177')
                                       add_ibind(bs,      "/DOKEY BSPC");
    if (iscntrl(*bword)   && *bword)   add_ibind(bword,   "/DOKEY BWORD");
    if (iscntrl(*dline)   && *dline)   add_ibind(dline,   "/DOKEY DLINE");
    if (iscntrl(*refresh) && *refresh) add_ibind(refresh, "/DOKEY REFRESH");
    if (iscntrl(*lnext)   && *lnext)   add_ibind(lnext,   "/DOKEY LNEXT");
}

#ifdef EMXANSI
# define CAN_GET_WINSIZE
# undef TIOCGWINSZ
#endif
#ifdef TIOCGWINSZ
# define CAN_GET_WINSIZE
#endif

int get_window_size()
{
#ifdef CAN_GET_WINSIZE
    int ocol = columns, oline = lines;

# ifdef TIOCGWINSZ
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) < 0) return 0;
    if (size.ws_col > 0) columns = size.ws_col;
    if (size.ws_row > 0) lines = size.ws_row;
# endif

# ifdef EMXANSI
    static VIOMODEINFO info;    /* must be static for thunking (16 bit func) */
    info.cb = sizeof(info);
    VioGetMode(&info,(HVIO)0);
    if (info.col > 0) columns = info.col;
    if (info.row > 0) lines = info.row;
# endif

    if (columns == ocol && lines == oline) return 1;
    setivar("wrapsize", columns - (ocol - wrapsize), FALSE);
    ch_visual();
    do_hook(H_RESIZE, NULL, "%d %d", columns, lines);
    return 1;
#else
    return 0;
#endif
}

void cbreak_noecho_mode()
{
    tty_struct tty;

    if (no_tty) return;
    if (ingetattr(&tty) < 0) die(ingetattr_error, errno);
    structcpy(old_tty, tty);

#ifdef USE_TERMIOS
    tty.c_lflag &= ~(ECHO | ICANON);
    tty.c_lflag |= ISIG;
    tty.c_iflag |= IGNBRK | IGNPAR;
    tty.c_iflag &= ~(ICRNL | INLCR | ISTRIP | IGNCR);
# ifdef OCRNL
    tty.c_oflag &= ~OCRNL;
# else
    /* OCRNL should already be off anyway */
# endif
    /* Leave ONLCR on, so write(1) and other things that blast onto the screen
     * without asking look at least somewhat sane.
     */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
#endif

#ifdef USE_HPUX_TERMIO
    tty.c_lflag &= ~(ECHO | ECHOE | ICANON);
    tty.c_iflag &= ~ICRNL;
    tty.c_oflag &= ~OCRNL;
    /* Leave ONLCR on, so write(1) and other things that blast onto the screen
     * without asking look at least somewhat sane.
     */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
#endif

#ifdef USE_SGTTY
    tty.sg_flags |= CBREAK;
    tty.sg_flags &= ~(ECHO | CRMOD);
    /* Sgtty's CRMOD is equivalent to termios' (ICRNL | OCRNL | ONLCR).
     * So to turn off icrnl and ocrnl we must also turn off onlcr.
     * This means we'll have to print '\r' ourselves in output.c, and
     * we can't do anything about making external screen writers look sane.
     */
#endif

    if (insetattr(&tty) < 0) die(insetattr_error, errno);
    is_custom_tty = 1;
}

void reset_tty()
{
    if (is_custom_tty) {
        if (insetattr(&old_tty) < 0) perror(insetattr_error);
        else is_custom_tty = 0;
    }
}
