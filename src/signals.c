/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: signals.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

/* Signal handling, core dumps, job control, and interactive shells
 */

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

/* POSIX.1 systems should define WIFEXITED and WEXITSTATUS in sys/wait.h,
 * but apparently some don't.  Some non-posix systems define them with a 
 * union wait parameter instead of int; we can't use those.  We could test
 * them in autoconfig, but it's not worth the extra time.
 */

#ifdef _POSIX_VERSION
# include <sys/wait.h>
#else
# undef WIFEXITED
# undef WEXITSTATUS
#endif
#ifdef sequent          /* the wait macros are broken on Dynix */
# undef WIFEXITED
# undef WEXITSTATUS
#endif
#ifndef WIFEXITED
# define WIFEXITED(w)  (((*(int *)&(w)) & 0177) == 0)   /* works most places */
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(w)  ((*(int *)&(w)) >> 8)          /* works most places */
#endif


#define TEST_SIG(sig) (pending_signals & (1 << ((sig) - 1)))
#define SET_SIG(sig) (pending_signals |= (1 << ((sig) - 1)))
#define CLR_SIG(sig) (pending_signals &= ~(1 << ((sig) - 1)))
#define ZERO_SIG() (pending_signals = 0)

typedef RETSIG FDECL((SigHandler),(int sig));

static unsigned long pending_signals;
static RETSIG FDECL((*parent_tstp_handler),(int sig));

static SigHandler *FDECL(setsighandler,(int sig, SigHandler *func));
static void   NDECL(interrupt);
static RETSIG FDECL(terminate,(int sig));
static RETSIG FDECL(core_handler,(int sig));
static RETSIG FDECL(ignore_signal,(int sig));
static RETSIG FDECL(signal_scheduler,(int sig));


static SigHandler *setsighandler(sig, func)
    int sig;
    SigHandler *func;
{
#ifndef SA_RESTART
    return signal(sig, func);
#else
    /* Disable system call restarting.  We want select() to be interruptable. */
    struct sigaction act;
    SigHandler *oldfunc;

    sigaction(sig, NULL, &act);
    oldfunc = act.sa_handler;
    act.sa_flags &= ~SA_RESTART;
    act.sa_handler = func;
    sigaction(sig, &act, NULL);
    return oldfunc;
#endif
}

void init_signals()
{
    ZERO_SIG();
    setsighandler(SIGINT  , signal_scheduler);
    setsighandler(SIGTERM , terminate);
    setsighandler(SIGHUP  , terminate);
#ifdef SIGTSTP
    parent_tstp_handler = setsighandler(SIGTSTP , signal_scheduler);
#endif
#ifdef SIGWINCH
    setsighandler(SIGWINCH, signal_scheduler);
#endif
#ifdef SIGBUS /* not defined in Linux */
    setsighandler(SIGBUS  , core_handler);
#endif
    setsighandler(SIGSEGV , core_handler);
    setsighandler(SIGQUIT , core_handler);
    setsighandler(SIGILL  , core_handler);
    setsighandler(SIGTRAP , core_handler);
    setsighandler(SIGFPE  , core_handler);
    setsighandler(SIGPIPE , ignore_signal);
}

static RETSIG ignore_signal(sig)
    int sig;
{
    setsighandler(sig, ignore_signal);
}

static void interrupt()
{
    int c;

    if (visual) fix_screen();
    else clear_input_line();
    printf("C) continue; X) exit; T) disable triggers; P) kill processes\r\n");
    fflush(stdout);
    c = igetchar();
    clear_input_line();
    if (strchr("xyXY", c)) die("Interrupt, exiting.\n");
    setup_screen();
    logical_refresh();
    if (c == 't' || c == 'T') {
        setivar("borg", 0, FALSE);
        oputs("% Cyborg triggers disabled");
    } else if (c == 'p' || c == 'P') {
        kill_procs();
    }
    oflush();       /* in case of output between SIGINT receipt and handling */
}

void tog_sigquit()
{
    setsighandler(SIGQUIT, ignore_sigquit ? ignore_signal : core_handler);
}

int suspend()
{
#ifdef SIGTSTP
    if (parent_tstp_handler == SIG_DFL) {      /* true for job-control shells */
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
    if (sig != SIGQUIT) {
        printf("Core dumped - signal %d\n", sig);
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
    setsighandler(sig, signal_scheduler);
    SET_SIG(sig);                   /* set flag to deal with it later */
}

void process_signals()
{
    if (pending_signals == 0) return;

    if (TEST_SIG(SIGINT))   interrupt();
#ifdef SIGTSTP
    if (TEST_SIG(SIGTSTP))  suspend();
#endif
#ifdef SIGWINCH
    if (TEST_SIG(SIGWINCH))
        if (!get_window_size()) operror("TIOCGWINSZ ioctl");
#endif
    ZERO_SIG();
}

int interrupted()
{
    return TEST_SIG(SIGINT);
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

    if (visual) fix_screen();
    reset_tty();
#ifdef SIGTSTP
    setsighandler(SIGTSTP, parent_tstp_handler);
#endif
    result = system(cmd);
#ifdef SIGTSTP
    setsighandler(SIGTSTP, signal_scheduler);
#endif
    result = (WIFEXITED(result)) ? WEXITSTATUS(result) : -1;
    cbreak_noecho_mode();
    if (shpause) {
        oputs("% Done-- press a key to return.");
        igetchar();
    }
    get_window_size();
    setup_screen();
    do_hook(H_RESUME, "%% Resuming TinyFugue", "");
    if (maildelay > 0) check_mail();
    return result;
}
