/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: signals.c,v 35004.14 1997/03/27 01:04:43 hawkeye Exp $ */

/* Signal handling, core dumps, job control, and interactive shells */

#include "config.h"
#include <signal.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "world.h" /* for process.h */
#include "process.h"
#include "tty.h"
#include "output.h"
#include "signals.h"
#include "variable.h"

#ifdef TF_AIX_DECLS
struct rusage *dummy_struct_rusage;
union wait *dummy_union_wait;
#endif

/* POSIX.1 systems should define WIFEXITED and WEXITSTATUS, taking an |int|
 * parameter, in <sys/wait.h>.  For posix systems, we use them.  For non-posix
 * systems, we use our own.  For systems which falsely claim to be posix,
 * but do not define the wait macros, we use our own.  We can not detect
 * systems which falsely claim to be posix and incorrectly define the wait
 * macros as taking a |union wait| parameter.  The workaround for such systems
 * is to change "#ifdef _POSIX_VERSION" to "#if 0" below.
 */

#ifdef _POSIX_VERSION
# include <sys/types.h>
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
# define WIFEXITED(w)  (((*(int *)&(w)) & 0xFF) == 0)   /* works most places */
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(w)  (((*(int *)&(w)) >> 8) & 0xFF) /* works most places */
#endif

typedef RETSIG FDECL((SigHandler),(int sig));

#ifndef HAVE_raise
# ifdef HAVE_kill
#  define raise(sig) kill(getpid(), sig)
# endif
#endif

#ifdef SIGABRT
# define ABORT SIGABRT
#else
# ifdef SIGQUIT
#  define ABORT SIGQUIT
# else
#  define ABORT SIGTERM
# endif
#endif

/* Zero out undefined signals, so we don't have to #ifdef everything later. */
#ifndef SIGHUP
# define SIGHUP 0
#endif
#ifndef SIGTRAP
# define SIGTRAP 0
#endif
#ifndef SIGABRT
# define SIGABRT 0
#endif
#ifndef SIGBUS /* not defined in Linux */
# define SIGBUS 0
#endif
#ifndef SIGPIPE
# define SIGPIPE 0
#endif
#ifndef SIGUSR1
# define SIGUSR1 0
#endif
#ifndef SIGUSR2
# define SIGUSR2 0
#endif
#ifndef SIGTSTP
# define SIGTSTP 0
#endif
#ifndef SIGWINCH
# define SIGWINCH 0
#endif

#ifndef NSIG
  /* Find an upper bound of the signals we use */
# define NSIG \
   ((SIGHUP  | SIGINT  | SIGQUIT | SIGILL  | SIGTRAP | SIGABRT | SIGFPE  | \
     SIGBUS  | SIGSEGV | SIGPIPE | SIGTERM | SIGUSR1 | SIGUSR2 | SIGTSTP | \
     SIGWINCH) + 1)
#endif

VEC_TYPEDEF(sig_set, (NSIG-1));


static sig_set pending_signals;
static RETSIG FDECL((*parent_tstp_handler),(int sig));

static void   NDECL(handle_interrupt);
static void   FDECL(terminate,(int sig));
static void   NDECL(coremsg);
static RETSIG FDECL(core_handler,(int sig));
static RETSIG FDECL(signal_scheduler,(int sig));
#ifndef SIG_IGN
static RETSIG FDECL(SIG_IGN,(int sig));
#endif


static SigHandler *FDECL(setsighandler,(int sig, SigHandler *func));

/* HAVE_sigaction doesn't mean we NEED_sigaction.  On some systems that have
 * it, struct sigaction will not get defined unless _POSIX_SOURCE or similar
 * is defined, so it's best to avoid it if we don't need it.
 */
#ifdef SA_RESTART
# define NEED_sigaction
#endif
#ifdef SA_ACK
# define NEED_sigaction
#endif

static SigHandler *setsighandler(sig, func)
    int sig;
    SigHandler *func;
{
    if (!sig) return NULL;
#ifndef NEED_sigaction
    return signal(sig, func);
#else
    {
        struct sigaction act;
        SigHandler *oldfunc;

        sigaction(sig, NULL, &act);
        oldfunc = act.sa_handler;
# ifdef SA_RESTART
        /* Disable system call restarting, so select() is interruptable. */
        act.sa_flags &= ~SA_RESTART;
# endif
# ifdef SA_ACK
        /* Disable OS2 SA_ACK, so signals can be re-installed POSIX-style. */
        act.sa_flags &= ~SA_ACK;
# endif
        act.sa_handler = func;
        sigaction(sig, &act, NULL);
        return oldfunc;
    }
#endif /* HAVE_sigaction */
}


void init_signals()
{
    VEC_ZERO(&pending_signals);

    setsighandler(SIGHUP  , signal_scheduler);
    setsighandler(SIGINT  , signal_scheduler);
    setsighandler(SIGQUIT , core_handler);
    setsighandler(SIGILL  , core_handler);
    setsighandler(SIGTRAP , core_handler);
    setsighandler(SIGABRT , core_handler);
    setsighandler(SIGFPE  , core_handler);
    setsighandler(SIGBUS  , core_handler);
    setsighandler(SIGSEGV , core_handler);
    setsighandler(SIGPIPE , SIG_IGN);
    setsighandler(SIGTERM , signal_scheduler);
    setsighandler(SIGUSR1 , signal_scheduler);
    setsighandler(SIGUSR2 , signal_scheduler);
    parent_tstp_handler = setsighandler(SIGTSTP , signal_scheduler);
    setsighandler(SIGWINCH, signal_scheduler);

}

#ifndef SIG_IGN
static RETSIG SIG_IGN(sig)
    int sig;
{
    setsighandler(sig, SIG_IGN);  /* restore handler (POSIX) */
}
#endif

static void handle_interrupt()
{
    int c;
    extern int no_tty;

    if (no_tty)
        die("Interrupt, exiting.", 0);
    fix_screen();
    puts("C) continue tf; X) exit; T) disable triggers; P) kill processes\r");
    fflush(stdout);
    c = igetchar();
    if (ucase(c) == 'X')
        die("Interrupt, exiting.", 0);
    setup_screen(0);
    if (ucase(c) == 'T') {
        setivar("borg", 0, FALSE);
        oputs("% Cyborg triggers disabled.");
    } else if (ucase(c) == 'P') {
        kill_procs();
    }
    oputs("% Resuming TinyFugue.");
}

int suspend()
{
#if SIGTSTP
    if (parent_tstp_handler == SIG_DFL) {      /* true for job-control shells */
        check_mail();
        fix_screen();
        reset_tty();
        raise(SIGSTOP);
        cbreak_noecho_mode();
        get_window_size();
        setup_screen(-1);
        check_mail();
        return 1;
    }
#endif
    oputs("% Job control not supported.");
    return 0;
}


static RETSIG core_handler(sig)
    int sig;
{
    setsighandler(sig, core_handler);  /* restore handler (POSIX) */

    if (sig == SIGQUIT) {
        fix_screen();
        puts("SIGQUIT received.  Dump core?  (y/n)\r");
        fflush(stdout);
        if (igetchar() != 'y') {
            setup_screen(0);
            oputs("% Resuming TinyFugue.");
            return;
        }
        fputs("Abnormal termination - SIGQUIT\n", stderr);
    }
    setsighandler(sig, SIG_DFL);
    panic_fix_screen();
    reset_tty();
    if (sig != SIGQUIT) {
        coremsg();
        fprintf(stderr, "> Abnormal termination - signal %d\n\n", sig);
#ifdef PLATFORM_UNIX
        fputs("If you can, follow these instructions to get a stack trace.\n",
            stderr);
        fputs("If you haven't already done so, in the \"Config\" file set\n",
            stderr);
        fputs("CCFLAGS='-g' and STRIP='', and run \"make\" again.  Then do:\n",
            stderr);
        fputs("\n", stderr);
        fputs("cd src\n", stderr);
        fputs("script\n", stderr);
        fputs("gdb -q tf   ;# if gdb is unavailable, use \"dbx tf\" instead.\n",
            stderr);
        fputs("run\n", stderr);
        fputs("(do whatever is needed to reproduce the core dump)\n", stderr);
        fputs("where\n", stderr);
        fputs("quit\n", stderr);
        fputs("exit\n", stderr);
        fputs("\n", stderr);
        fputs("and mail the \"typescript\" file to the address above.\n",
            stderr);
        fputs("\n", stderr);
#else
        fputs("If you can, get a stack trace and send it to the author.\n",
            stderr);
#endif
        fputs("If you can't or don't get a stack trace, at least describe\n",
            stderr);
        fputs("what you were doing at the time of this crash.\n", stderr);
    }
    raise(sig);
}

void crash(internal, fmt, file, line, n)
    CONST char *fmt, *file;
    int internal, line;
    long n;
{
    setsighandler(SIGQUIT, SIG_DFL);
    panic_fix_screen();
    reset_tty();
    if (internal) coremsg();
    fprintf(stderr, "> %s:  %s, line %d\n",
        internal ? "Internal error" : "Aborting", file, line);
    fprintf(stderr, "> TERM=%s, visual=%ld, emulation=%ld, lp=%ld\n",
        TERM, visual, emulation, lpflag);
    fputs("> ", stderr);
    fprintf(stderr, fmt, n);
    fputs("\n\n", stderr);
    raise(SIGQUIT);
}

static void coremsg()
{
    extern CONST char version[], sysname[];
    fputs("Please report this message verbatim to hawkeye@tcp.com.\n", stderr);
    fputs("Also describe what you were doing in tf when this\n", stderr);
    fputs("occured, and if you can repeat it.\n\n", stderr);
    fprintf(stderr, "> %s\n", version);
    if (*sysname) fprintf(stderr, "> %s\n", sysname);
}

static void terminate(sig)
    int sig;
{
    setsighandler(sig, SIG_DFL);
    fix_screen();
    reset_tty();
    fprintf(stderr, "Terminating - signal %d\n", sig);
    raise(sig);
}

static RETSIG signal_scheduler(sig)
    int sig;
{
    setsighandler(sig, signal_scheduler);  /* restore handler (POSIX) */
    VEC_SET(sig, &pending_signals);        /* set flag to deal with it later */
}

void process_signals()
{
    if (VEC_ISSET(SIGINT, &pending_signals))   handle_interrupt();
    if (VEC_ISSET(SIGTSTP, &pending_signals))  suspend();
    if (VEC_ISSET(SIGWINCH, &pending_signals))
        if (!get_window_size()) operror("TIOCGWINSZ ioctl");
    if (VEC_ISSET(SIGHUP, &pending_signals))   do_hook(H_SIGHUP, NULL, "");
    if (VEC_ISSET(SIGTERM, &pending_signals))  do_hook(H_SIGTERM, NULL, "");
    if (VEC_ISSET(SIGUSR1, &pending_signals))  do_hook(H_SIGUSR1, NULL, "");
    if (VEC_ISSET(SIGUSR2, &pending_signals))  do_hook(H_SIGUSR2, NULL, "");

    if (VEC_ISSET(SIGHUP, &pending_signals))   terminate(SIGHUP);
    if (VEC_ISSET(SIGTERM, &pending_signals))  terminate(SIGTERM);

    VEC_ZERO(&pending_signals);
}

int interrupted()
{
    return VEC_ISSET(SIGINT, &pending_signals);
}

int shell_status(result)
    int result;
{
    /* If the next line causes errors like "request for member `w_S' in
     * something not a structure or union", then <sys/wait.h> must have
     * defined WIFEXITED and WEXITSTATUS incorrectly (violating Posix.1).
     * The workaround is to not #include <sys/wait.h> at the top of this
     * file, so we can use our own definitions.
     */
    return (WIFEXITED(result)) ? WEXITSTATUS(result) : -1;
}

int shell(cmd)
    CONST char *cmd;
{
    int result;

    check_mail();
    fix_screen();
    reset_tty();
    setsighandler(SIGTSTP, parent_tstp_handler);
    result = system(cmd);
    setsighandler(SIGTSTP, signal_scheduler);
    cbreak_noecho_mode();
    if (result == -1) {
        eprintf("%s", strerror(errno));
    } else if (shpause) {
        oputs("% Press any key to continue.");
        oflush();
        igetchar();
    }
    get_window_size();
    setup_screen(-1);
    if (result == -1) return result;
    do_hook(H_RESUME, "%% Resuming TinyFugue.", "");
    check_mail();
#ifdef PLATFORM_OS2
    return result;
#else /* UNIX */
    return shell_status(result);
#endif
}
