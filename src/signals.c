/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: signals.c,v 33000.4 1994/04/19 23:45:23 hawkeye Exp $ */

/* Signal handling, core dumps, job control, and interactive shells */

#include "config.h"
#include <signal.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "process.h"
#include "tty.h"
#include "output.h"
#include "socket.h"
#include "macro.h"
#include "signals.h"

/* POSIX.1 systems should define WIFEXITED and WEXITSTATUS, taking an |int|
 * parameter, in <sys/wait.h>.  For posix systems, we use them.  For non-posix
 * systems, we use our own.  For systems which falsely claim to be posix,
 * but do not define the wait macros, we use our own.  We can not detect
 * systems which falsely claim to be posix and incorrectly define the wait
 * macros as taking a |union wait| parameter.  The workaround for such systems
 * is to change "#ifdef _POSIX_VERSION" to "#if 0" below.
 */

#ifdef _POSIX_VERSION
# include <sys/wait.h>
#else
# undef WIFEXITED
# undef WEXITSTATUS
#endif
#ifdef sequent          /* the wait macros are known to be broken on Dynix */
# undef WIFEXITED
# undef WEXITSTATUS
#endif

/* These macros can take an |int| or |union wait| parameter, but the posix
 * macros are preferred because these require specific knowledge of the
 * bit layout, which may not be correct on some systems (although most
 * unix-like systems do use this layout).
 */
#ifndef WIFEXITED
# define WIFEXITED(w)  (((*(int *)&(w)) & 0177) == 0)   /* works most places */
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(w)  ((*(int *)&(w)) >> 8)          /* works most places */
#endif

typedef RETSIG FDECL((SigHandler),(int sig));

#ifndef SIGTSTP
# define SIGTSTP 0
#endif
#ifndef SIGWINCH
# define SIGWINCH 0
#endif
#ifndef SIGBUS /* not defined in Linux */
# define SIGBUS 0
#endif

#ifndef NSIG
  /* Find an upper bound of the signals we use */
# define NSIG \
   ((SIGINT | SIGTERM | SIGHUP | SIGSEGV | SIGQUIT | SIGILL | SIGTRAP | \
     SIGFPE | SIGPIPE | SIGTSTP | SIGWINCH | SIGBUS) + 1)
#endif

VEC_TYPEDEF(sig_set, (NSIG-1));

#define SIG_SET(sig)     VEC_SET((sig), &pending_signals)
#define SIG_CLR(sig)     VEC_CLR((sig), &pending_signals)
#define SIG_ISSET(sig)   VEC_ISSET((sig), &pending_signals)
#define SIG_ZERO()       VEC_ZERO(&pending_signals)


static sig_set pending_signals;
static RETSIG FDECL((*parent_tstp_handler),(int sig));

static void   NDECL(interrupt);
static RETSIG FDECL(terminate,(int sig));
static RETSIG FDECL(core_handler,(int sig));
static RETSIG FDECL(signal_scheduler,(int sig));
#ifndef SIG_IGN
static RETSIG FDECL(SIG_IGN,(int sig));
#endif

#ifndef SA_RESTART
# define setsighandler(sig, func)  signal(sig, func)

#else

static SigHandler *FDECL(setsighandler,(int sig, SigHandler *func));

static SigHandler *setsighandler(sig, func)
    int sig;
    SigHandler *func;
{
    /* Disable system call restarting.  We want select() to be interruptable. */
    struct sigaction act;
    SigHandler *oldfunc;

    sigaction(sig, NULL, &act);
    oldfunc = act.sa_handler;
    act.sa_flags &= ~SA_RESTART;
    act.sa_handler = func;
    sigaction(sig, &act, NULL);
    return oldfunc;
}

#endif /* SA_RESTART */


void init_signals()
{
    SIG_ZERO();
    setsighandler(SIGINT  , signal_scheduler);
    setsighandler(SIGTERM , terminate);
    setsighandler(SIGHUP  , terminate);
#if SIGTSTP
    parent_tstp_handler = setsighandler(SIGTSTP , signal_scheduler);
#endif
#if SIGWINCH
    setsighandler(SIGWINCH, signal_scheduler);
#endif
#if SIGBUS /* not defined in Linux */
    setsighandler(SIGBUS  , core_handler);
#endif
    setsighandler(SIGSEGV , core_handler);
    setsighandler(SIGQUIT , core_handler);
    setsighandler(SIGILL  , core_handler);
    setsighandler(SIGTRAP , core_handler);
    setsighandler(SIGFPE  , core_handler);
    setsighandler(SIGPIPE , SIG_IGN);
}

#ifndef SIG_IGN
static RETSIG SIG_IGN(sig)
    int sig;
{
    setsighandler(sig, SIG_IGN);  /* restore handler, for SYSV */
}
#endif

static void interrupt()
{
    int c;

    if (visual) fix_screen();
    else clear_input_line();
    printf("C) continue; X) exit; T) disable triggers; P) kill processes\r\n");
    fflush(stdout);
    c = igetchar();
    clear_input_line();
    if (strchr("xyXY", c)) die("Interrupt, exiting.\r\n");
    setup_screen();
    logical_refresh();
    if (ucase(c) == 'T') {
        setivar("borg", 0, FALSE);
        oputs("% Cyborg triggers disabled");
    } else if (ucase(c) == 'P') {
        kill_procs();
    }
    oflush();       /* in case of output between SIGINT receipt and handling */
}

void tog_sigquit()
{
    setsighandler(SIGQUIT, ignore_sigquit ? SIG_IGN : core_handler);
}

int suspend()
{
#if SIGTSTP
    if (parent_tstp_handler == SIG_DFL) {      /* true for job-control shells */
        oflush();
        if (visual) fix_screen();
        reset_tty();
        kill(getpid(), SIGSTOP);
        cbreak_noecho_mode();
        get_window_size();
        setup_screen();
        logical_refresh();
        if (maildelay > 0) check_mail();
        return 1;
    }
#endif
    oputs("% Job control not supported.");
    return 0;
}

static RETSIG core_handler(sig)
    int sig;
{
    setsighandler(sig, SIG_DFL);
    cleanup();
    printf("Core dumped - signal %d\n", sig);
    if (sig != SIGQUIT) {
        puts("Please report this and any preceeding messages to");
        puts("the author, after reading \"/help core\" or README.");
    }
    kill(getpid(), sig);
}

static RETSIG terminate(sig)
    int sig;
{
    setsighandler(sig, SIG_DFL);
    cleanup();
    printf("Terminating - signal %d\n", sig);
    kill(getpid(), sig);
}

static RETSIG signal_scheduler(sig)
    int sig;
{
    setsighandler(sig, signal_scheduler);  /* restore handler, for SYSV */
    SIG_SET(sig);                          /* set flag to deal with it later */
}

void process_signals()
{
    if (SIG_ISSET(SIGINT))   interrupt();
#if SIGTSTP
    if (SIG_ISSET(SIGTSTP))  suspend();
#endif
#if SIGWINCH
    if (SIG_ISSET(SIGWINCH))
        if (!get_window_size()) operror("TIOCGWINSZ ioctl");
#endif
    SIG_ZERO();
}

int interrupted()
{
    return SIG_ISSET(SIGINT);
}

void core(why)
    CONST char *why;
{
    cleanup();
    puts(why);
    setsighandler(SIGQUIT, SIG_DFL);
    kill(getpid(), SIGQUIT);
}

int shell(cmd)
    char *cmd;
{
    int result;

    oflush();
    if (visual) fix_screen();
    reset_tty();
#if SIGTSTP
    setsighandler(SIGTSTP, parent_tstp_handler);
#endif
    result = system(cmd);
#if SIGTSTP
    setsighandler(SIGTSTP, signal_scheduler);
#endif
    /* If the next line causes errors like "request for member `w_S' in
     * something not a structure or union", then <sys/wait.h> must have
     * defined WIFEXITED and WEXITSTATUS incorrectly (violating Posix.1).
     * The workaround is to not #include <sys/wait.h> at the top of this
     * file, so we can use our own definitions.
     */
    result = (WIFEXITED(result)) ? WEXITSTATUS(result) : -1;
    cbreak_noecho_mode();
    if (shpause) {
        oputs("% Press any key to continue.");
        igetchar();
    }
    get_window_size();
    setup_screen();
    do_hook(H_RESUME, "%% Resuming TinyFugue", "");
    if (maildelay > 0) check_mail();
    return result;
}
