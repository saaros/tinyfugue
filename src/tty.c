/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tty.c,v 33000.4 1994/04/16 05:10:09 hawkeye Exp $ */

/*
 * TTY driver routines.
 */

#include "config.h"
#include "port.h"

#ifdef USE_TERMIOS                    /* POSIX is the way to go. */
# include <termios.h>
# ifndef TIOCGWINSZ
#  include <sys/ioctl.h>              /* BSD needs this for TIOCGWINSZ */
# endif
# define tty_struct struct termios
# define insetattr(buf) (tcsetattr(0, TCSAFLUSH, (buf)))
# define ingetattr(buf) (tcgetattr(0, (buf)))
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
# define insetattr(buf) (ioctl(0, TCSETAF, (buf)))
# define ingetattr(buf) (ioctl(0, TCGETA, (buf)))
# define insetattr_error "TCSETAF ioctl"
# define ingetattr_error "TCGETA ioctl"
#endif

#ifdef USE_TERMIOS
# ifdef M_XENIX
#  include <sys/types.h>              /* Needed for sys/stream.h, which is... */
#  include <sys/stream.h>             /* needed for sys/ptem.h, which is... */
#  include <sys/ptem.h>               /* needed for struct winsize.  Ugh. */
# endif
#endif

#ifdef USE_SGTTY
# include <sys/ioctl.h>
# include <sgtty.h>                  /* BSD's old "new" terminal driver. */
# define tty_struct struct sgttyb
# define insetattr(buf) (ioctl(0, TIOCSETP, (buf)))
# define ingetattr(buf) (ioctl(0, TIOCGETP, (buf)))
# define insetattr_error "TIOCSETP ioctl"
# define ingetattr_error "TIOCGETP ioctl"
#endif

static tty_struct old_tty;
static int is_custom_tty = 0;        /* is tty in customized mode? */

#include <ctype.h>
#include "tf.h"
#include "util.h"
#include "tty.h"
#include "output.h"       /* for redraw() */
#include "macro.h"        /* for do_hook(), add_ibind(), H_RESIZE */

#define DEFAULT_COLUMNS 80

void init_tty()
{
    tty_struct tty;
#ifdef USE_HPUX_TERMIO
    struct ltchars chars;
#endif
#ifdef USE_SGTTY
    struct ltchars chars;
#endif

    char bs[2], dline[2], bword[2], refresh[2], lnext[2];
    bs[0] = dline[0] = bword[0] = refresh[0] = lnext[0] = '\0';

    if (ingetattr(&tty) < 0) {
        perror(ingetattr_error);
        return;
    }

#ifdef USE_TERMIOS
    *bs = tty.c_cc[VERASE];
    *dline = tty.c_cc[VKILL];
# ifdef VWERASE /* Not POSIX, but many systems have it. */
    *bword = tty.c_cc[VWERASE];
# endif
# ifdef VREPRINT /* Not POSIX, but many systems have it. */
    *refresh = tty.c_cc[VREPRINT];
# endif
# ifdef VLNEXT /* Not POSIX, but many systems have it. */
    *lnext = tty.c_cc[VLNEXT];
# endif
#endif

#ifdef USE_HPUX_TERMIO
    if (ioctl(0, TIOCGLTC, &chars) < 0) perror("TIOCGLTC ioctl");
    *bs = tty.c_cc[VERASE];
    *dline = tty.c_cc[VKILL];
    *bword = chars.t_werasc;
    *refresh = chars.t_rprntc;
#endif

#ifdef USE_SGTTY
    if (ioctl(0, TIOCGLTC, &chars) < 0) perror("TIOCGLTC ioctl");
    *bs = tty.sg_erase;
    *dline = tty.sg_kill;
    *bword = chars.t_werasc;
    *refresh = chars.t_rprntc;
    *lnext = chars.t_lnextc;
#endif

    bs[1] = dline[1] = bword[1] = refresh[1] = lnext[1] = '\0';
    /* Note that some systems use \0 to disable, others use \377; we must
     * check both.  Also, some seem to leave garbage in some of the fields,
     * so we'll ignore anything that isn't a control character.
     */
    if (iscntrl(*bs)      && *bs)      add_ibind(bs,      "/DOKEY BSPC");
    if (iscntrl(*bword)   && *bword)   add_ibind(bword,   "/DOKEY BWORD");
    if (iscntrl(*dline)   && *dline)   add_ibind(dline,   "/DOKEY DLINE");
    if (iscntrl(*refresh) && *refresh) add_ibind(refresh, "/DOKEY REFRESH");
    if (iscntrl(*lnext)   && *lnext)   add_ibind(lnext,   "/DOKEY LNEXT");

    cbreak_noecho_mode();
}

int get_window_size()
{
#ifdef TIOCGWINSZ
    extern int columns, lines;
    struct winsize size;
    int ocol, oline;

    ocol = columns;
    oline = lines;
    if (ioctl(0, TIOCGWINSZ, &size) < 0) return 0;
    if (size.ws_col) columns = size.ws_col;
    if (size.ws_row) lines = size.ws_row;
    if (columns == ocol && lines == oline) return 1;
    setivar("wrapsize", columns - (ocol - wrapsize), FALSE);
    if (visual) redraw();
    do_hook(H_RESIZE, NULL, "%d %d", columns, lines);
    return 1;
#else
    return 0;
#endif
}

void cbreak_noecho_mode()
{
    tty_struct tty;

    if (ingetattr(&tty) < 0) {
        perror(ingetattr_error);
        die("Can't get terminal mode.");
    }
    structcpy(old_tty, tty);

#ifdef USE_TERMIOS
    tty.c_lflag &= ~(ECHO | ICANON);
    tty.c_lflag |= ISIG;
    tty.c_iflag = IGNBRK | IGNPAR;
    tty.c_iflag &= ~ICRNL;
    tty.c_oflag &= ~OCRNL;
    /* Leave ONLCR on, so "write" and other things that blast onto the screen
     * look at least somewhat sane.
     */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
#endif

#ifdef USE_HPUX_TERMIO
    tty.c_lflag &= ~(ECHO | ECHOE | ICANON);
    tty.c_iflag &= ~ICRNL;
    tty.c_oflag &= ~OCRNL;
    /* Leave ONLCR on, so "write" and other things that blast onto the screen
     * look at least somewhat sane.
     */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
#endif

#ifdef USE_SGTTY
    tty.sg_flags |= CBREAK;
    tty.sg_flags &= ~(ECHO | CRMOD);
    /* Sgtty's CRMOD is equivalent to termios' (ICRNL | OCRNL | ONLCR).
     * So to turn off icrnl and ocrnl we must also turn off onlcr.
     * This means we'll have to print '\r' ourselves in output.c.
     */
#endif

    if (insetattr(&tty) < 0) {
        perror(insetattr_error);
        die("Can't set terminal mode.");
    }
    is_custom_tty = 1;
}

void reset_tty()
{
    if (is_custom_tty) {
        if (insetattr(&old_tty) < 0) perror(insetattr_error);
        else is_custom_tty = 0;
    }
}
