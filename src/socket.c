/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: socket.c,v 35004.120 1999/01/31 00:27:53 hawkeye Exp $ */


/***************************************************************
 * Fugue socket handling
 *
 * Written by Ken Keys
 * Reception and transmission through sockets is handled here.
 * This module also contains the main loop.
 * Multiple sockets handled here.
 * Autologin handled here.
 ***************************************************************/

#include "config.h"
#include <sys/types.h>
#ifdef SYS_SELECT_H
# include SYS_SELECT_H
#endif
/* #include <sys/time.h> */
#include <fcntl.h>
#include <sys/file.h>	/* for FNONBLOCK on SVR4, hpux, ... */
#include <sys/socket.h>

#ifdef NETINET_IN_H
# include NETINET_IN_H
#else
/* Last resort - we'll assume the "normal" stuff. */
struct in_addr {
	unsigned long	s_addr;
};
struct sockaddr_in {
	short		sin_family;
	unsigned short	sin_port;
	struct in_addr	sin_addr;
	char		sin_zero[8];
};
#define	htons(x)	(x)	/* assume big-endian machine */
#endif

#ifdef ARPA_INET_H
# include ARPA_INET_H
#endif

#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "tfselect.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "output.h"
#include "process.h"
#include "macro.h"
#include "keyboard.h"
#include "commands.h"
#include "command.h"
#include "signals.h"
#include "search.h"
#include "variable.h"	/* set_var_by_*() */

#ifdef _POSIX_VERSION
# include <sys/wait.h>
#endif

#define in_connect(s,addr) connect(s, (struct sockaddr*)(addr), sizeof(*(addr)))

#ifndef NO_NETDB

# ifdef PLATFORM_OS2
#  define NONBLOCKING_GETHOST
# endif

# ifdef PLATFORM_UNIX
#  ifndef __CYGWIN32__
#   ifdef HAVE_waitpid
#    define NONBLOCKING_GETHOST
#   endif
#  endif
# endif

# include NETDB_H
# ifdef NONBLOCKING_GETHOST
   static void FDECL(waitforhostname,(int fd, CONST char *name));
   static int FDECL(nonblocking_gethost,(CONST char *name,
       struct in_addr *sin_addr, long *pidp, CONST char **what));
# endif
  static int FDECL(blocking_gethost,(CONST char *name,
       struct in_addr *sin_addr, int *errp));

#else /* NO_NETDB */
# define nonblocking_gethost(name, sin_addr, pidp, what) (-1)
# define blocking_gethost(name, sin_addr, errp) (-1)
#endif /* NO_NETDB */

#ifndef INADDR_NONE
# define INADDR_NONE 0xffffffff     /* should be in <netinet/in.h> */
#endif

#ifdef HAVE_h_errno
  extern int h_errno;
#else
# define h_errno 1
#endif

#ifndef HAVE_hstrerror
static CONST char *h_errlist[] = {
    "Error 0",
    "Unknown host",
    "Host name lookup failure",
    "Unknown server error",
    "No address associated with name"
};
# define hstrerror(err)  ((err) <= 4 ? h_errlist[(err)] : "unknown error")
#endif


/* Nonblocking connect.
 * Legend for comment columns:
 *      W = works, N = not defined, F = defined but fails, ? = unknown
 * Nonblocking connect will work on a system if the column contains a 'W'
 * and there is no 'F' above it; 'N' does not matter.  The order of the
 * tests is arranged to keep the 'F's below the 'W's.
 * 
 *                                                        S
 *                                                        o
 *                                      P           L  S  l     H
 *                                      O     S     i  u  a  I  P
 *                                      S  B  Y  A  n  n  r  R  /
 *                                      I  S  S  I  u  O  i  I  U
 *                                      X  D  V  X  x  S  s  X  X
 */
#ifdef FNONBLOCK                     /* N  ?  W  W  N  ?  W  W  W */
# define TF_NONBLOCK FNONBLOCK
#else
# ifdef O_NONBLOCK                   /* W  ?  ?  W  W  ?  W  W  ? */
#  define TF_NONBLOCK O_NONBLOCK
# else
#  ifdef FNDELAY                     /* ?  W  ?  F  W  ?  W  W  ? */
#   define TF_NONBLOCK FNDELAY
#  else
#   ifdef O_NDELAY                   /* ?  W  ?  F  W  ?  W  W  ? */
#    define TF_NONBLOCK O_NDELAY
#   else
#    ifdef FNBIO                     /* ?  ?  W  N  N  F  N  N  ? */
#     define TF_NONBLOCK FNBIO
#    else
#     ifdef FNONBIO                  /* ?  ?  ?  N  N  F  N  N  ? */
#      define TF_NONBLOCK FNONBIO
#     else
#      ifdef FNONBLK                 /* ?  ?  ?  N  N  ?  N  W  ? */
#       define TF_NONBLOCK FNONBLK
#      else
#       define TF_NONBLOCK 0
#      endif
#     endif
#    endif
#   endif
#  endif
# endif
#endif


#ifdef TF_AIX_DECLS
extern int FDECL(connect,(int, struct sockaddr *, int));
#endif

#ifdef SOCKS
# ifndef SOCKS_NONBLOCK
#  define TF_NONBLOCK 0
# endif
#endif

#define SOCKDEAD	00001	/* connection dead */
#define SOCKRESOLVING	00002	/* hostname not yet resolved */
#define SOCKPENDING	00004	/* connection not yet established */
#define SOCKLOGIN	00010	/* autologin requested by user */
#define SOCKPROMPT	00040	/* last prompt was definitely a prompt */
#define SOCKTELNET	00100	/* server supports telnet protocol */
#define SOCKPROXY	00200	/* indirect connection through proxy server */

VEC_TYPEDEF(telnet_opts, 256);

typedef struct Sock {		/* an open connection to a server */
    int fd;			/* socket to server, or pipe to name resolver */
    CONST char *host, *port;	/* server address, human readable */
    struct sockaddr_in addr;	/* server address, internal */
    telnet_opts tn_do, tn_will;	/* telnet DO and WILL options */
    short flags;		/* SOCK* flags */
    short numquiet;		/* # of lines to gag after connecting */
    struct World *world;	/* world to which socket is connected */
    struct Sock *next, *prev;	/* next/prev sockets in linked list */
    Aline *prompt;		/* prompt from server */
    Stringp buffer;		/* buffer for incoming characters */
    struct Queue *queue;	/* buffer for undisplayed lines */
    int activity;		/* number of undisplayed lines */
    attr_t attrs;		/* current text attributes */
    TIME_T time[2];		/* time of last receive/send */
    char state;			/* state of parser finite state automaton */
    long pid;			/* pid of name resolution process */
} Sock;


static Sock *FDECL(find_sock,(CONST char *name));
static void  FDECL(wload,(World *w));
static int   FDECL(fg_sock,(Sock *sock, int quiet));
static int   FDECL(get_host_address,(CONST char *name, struct in_addr *sin_addr, long *pidp, CONST char **what, int *errp));
static int   FDECL(openconn,(Sock *new));
static int   FDECL(establish,(Sock *new));
static void  NDECL(fg_live_sock);
static void  NDECL(nuke_dead_socks);
static void  FDECL(nukesock,(Sock *sock));
static void  FDECL(handle_prompt,(CONST char *str, int confirmed));
static void  FDECL(unprompt,(Sock *sock, int update));
static void  NDECL(handle_socket_line);
static int   NDECL(handle_socket_input);
static int   FDECL(transmit,(CONST char *s, unsigned int len));
static void  FDECL(telnet_send,(String *cmd));
static void  FDECL(f_telnet_recv,(int cmd, int opt));
static int   FDECL(is_quiet,(CONST char *str));
static int   FDECL(is_bamf,(CONST char *str));
static void  FDECL(do_naws,(Sock *sock));
static void  FDECL(telnet_debug,(CONST char *dir, CONST char *str, int len));
static void  NDECL(preferred_telnet_options);

#define telnet_recv(cmd, opt)	f_telnet_recv((UCHAR)cmd, (UCHAR)opt);
#define killsock(s)		(((s)->flags |= SOCKDEAD), dead_socks++)

#ifndef CONN_WAIT
#define CONN_WAIT 400000
#endif

#ifndef PROC_WAIT
#define PROC_WAIT 100000
#endif

#define SPAM (4*1024)		/* break loop if this many chars are received */

static fd_set readers;		/* input file descriptors */
static fd_set active;		/* active file descriptors */
static fd_set writers;		/* pending connections */
static fd_set connected;	/* completed connections */
static int nfds;		/* max # of readers/writers */
static Sock *hsock = NULL;	/* head of socket list */
static Sock *tsock = NULL;	/* tail of socket list */
static Sock *fsock = NULL;	/* foreground socket */
static int dead_socks = 0;	/* Number of unnuked dead sockets */
static CONST char *telnet_label[0x100];
STATIC_BUFFER(telbuf);

#define MAXQUIET        25	/* max # of lines to suppress during login */

/* TELNET options (RFC 855) */
#define TN_BINARY	'\000'	/* RFC  856 - transmit binary */
#define TN_ECHO		'\001'	/* RFC  857 - echo */
#define TN_SGA		'\003'	/* RFC  858 - suppress GOAHEAD option */
#define TN_STATUS	'\005'	/* RFC  859 - not used */
#define TN_TIMING_MARK	'\006'	/* RFC  860 - not used */
#define TN_TTYPE	'\030'	/* RFC 1091 - not used */
#define TN_SEND_EOR	'\031'	/* RFC  885 - transmit EOR */
#define TN_NAWS		'\037'	/* RFC 1073 - negotiate about window size */
#define TN_TSPEED	'\040'	/* RFC 1079 - not used */
#define TN_LINEMODE	'\042'	/* RFC 1184 - not used */
/* Note: many telnet servers (incorrectly?) send DO ECHO and DO SGA together
 * to mean character-at-a-time mode.
 */

/* TELNET special characters (RFC 854) */
#define TN_EOR		'\357'	/* End-Of-Record */
#define TN_SE		'\360'	/* not used */
#define TN_NOP		'\361'	/* not used */
#define TN_DATA_MARK	'\362'	/* not used */
#define TN_BRK		'\363'	/* not used */
#define TN_IP		'\364'	/* not used */
#define TN_AO		'\365'	/* not used */
#define TN_AYT		'\366'	/* not used */
#define TN_EC		'\367'	/* not used */
#define TN_EL		'\370'	/* not used */
#define TN_GA		'\371'	/* Go Ahead */
#define TN_SB		'\372'	/* not used */
#define TN_WILL		'\373'	/* I offer to ~, or ack for DO */
#define TN_WONT		'\374'	/* I will stop ~ing, or nack for DO */
#define TN_DO		'\375'	/* Please do ~?, or ack for WILL */
#define TN_DONT		'\376'	/* Stop ~ing!, or nack for WILL */
#define TN_IAC		'\377'	/* telnet Is A Command character */

#define UCHAR		unsigned char

#define IS_DO(sock, opt)	VEC_ISSET((UCHAR)(opt), &(sock)->tn_do)
#define IS_WILL(sock, opt)	VEC_ISSET((UCHAR)(opt), &(sock)->tn_will)

#define tn_send_opt(cmd, opt) \
    ( Sprintf(telbuf, 0, "%c%c%c", TN_IAC, (cmd), (opt)), telnet_send(telbuf) )

#define set_DO(opt)	( VEC_SET((UCHAR)(opt), &xsock->tn_do) )
#define set_DONT(opt)	( VEC_CLR((UCHAR)(opt), &xsock->tn_do) )
#define set_WILL(opt)	( VEC_SET((UCHAR)(opt), &xsock->tn_will) )
#define set_WONT(opt)	( VEC_CLR((UCHAR)(opt), &xsock->tn_will) )

#define DO(opt)		( set_DO(opt),   tn_send_opt(TN_DO,   (opt)) )
#define DONT(opt)	( set_DONT(opt), tn_send_opt(TN_DONT, (opt)) )
#define WILL(opt)	( set_WILL(opt), tn_send_opt(TN_WILL, (opt)) )
#define WONT(opt)	( set_WONT(opt), tn_send_opt(TN_WONT, (opt)) )

#define ANSI_CSI	'\233'	/* ANSI terminal Command Sequence Intro */

Sock *xsock = NULL;		/* current (transmission) socket */
int quit_flag = FALSE;		/* Are we all done? */
int active_count = 0;		/* # of (non-current) active sockets */
Aline *incoming_text = NULL;

#define CONFAIL(where, what, why) \
        do_hook(H_CONFAIL, "%% Connection to %s failed: %s: %s", "%s %s: %s", \
        (where), (what), (why))

/* initialize socket.c data */
void init_sock()
{
    int i;

    FD_ZERO(&readers);
    FD_ZERO(&active);
    FD_ZERO(&writers);
    FD_ZERO(&connected);
    FD_SET(STDIN_FILENO, &readers);
    nfds = 1;

    set_var_by_id(VAR_async_conn, !!TF_NONBLOCK, NULL);

    for (i = 0; i < 0x100; i++) telnet_label[i] = NULL;

    telnet_label[(UCHAR)TN_BINARY]	= "BINARY";
    telnet_label[(UCHAR)TN_ECHO]	= "ECHO";
    telnet_label[(UCHAR)TN_SGA]		= "SGA";
    telnet_label[(UCHAR)TN_STATUS]	= "STATUS";
    telnet_label[(UCHAR)TN_TIMING_MARK]	= "TIMING_MARK";
    telnet_label[(UCHAR)TN_TTYPE]	= "TTYPE";
    telnet_label[(UCHAR)TN_SEND_EOR]	= "SEND_EOR";
    telnet_label[(UCHAR)TN_NAWS]	= "NAWS";
    telnet_label[(UCHAR)TN_TSPEED]	= "TSPEED";
    telnet_label[(UCHAR)TN_LINEMODE]	= "LINEMODE";
    telnet_label[(UCHAR)TN_EOR]		= "EOR";
    telnet_label[(UCHAR)TN_SE]		= "SE";
    telnet_label[(UCHAR)TN_NOP]		= "NOP";
    telnet_label[(UCHAR)TN_DATA_MARK]	= "DATA_MARK";
    telnet_label[(UCHAR)TN_BRK]		= "BRK";
    telnet_label[(UCHAR)TN_IP]		= "IP";
    telnet_label[(UCHAR)TN_AO]		= "AO";
    telnet_label[(UCHAR)TN_AYT]		= "AYT";
    telnet_label[(UCHAR)TN_EC]		= "EC";
    telnet_label[(UCHAR)TN_EL]		= "EL";
    telnet_label[(UCHAR)TN_GA]		= "GA";
    telnet_label[(UCHAR)TN_SB]		= "SB";
    telnet_label[(UCHAR)TN_WILL]	= "WILL";
    telnet_label[(UCHAR)TN_WONT]	= "WONT";
    telnet_label[(UCHAR)TN_DO]		= "DO";
    telnet_label[(UCHAR)TN_DONT]	= "DONT";
    telnet_label[(UCHAR)TN_IAC]		= "IAC";
}

/* main_loop
 * Here we mostly sit in select(), waiting for something to happen.
 * The select timeout is set for the earliest process, mail check,
 * or refresh event.  Signal processing and garbage collection is
 * done at the beginning of each loop, where we're in a "clean" state.
 */
void main_loop()
{
    static struct timeval now;    /* static, for recursion */
    static TIME_T earliest;       /* static, for recursion */
    Sock *sock = NULL;            /* loop index */
    int count, received;
    Sock *stopsock;
    static int depth = 0;
    struct timeval tv, *tvp;
    struct timeval refresh_tv;

    depth++;
    while (!quit_flag) {
        if (depth > 1 && interrupted()) break;

        /* deal with pending signals */
        /* at loop beginning in case of signals before main_loop() */ 
        process_signals();

        /* run processes */
        /* at loop beginning in case of processes before main_loop() */ 
        gettime(&now);
        if (proctime && proctime <= now.tv_sec) runall();

        if (low_memory_warning) {
            low_memory_warning = 0;
            oputs("% WARNING: memory is low.  Try reducing history sizes.");
        }

        if (quit_flag) break;

        /* figure out when next event is so select() can timeout then */
        gettime(&now);
        earliest = proctime;
        if (maildelay > 0) {
            if (now.tv_sec >= mail_update) {
                check_mail();
                mail_update = now.tv_sec + maildelay;
            }
            if (!earliest || (mail_update < earliest))
                earliest = mail_update;
        }
        if (visual && clock_update >= 0) {
            if (now.tv_sec >= clock_update)
                update_status_field(NULL, STAT_CLOCK);
            if (!earliest || (clock_update < earliest))
                earliest = clock_update;
        }

        /* flush pending tfscreen output */
        /* must be after all possible output and before select() */
        oflush();

        if (pending_input || pending_line) {
            tvp = &tv;
            tv.tv_sec = 0;
            tv.tv_usec = 0;
        } else if (earliest) {
            tvp = &tv;
            tv.tv_sec = earliest - now.tv_sec;
            tv.tv_usec = 0 - now.tv_usec;
            if (tv.tv_usec < 0) {
                tv.tv_usec += 1000000;
                tv.tv_sec -= 1;
            }
            if (tv.tv_sec <= 0) {
                tv.tv_sec = 0;
#ifndef HAVE_gettimeofday
            } else if (proctime) {
                if ((--tv.tv_sec) == 0)
                    tv.tv_usec = PROC_WAIT;
#endif
            }
        } else tvp = NULL;

        if (need_refresh) {
            if (!visual) {
                refresh_tv.tv_sec = refreshtime / 1000000;
                refresh_tv.tv_usec = refreshtime % 1000000;
            } else {
                refresh_tv.tv_sec = refresh_tv.tv_usec = 0;
            }

            if (!tvp || refresh_tv.tv_sec < tvp->tv_sec ||
                (refresh_tv.tv_sec == tvp->tv_sec &&
                refresh_tv.tv_usec < tvp->tv_usec))
            {
                tvp = &refresh_tv;
            }
        }

        if (visual && need_more_refresh) {
            if (!tvp || 1 < tvp->tv_sec ||
                (1 == tvp->tv_sec && 0 < tvp->tv_usec))
            {
                refresh_tv.tv_sec = 1;
                refresh_tv.tv_usec = 0;
                tvp = &refresh_tv;
            }
        }

        /* Wait for next event.
         *   descriptor read:	user input, socket input, or /quote !
         *   descriptor write:	nonblocking connect()
         *   timeout:		time for runall() or do_refresh()
         * Note: if the same descriptor appears in more than one fd_set, some
         * systems count it only once, some count it once for each occurance.
         */
        structcpy(active, readers);
        structcpy(connected, writers);
        count = tfselect(nfds, &active, &connected, NULL, tvp);

        if (count < 0) {
            /* select() must have exited due to error or interrupt. */
            if (errno != EINTR)
                core(strerror(errno), __FILE__, __LINE__, 0);
            /* In case we're in a kb tfgetS(), clear things for parent loop. */
            FD_ZERO(&active);
            FD_ZERO(&connected);

        } else {
            if (count == 0) {
                /* select() must have exited due to timeout. */
                do_refresh();
            }

            /* check for user input */
            if (pending_input || FD_ISSET(STDIN_FILENO, &active)) {
                if (FD_ISSET(STDIN_FILENO, &active)) count--;
                do_refresh();
                if (!handle_keyboard_input(FD_ISSET(STDIN_FILENO, &active))) {
                    /* input is at EOF, stop reading it */
                    FD_CLR(STDIN_FILENO, &readers);
                }
            }

            /* Check for socket completion/activity.  We pick up where we
             * left off last time, so sockets near the end of the list aren't
             * starved.  We stop when we've gone through the list once, or
             * when we've received a lot of data (so spammy sockets don't
             * degrade interactive response too much).
             */
            if (hsock) {
                received = 0;
                if (!sock) sock = hsock;
                stopsock = sock;
                do /* while (count && sock != stopsock && received < SPAM) */ {
                    xsock = sock;
                    if (sock->flags & SOCKDEAD) {
                        /* do nothing */
                    } else if (FD_ISSET(xsock->fd, &connected)) {
                        count--;
                        establish(xsock);
                    } else if (FD_ISSET(xsock->fd, &active)) {
                        count--;
                        if (xsock->flags & SOCKRESOLVING) {
                            openconn(xsock);
                        } else if (xsock->flags & SOCKPENDING) {
                            establish(xsock);
                        } else if (xsock == fsock || background) {
                            received += handle_socket_input();
                        } else {
                            FD_CLR(xsock->fd, &readers);
                        }
                    }
                    sock = sock->next ? sock->next : hsock;
                } while (count && sock != stopsock && received < SPAM);

                /* fsock and/or xsock may have changed during loop above. */
                xsock = fsock;
            }

            /* other active fds must be from command /quotes. */
            if (count) proctime = time(NULL);
        }

        if (pending_line && read_depth) {    /* end of tf read() */
            pending_line = FALSE;
            break;
        }

        if (dead_socks && (!fsock || fsock->flags & SOCKDEAD)) {
            if (auto_fg) fg_live_sock();
            else fg_sock(NULL, FALSE);
        }
        /* garbage collection */
        if (depth == 1) {
            if (sock && sock->flags & SOCKDEAD) sock = NULL;
            if (dead_socks) nuke_dead_socks(); /* at end in case of quitdone */
            nuke_dead_macros();
            nuke_dead_procs();
        }
    }

    /* end of loop */
    if (!--depth)
        while (hsock) nukesock(hsock);
}

int is_active(fd)
    int fd;
{
    return FD_ISSET(fd, &active);
}

void readers_clear(fd)
    int fd;
{
    FD_CLR(fd, &readers);
}

void readers_set(fd)
    int fd;
{
    FD_SET(fd, &readers);
    if (fd >= nfds) nfds = fd + 1;
}

int tog_bg()
{
    Sock *sock;
    if (background)
        for (sock = hsock; sock; sock = sock->next)
            if (!(sock->flags & (SOCKDEAD | SOCKPENDING)))
                FD_SET(sock->fd, &readers);
    return 1;
}

/* get name of foreground world */
CONST char *fgname()
{
    return fsock ? fsock->world->name : NULL;
}

/* get current operational world */
World *xworld()
{
    return xsock ? xsock->world : NULL;
}

TIME_T sockidle(name, dir)
    CONST char *name;
{
    Sock *sock;
    sock = *name ? find_sock(name) : xsock;
    return sock ? time(NULL) - sock->time[dir] : -1;
}

static Sock *find_sock(name)
    CONST char *name;
{
    Sock *sock;

    /* It is possible to (briefly) have multiple sockets with the same name,
     * if at most one is alive.  The alive socket is the most interesting,
     * and it will be closer to the tail, so we search backwards.
     */
    for (sock = tsock; sock; sock = sock->prev)
        if (sock->world && cstrcmp(sock->world->name, name) == 0)
            return sock;
    return NULL;
}

/* load macro file for a world */
static void wload(w)
    World *w;
{
    CONST char *mfile;
    if (restriction >= RESTRICT_FILE) return;
    if ((mfile = world_mfile(w)))
        do_file_load(mfile, FALSE);
}


/* bring a socket into the foreground */
static int fg_sock(sock, quiet)
    Sock *sock;
    int quiet;
{
    int oldecho = sockecho;
    int activity;
    static int depth = 0;

    if (depth) {
        eprintf("illegal reentry");
        return 0;
    }

    sockecho = sock ? !IS_DO(sock, TN_ECHO) : TRUE;
    if (oldecho != sockecho)
        set_refresh_pending(REF_LOGICAL);

    xsock = sock;
    if (sock == fsock) return 2;  /* already there */
    fsock = sock;
    depth++;

    if ((activity = sock && sock->activity)) {
        /* this must be done before calling any user code (hooks, %status_*) */
        sock->activity = 0;
        --active_count;
    }

    update_status_field(NULL, STAT_WORLD);

    if (sock) {
        FD_SET(sock->fd, &readers);
        if (activity)
            update_status_field(NULL, STAT_ACTIVE);
        do_hook(H_WORLD, (sock->flags & SOCKDEAD) ?
            "---- World %s (dead) ----" : "---- World %s ----",
           "%s", sock->world->name);
        if (activity)
            flushout_queue(sock->queue, quiet);
        tog_lp();
        update_prompt(sock->prompt, 1);
        if (sockmload) wload(sock->world);
    } else {
        do_hook(H_WORLD, "---- No world ----", "");
        update_prompt(NULL, 1);
    }
    depth--;
    return 1;
}

struct Value *handle_fg_command(args)
    char *args;
{
    int opt, nosock = FALSE, noerr = FALSE, dir = 0, quiet = FALSE;
    Sock *sock;

    startopt(args, "nlqs<>");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
        case 'n':  nosock = TRUE;  break;
        case 's':  noerr = TRUE;  break;
        case 'l':  break;  /* accepted and ignored */
        case 'q':  quiet = TRUE; break;
        case '<':  dir = -1;  break;
        case '>':  dir =  1;  break;
        default:   return newint(0);
        }
    }

    if (nosock) {
        return newint(fg_sock(NULL, quiet));

    } else if (dir) {
        Sock *stop;
        if (!hsock) return 0;
        stop = sock = (fsock ? fsock : hsock);
        do {
            sock = (dir > 0) ? (sock->next ? sock->next : hsock) :
                               (sock->prev ? sock->prev : tsock);
        } while ((sock->flags & SOCKPENDING) && sock != stop);
        return newint(fg_sock(sock, quiet));
    }

    sock = (!*args) ? hsock : find_sock(args);
    if (!sock || sock->flags & SOCKPENDING) {
        if (!noerr) eprintf("not connected to '%s'", args);
        return newint(0);
    }
    return newint(fg_sock(sock, quiet));
}

/* openworld
 * If (name && port), they are used as hostname and port number.
 * If (!port), name is used as the name of a world.  A NULL or empty name
 * corresponds to the first defined world.  A NULL name should be used for
 * the initial automatic connection, an empty name should be used for any
 * other unspecified connection.
 */
int openworld(name, port, autologin, quietlogin)
    CONST char *name, *port;
    int autologin, quietlogin;
{
    World *world = NULL;

    xsock = NULL;
    if (!port) {
        world = find_world(name);
        if (!world)
            if (name)
                do_hook(H_CONFAIL, "%% Connection to %s failed: %s", "%s %s",
                    *name ? name : "default world", "no such world");
            else
                do_hook(H_WORLD, "---- No world ----", "");
    } else {
        if (restriction >= RESTRICT_WORLD)
            eprintf("arbitrary connections restricted");
        else {
            world = new_world(NULL, "", "", name, port, "", "", WORLD_TEMP);
        }
    }

    return world ? opensock(world, autologin, quietlogin) : 0;
}

int opensock(world, autologin, quietlogin)
    World *world;
    int autologin, quietlogin;
{
    struct servent *service;
    int herr;
    CONST char *what;

    if (world->sock) {
        if (!(world->sock->flags & SOCKDEAD)) {
            eprintf("socket to %s already exists", world->name);
            return 0;
        }
        /* recycle existing Sock */
        dead_socks--;
        xsock = world->sock;
        if (xsock->fd >= 0) {
            close(xsock->fd);
            FD_CLR(xsock->fd, &readers);
            FD_CLR(xsock->fd, &writers);
        }
        if (xsock->host) FREE(xsock->host);
        if (xsock->port) FREE(xsock->port);

    } else {
        /* create and initialize new Sock */
        if (!(xsock = world->sock = (Sock *) MALLOC(sizeof(struct Sock)))) {
            eprintf("opensock: not enough memory");
            return 0;
        }
        xsock->world = world;
        xsock->prev = tsock;
        tsock = *(tsock ? &tsock->next : &hsock) = xsock;
        xsock->activity = 0;
        Stringinit(xsock->buffer);
        xsock->prompt = NULL;
        init_queue(xsock->queue = (Queue *)XMALLOC(sizeof(Queue)));
        xsock->next = NULL;
    }
    xsock->fd = -1;
    xsock->pid = -1;
    xsock->state = '\0';
    xsock->attrs = 0;
    VEC_ZERO(&xsock->tn_do);
    VEC_ZERO(&xsock->tn_will);
    xsock->flags = (autologin ? SOCKLOGIN : 0);
    xsock->time[SOCK_RECV] = xsock->time[SOCK_SEND] = time(NULL);
    quietlogin = quietlogin && autologin && world_character(world);
    xsock->numquiet = quietlogin ? MAXQUIET : 0;

    xsock->addr.sin_family = AF_INET;

    if (!(world->flags & WORLD_NOPROXY) && proxy_host && *proxy_host) {
        xsock->flags |= SOCKPROXY;
        xsock->host = STRDUP(proxy_host);
        xsock->port = (proxy_port && *proxy_port) ? proxy_port : "23";
        xsock->port = STRDUP(xsock->port);
    } else {
        xsock->host = STRDUP(world->host);
        xsock->port = STRDUP(world->port);
    }

    if (is_digit(*xsock->port)) {
        xsock->addr.sin_port = htons(atoi(xsock->port));
#ifndef NO_NETDB
    } else if ((service = getservbyname(xsock->port, "tcp"))) {
        xsock->addr.sin_port = service->s_port;
#endif
    } else {
        CONFAIL(world->name, xsock->port, "no such service");
        killsock(xsock);  /* can't nukesock(), this may be a recycled Sock. */
        return 0;
    }

    xsock->flags |= SOCKRESOLVING;
    xsock->fd = get_host_address(xsock->host, &xsock->addr.sin_addr,
        &xsock->pid, &what, &herr);
    if (xsock->fd == 0) {
        /* The name lookup succeeded */
        xsock->flags &= ~SOCKRESOLVING;
        return openconn(xsock);
    } else if (xsock->fd < 0) {
        /* The name lookup failed */
        /* Note, some compilers can't handle (herr ? hsterror : literal) */
        if (what)
            CONFAIL(world->name, what, strerror(errno));
        else if (herr)
            CONFAIL(world->name, xsock->host, hstrerror(herr));
        else
            CONFAIL(world->name, xsock->host, "name resolution failed");
        killsock(xsock);  /* can't nukesock(), this may be a recycled Sock. */
        return 0;
    } else {
        /* The name lookup is pending.  We wait for it for a fraction of a
         * second here so "relatively fast" looks "immediate" to the user.
         */
        fd_set readable;
        struct timeval tv;
        FD_ZERO(&readable);
        FD_SET(xsock->fd, &readable);
        tv.tv_sec = 0;
        tv.tv_usec = CONN_WAIT;
        if (select(xsock->fd + 1, &readable, NULL, NULL, &tv) > 0) {
            /* The lookup completed. */
            return openconn(xsock);
        }
        /* select() returned 0, or -1 and errno==EINTR.  Either way, the
         * lookup needs more time.  So we add the fd to the set being
         * watched by main_loop(), and don't block here any longer.
         */
        readers_set(xsock->fd);
        do_hook(H_PENDING, "%% Hostname resolution for %s in progress.", "%s",
            xsock->world->name);
        return 2;
    }
}

static int openconn(sock)
    Sock *sock;
{
    int flags, err = 0;

    xsock = sock;
#ifdef NONBLOCKING_GETHOST
    if (xsock->flags & SOCKRESOLVING) {
        xsock->flags &= ~SOCKRESOLVING;
        FD_CLR(xsock->fd, &readers);
        if (read(xsock->fd, &err, sizeof(err)) < 0 || err != 0) {
            if (!err)
                CONFAIL(xsock->world->name, "read", strerror(errno));
            else
                CONFAIL(xsock->world->name, xsock->host, hstrerror(err));
            close(xsock->fd);
            xsock->fd = -1;
# ifdef PLATFORM_UNIX
	    if (xsock->pid >= 0)
		if (waitpid(xsock->pid, NULL, 0) < 0)
		    tfprintf(tferr, "waitpid %ld: %s", xsock->pid, strerror(errno));
	    xsock->pid = -1;
# endif /* PLATFORM_UNIX */
            killsock(xsock);
            return 0;
        }
        read(xsock->fd, (char*)&xsock->addr.sin_addr, sizeof(struct in_addr));
        close(xsock->fd);
# ifdef PLATFORM_UNIX
        if (xsock->pid >= 0)
            if (waitpid(xsock->pid, NULL, 0) < 0)
                tfprintf(tferr, "waitpid %ld: %s", xsock->pid, strerror(errno));
        xsock->pid = -1;
# endif /* PLATFORM_UNIX */
    }
#endif /* NONBLOCKING_GETHOST */

#if 0
    /* Jump back here if we start a nonblocking connect and then discover
     * that the platform has a broken read() or select().
     */
    retry:
#endif

    if ((xsock->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        CONFAIL(xsock->world->name, "socket", strerror(errno));
        killsock(xsock);
        return 0;
    }
    if (xsock->fd >= nfds) nfds = xsock->fd + 1;

    if (!TF_NONBLOCK) {
        set_var_by_id(VAR_async_conn, 0, NULL);
    } else if (async_conn) {
        /* note: 3rd arg to fcntl() is optional on Unix, but required by OS/2 */
        if ((flags = fcntl(xsock->fd, F_GETFL, 0)) < 0) {
            operror("Can't make socket nonblocking: F_GETFL fcntl");
            set_var_by_id(VAR_async_conn, 0, NULL);
        } else if ((fcntl(xsock->fd, F_SETFL, flags | TF_NONBLOCK)) < 0) {
            operror("Can't make socket nonblocking: F_SETFL fcntl");
            set_var_by_id(VAR_async_conn, 0, NULL);
        }
    }

    xsock->flags |= SOCKPENDING;
    if (in_connect(xsock->fd, &xsock->addr) == 0) {
        /* The connection completed successfully. */
        xsock->flags &= ~SOCKPENDING;
        return establish(xsock);

#ifdef EINPROGRESS
    } else if (errno == EINPROGRESS) {
        /* The connection needs more time.  It will select() as writable when
         * it has connected, or readable when it has failed.  We select() it
         * briefly here so "fast" looks synchronous to the user.
         */
        fd_set writeable;
        struct timeval tv;
        FD_ZERO(&writeable);
        FD_SET(xsock->fd, &writeable);
        tv.tv_sec = 0;
        tv.tv_usec = CONN_WAIT;
        if (select(xsock->fd + 1, NULL, &writeable, NULL, &tv) > 0) {
            /* The connection completed. */
            return establish(xsock);
        }
        /* select() returned 0, or -1 and errno==EINTR.  Either way, the
         * connection needs more time.  So we add the fd to the set being
         * watched by main_loop(), and don't block here any longer.
         */
        FD_SET(xsock->fd, &writers);
        FD_SET(xsock->fd, &readers);
        do_hook(H_PENDING, "%% Connection to %s in progress.", "%s",
            xsock->world->name);
        return 2;
#endif /* EINPROGRESS */

#if 0  /* this can cause problems on nonbuggy systems, so screw the sysv bug */
    } else if (can_nonblock && (errno == EAGAIN
# ifdef EWOULDBLOCK
                                                || errno == EWOULDBLOCK
# endif
                                                                       )) {
        /* A bug in SVR4.2 causes nonblocking connect() to (sometimes?)
         * incorrectly fail with EAGAIN.  The only thing we can do about
         * it is to try a blocking connect().
         */
        close(xsock->fd);
        can_nonblock = FALSE;
        goto retry; /* try again */
#endif /* 0 */
    }

    /* The connection failed.  Give up. */
    CONFAIL(xsock->world->name, "connect", strerror(errno));
    killsock(xsock);
    return 0;
}

/* Convert name or ip number string to an in_addr.
 * Returns -1 for failure, 0 for success, or a positive file descriptor
 * connected to a pending name lookup process or thread.
 */
static int get_host_address(name, sin_addr, pidp, what, errp)
    CONST char *name;
    struct in_addr *sin_addr;
    long *pidp;
    CONST char **what;
    int *errp;
{
#ifndef dgux
    /* Most versions of inet_addr() return a long. */
    sin_addr->s_addr = inet_addr(name);
#else
    /* DGUX's inet_addr() returns a struct in_addr, which actually makes more
     * sense, but isn't compatible with anyone else. */
    sin_addr->s_addr = inet_addr(name).s_addr;
#endif
    if (sin_addr->s_addr != INADDR_NONE) return 0;  /* success */

    /* Numeric format failed.  Try name format. */
    *errp = 0;
    *what = NULL;
    return
#ifdef NONBLOCKING_GETHOST
        (async_name) ? nonblocking_gethost(name, sin_addr, pidp, what) :
#endif
        blocking_gethost(name, sin_addr, errp);
}

#ifndef NO_NETDB

static int blocking_gethost(name, sin_addr, errp)
    CONST char *name;
    struct in_addr *sin_addr;
    int *errp;
{
    struct hostent *host;

    if ((host = gethostbyname(name))) {
        *errp = 0;
        memcpy((GENERIC *)sin_addr, (GENERIC *)host->h_addr, sizeof(*sin_addr));
        return 0;
    }
    *errp = h_errno;
    return -1;
}

#ifdef NONBLOCKING_GETHOST
static void waitforhostname(fd, name)
    int fd;
    CONST char *name;
{
    struct hostent *host;
    int err;

    if ((host = gethostbyname(name))) {
        err = 0;
        write(fd, &err, sizeof(err));
        write(fd, (GENERIC *)host->h_addr, sizeof(struct in_addr));
    } else {
        err = h_errno;
        write(fd, &err, sizeof(err));
    }
    close(fd);
}

# ifdef PLATFORM_OS2
typedef struct _threadpara {
    CONST char *hostname;
    int   fd;
} threadpara;

void os2waitforhostname(targs)
    threadpara *targs;
{
    waitforhostname(targs->fd, targs->hostname);
    FREE(targs->hostname);
    FREE(targs);
}
# endif /* PLATFORM_OS2 */

static int nonblocking_gethost(name, sin_addr, pidp, what)
    CONST char *name;
    struct in_addr *sin_addr;
    long *pidp;
    CONST char **what;
{
    int fds[2];
    int err;

    *what = "pipe";
    if (pipe(fds) < 0) return -1;

#ifdef PLATFORM_UNIX
    {
        *what = "fork";
        *pidp = fork();
        if (*pidp > 0) {          /* parent */
            close(fds[1]);
            return fds[0];
        } else if (*pidp == 0) {  /* child */
            close(fds[0]);
            waitforhostname(fds[1], name);
            exit(0);
        }
    }
#endif
#ifdef PLATFORM_OS2
    {
        threadpara *tpara;
  
        if ((tpara = XMALLOC(sizeof(threadpara)))) {
            setmode(fds[0],O_BINARY);
            setmode(fds[1],O_BINARY);
            tpara->fd = fds[1];
            tpara->hostname = STRDUP(name);

            /* BUG: gethostbyname() isn't threadsafe! */
            *what = "_beginthread";
            if (_beginthread(os2waitforhostname,NULL,0x8000,(void*)tpara) != -1)
                return(fds[0]);
        }
    }
#endif

    /* failed */
    err = errno;
    close(fds[0]);
    close(fds[1]);
    errno = err;
    return -1;
}
#endif /* NONBLOCKING_GETHOST */

#endif /* NO_NETDB */


/* Establish a sock for which connect() has completed. */
static int establish(sock)
    Sock *sock;
{
    xsock = sock;
#if TF_NONBLOCK
    if (xsock->flags & SOCKPENDING) {
        CONST char *errmsg = "nonblocking connect";
        int err = 0, len = sizeof(err), flags;

        /* Old Method 1: If read(fd, buf, 0) fails, the connect() failed, and
         * errno will explain why.  Problem: on some systems, a read() of
         * 0 bytes is always successful, even if socket is not connected.
         */
        /* Old Method 2: If a second connect() fails with EISCONN, the first
         * connect() worked.  On the slim chance that the first failed, but
         * the second worked, use that.  Otherwise, use getsockopt(SO_ERROR)
         * to find out why the first failed.  If SO_ERROR isn't available,
         * use read() to get errno.  This method works for all systems, as
         * well as SOCKS 4.2beta.  Problems: Some socket implementations
         * give the wrong errno; extra net traffic on failure.
         */
        /* CURRENT METHOD:  If possible, use getsockopt(SO_ERROR) to test for
         * an error.  This avoids the overhead of a second connect(), and the
         * possibility of getting the error value from the second connect()
         * (linux).  (Potential problem: it's possible that some systems
         * don't clear the SO_ERROR value for successful connect().)
         * If SO_ERROR is not available or we are using SOCKS, we try to read
         * 0 bytes.  If it fails, the connect() must have failed.  If it
         * succeeds, we can't know if it's because the connection really
         * succeeded, or the read() did a no-op for the 0 size, so we try a
         * second connect().  If the second connect() fails with EISCONN, the
         * first connect() worked.  If it works (unlikely), use it.  Otherwise,
         * use read() to get the errno.
         * Tested on:  Linux, HP-UX, Solaris, Solaris with SOCKS5...
         */
        /* Alternative: replace second connect() with getpeername(), and
         * check for ENOTCONN.  Disadvantage: doesn't work with SOCKS, etc.
         */

#ifdef SO_ERROR
# ifndef SOCKS
#  define USE_SO_ERROR
# endif
#endif

#ifdef USE_SO_ERROR
        if (getsockopt(xsock->fd, SOL_SOCKET, SO_ERROR,
            (GENERIC*)&err, &len) < 0)
        {
            errmsg = "getsockopt";
            err = errno;
        }

#else
        {
            char ch;
            if (read(xsock->fd, &ch, 0) < 0) {
                err = errno;
                errmsg = "nonblocking connect/read";
            } else if ((in_connect(xsock->fd, &xsock->addr) < 0) &&
                errno != EISCONN)
            {
                read(xsock->fd, &ch, 1);   /* must fail */
                err = errno;
                errmsg = "nonblocking connect 2/read";
            }
        }
#endif
        if (err != 0) {
            killsock(xsock);
            CONFAIL(xsock->world->name, errmsg, strerror(err));
            return 0;
        }

        /* connect() worked.  Clear the pending stuff, and get on with it. */
        xsock->flags &= ~SOCKPENDING;
        FD_CLR(xsock->fd, &writers);

        /* Turn off nonblocking (this should help on buggy systems). */
        /* note: 3rd arg to fcntl() is optional on Unix, but required by OS/2 */
        if ((flags = fcntl(xsock->fd, F_GETFL, 0)) >= 0)
            fcntl(xsock->fd, F_SETFL, flags & ~TF_NONBLOCK);
    }
#endif /* TF_NONBLOCK */

    FD_SET(xsock->fd, &readers);

    /* atomicness ends here */

    if (sock->flags & SOCKPROXY) {
        FREE(sock->host);
        FREE(sock->port);
        sock->host = STRDUP(sock->world->host);
        sock->port = STRDUP(sock->world->port);
        do_hook(H_PROXY, "", "%s", sock->world->name);
    }

    wload(sock->world);

    if (!(sock->flags & SOCKPROXY)) {
        do_hook(H_CONNECT, "%% Connection to %s established.", "%s",
            sock->world->name);

        if (login && sock->flags & SOCKLOGIN) {
            if (world_character(sock->world) && world_pass(sock->world))
                do_hook(H_LOGIN, NULL, "%s %s %s", sock->world->name,
                    world_character(sock->world), world_pass(sock->world));
            sock->flags &= ~SOCKLOGIN;
        }
    }

    return 1;
}

/* nukesock
 * Remove socket from list and free memory.  Should only be called on a
 * Sock which is known to have no references other than the socket list.
 */
static void nukesock(sock)
    Sock *sock;
{
    if (sock == xsock) xsock = NULL;
    if (sock->world->sock == sock) {
        /* false if /connect follows close() in same interation of main loop */
        sock->world->sock = NULL;
    }
    if (sock->world->flags & WORLD_TEMP) {
        nuke_world(sock->world);
        sock->world = NULL;
    }
    *((sock == hsock) ? &hsock : &sock->prev->next) = sock->next;
    *((sock == tsock) ? &tsock : &sock->next->prev) = sock->prev;
    if (sock->activity) {
	--active_count;
	update_status_field(NULL, STAT_ACTIVE);
    }
    if (sock->fd >= 0) {
        FD_CLR(sock->fd, &readers);
        FD_CLR(sock->fd, &writers);
        close(sock->fd);
#ifdef NONBLOCKING_GETHOST
# ifdef PLATFORM_UNIX
        if (sock->pid >= 0)
            if (waitpid(sock->pid, NULL, 0) < 0)
                tfprintf(tferr, "waitpid %ld: %s", sock->pid, strerror(errno));
        sock->pid = -1;
# endif /* PLATFORM_UNIX */
#endif /* NONBLOCKING_GETHOST */
    }
    Stringfree(sock->buffer);
    if (sock->prompt) free_aline(sock->prompt);
    while(sock->queue->head)
        free_aline((Aline*)unlist(sock->queue->head, sock->queue));
    FREE(sock->host);
    FREE(sock->port);
    FREE(sock->queue);
    FREE(sock);
}

static void fg_live_sock()
{
    /* If the fg sock is dead, find another sock to put in fg.  Since this
     * is called from main_loop(), xsock must be the same as fsock.  We must
     * keep it that way.  Note that a user hook in fg_sock() could kill the
     * new fg sock, so we must loop until the new fg sock stays alive.
     */
    while (fsock && (fsock->flags & SOCKDEAD)) {
        for (xsock = hsock; xsock; xsock = xsock->next) {
            if (!(xsock->flags & (SOCKDEAD | SOCKPENDING))) break;
        }
        fg_sock(xsock, FALSE);
    }
}

/* delete all dead sockets */
static void nuke_dead_socks()
{
    Sock *sock, *next;

    for (sock = hsock; sock; sock = next) {
        next = sock->next;
        if (sock->flags & SOCKDEAD) {
            if (sock->activity) {
                if (sock->fd >= 0)
		    FD_CLR(sock->fd, &readers);
            } else {
                nukesock(sock);
                dead_socks--;
            }
        }
    }
    if (quitdone && !hsock) quit_flag = 1;
}

/* disconnect a socket */
struct Value *handle_dc_command(args)
    char *args;
{
    Sock *s;

    if (!*args) {
        if (!xsock) {
            eprintf("no current socket");
            return newint(0);
        }
        killsock(xsock);
        oprintf ("%% Connection to %s closed.", xsock->world->name);
    } else if (cstrcmp(args, "-all") == 0) {
        for (s = hsock; s; s = s->next) killsock(s);
    } else {
        if ((s = find_sock(args)) && !(s->flags & SOCKDEAD)) {
            killsock(s);
            oprintf ("%% Connection to %s closed.", s->world->name);
        } else {
            eprintf("Not connected to %s", args);
            return newint(0);
        }
    }
    return newint(1);
}

/* display list of open sockets and their state. */
struct Value *handle_listsockets_command(args)
    char *args;
{
    Sock *sock;
    char idlebuf[16], linebuf[16];
    TIME_T now;
    int t, opt;
    int count = 0, error = 0, shortflag = FALSE, mflag = matching;
    Pattern pat_name, pat_type;

    init_pattern_str(&pat_name, NULL);
    init_pattern_str(&pat_type, NULL);

    startopt(args, "m:sT:");
    while ((opt = nextopt(&args, NULL))) {
        switch(opt) {
        case 'm':
            if ((mflag = enum2int(args, enum_match, "-m")) < 0)
                goto listsocket_error;
            break;
        case 's':
            shortflag = TRUE;
            break;
        case 'T':
            free_pattern(&pat_type);
            error += !init_pattern_str(&pat_type, args);
            break;
        default:
            goto listsocket_error;
        }
    }
    if (error) goto listsocket_error;
    init_pattern_mflag(&pat_type, mflag);
    if (*args) error += !init_pattern(&pat_name, args, mflag);
    if (error) goto listsocket_error;

    if (!hsock) {
        if (!shortflag) eprintf("Not connected to any sockets.");
        goto listsocket_error;
    }

    now = time(NULL);

    if (!shortflag)
        oputs("     LINES IDLE TYPE      NAME            HOST                       PORT");
    for (sock = hsock; sock; sock = sock->next) {
        if (pat_type.str &&
            !patmatch(&pat_type, sock->world->type ? sock->world->type : ""))
            continue;
        if (*args && !patmatch(&pat_name, sock->world->name)) continue;
        count++;
        if (shortflag) {
            oputs(sock->world->name);
            continue;
        }
        if (sock == fsock)
            sprintf(linebuf, "%7s", "foregnd");
        else
            sprintf(linebuf, "%7d", sock->activity);
        t = now - sock->time[SOCK_RECV];
        if (t < (60))
            sprintf(idlebuf, "%3ds", t);
        else if ((t /= 60) < 60)
            sprintf(idlebuf, "%3dm", t);
        else if ((t /= 60) < 24)
            sprintf(idlebuf, "%3dh", t);
        else if ((t /= 24) < 1000)
            sprintf(idlebuf, "%3dd", t);
        else
            sprintf(idlebuf, "long");

        oprintf("%c%c %s %s %-9.9s %-15.15s %-26.26s %.6s",
            ((sock == xsock) ? '*' : ' '),
            ((sock->flags & SOCKDEAD) ? '!' :
                (sock->flags & (SOCKPENDING | SOCKRESOLVING)) ? '?' :
                (sock->flags & (SOCKPROXY)) ? '@' : ' '),
            linebuf, idlebuf,
            sock->world->type, sock->world->name, sock->host, sock->port);
    }

listsocket_error:
    free_pattern(&pat_name);
    free_pattern(&pat_type);

    return newint(count);
}

int handle_send_function(text, world, eol_flag)
    CONST char *text, *world;
    int eol_flag;
{
    Sock *save, *sock;

    save = xsock;
    sock = (!world || !*world) ? xsock : find_sock(world);
    if (!sock || sock->flags & (SOCKDEAD | SOCKPENDING)) {
        eprintf("Not connected.");
        return 0;
    }
    xsock = sock;
    send_line(text, strlen(text), eol_flag);
    xsock = save;
    return 1;
}


/* tramsmit text to current socket */
static int transmit(str, numtowrite)
    CONST char *str;
    unsigned int numtowrite;
{
    int numwritten, err;

    if (!xsock || xsock->flags & (SOCKDEAD | SOCKPENDING)) return 0;
    while (numtowrite) {
        numwritten = send(xsock->fd, str, numtowrite, 0);
        if (numwritten < 0) {
            err = errno;
            if (err == EAGAIN
#ifdef EWOULDBLOCK
                                || err == EWOULDBLOCK
#endif
                                                       ) {
                fd_set writefds;
                numwritten = 0;
                FD_ZERO(&writefds);
                FD_SET(xsock->fd, &writefds);
                if (select(xsock->fd + 1, NULL, &writefds, NULL, NULL) < 0)
                    if (err == EINTR) break;
            } else {
                killsock(xsock);
                do_hook(H_DISCONNECT,
                    "%% Connection to %s closed: %s: %s", "%s %s %s",
                    xsock->world->name, "send", strerror(err));
                return 0;
            }
        }
        numtowrite -= numwritten;
        str += numwritten;
        xsock->time[SOCK_SEND] = time(NULL);
    }
    return 1;
}

/* send_line
 * Send a line to the server on the current socket.  If there is a prompt
 * associated with the current socket, clear it.
 * RFCs 854 and 1123 technically forbid sending 8-bit data in non-BINARY mode;
 * but if the user types it, we send it regardless of BINARY mode.  Some
 * servers accept it, some strip the high bit, some ignore the characters.
 */
int send_line(src, len, eol_flag)
    CONST char *src;
    unsigned int len;
    int eol_flag;
{
    int result;
    int i = 0, j = 0;
    char *buf;

    if (!xsock || xsock->flags & (SOCKDEAD | SOCKPENDING)) return 0;

    if (xsock && xsock->prompt) {
        /* Not the same as unprompt(): we keep attrs, and delete buffer. */
        Stringterm(xsock->buffer, 0);
        free_aline(xsock->prompt);
        xsock->prompt = NULL;
        if (xsock == fsock) update_prompt(xsock->prompt, 1);
    }

    buf = XMALLOC(2 * len + 3);
    while (j < len) {
        if (src[j] == TN_IAC)
            buf[i++] = TN_IAC;    /* double IAC */
        buf[i] = unmapchar(src[j]);
        i++, j++;
    }

    if (eol_flag) {
        /* In telnet NVT mode, append CR LF; in telnet BINARY mode,
         * append LF, CR, or CR LF, according to variable.
         */
        if (!IS_WILL(xsock, TN_BINARY) || binary_eol != EOL_LF)
            buf[i++] = '\r';
        if (!IS_WILL(xsock, TN_BINARY) || binary_eol != EOL_CR)
            buf[i++] = '\n';
    }

    result = transmit(buf, i);

    FREE(buf);
    return result;
}

static void handle_socket_line()
{
    /* xsock->flags |= SOCKPROMPT; */
    incoming_text = new_alinen(xsock->buffer->s, 0, xsock->buffer->len);
    incoming_text->links = 1;
    gettime(&incoming_text->tv);
    xsock->time[SOCK_RECV] = incoming_text->tv.tv_sec;
    Stringterm(xsock->buffer, 0);

    xsock->attrs = (emulation == EMUL_ANSI_ATTR) ?
        handle_ansi_attr(incoming_text, xsock->attrs) : 0;

    if (borg || hilite || gag) {
        if (find_and_run_matches(incoming_text->str, 0, &incoming_text,
            xworld(), TRUE))
        {
            if (xsock != fsock)
                do_hook(H_BACKGROUND, "%% Trigger in world %s", "%s %S",
                    xsock->world->name, incoming_text);
        }
    }

    if (is_bamf(incoming_text->str) || is_quiet(incoming_text->str) ||
        is_watchdog(xsock->world->history, incoming_text) ||
        is_watchname(xsock->world->history, incoming_text))
    {
        incoming_text->attrs |= F_GAG;
    }

    world_output(xsock->world, incoming_text);
    free_aline(incoming_text);
    incoming_text = NULL;
}

/* log, record, and display aline as if it came from sock */
void world_output(world, aline)
    World *world;
    Aline *aline;
{
    aline->links++;
    recordline(world->history, aline);
    if (world->sock && !(world->sock->flags & SOCKDEAD) &&
        !(gag && (aline->attrs & F_GAG)))
    {
        if (world->sock == fsock) {
            record_global(aline);
            screenout(aline);
        } else {
            if (bg_output) {
                aline->links++;
                enqueue(world->sock->queue, aline);
            }
            if (!world->sock->activity++) {
                ++active_count;
                update_status_field(NULL, STAT_ACTIVE);
                do_hook(H_ACTIVITY, "%% Activity in world %s", "%s",
                    world->name);
            }
        }
    }
    free_aline(aline);
}

/* get the prompt for the fg sock */
Aline *fgprompt()
{
    return (fsock) ? fsock->prompt : NULL;
}

int tog_lp()
{
    if (!fsock) {
        /* do nothing */
    } else if (lpflag) {
        if (fsock->buffer->len) {
            (fsock->prompt = new_aline(fsock->buffer->s, 0))->links++;
            if (emulation == EMUL_ANSI_ATTR)
                xsock->attrs = handle_ansi_attr(fsock->prompt, xsock->attrs);
            else
                xsock->attrs = 0;
            set_refresh_pending(REF_PHYSICAL);
        }
    } else {
        if (fsock->prompt && !(fsock->flags & SOCKPROMPT)) {
            unprompt(fsock, 1);
            set_refresh_pending(REF_PHYSICAL);
        }
    }
    return 1;
}

struct Value *handle_prompt_command(args)
    char *args;
{
    if (xsock) handle_prompt(args, TRUE);
    return newint(!!xsock);
}

static void handle_prompt(str, confirmed)
    CONST char *str;
    int confirmed;
{
    if (lpquote) proctime = time(NULL);
    if (xsock->prompt) {
        unprompt(xsock, 0);
    }
    (xsock->prompt = new_aline(str, xsock->attrs))->links++;
    if (emulation == EMUL_ANSI_ATTR)
        xsock->attrs = handle_ansi_attr(xsock->prompt, xsock->attrs);
    else
        xsock->attrs = 0;
    /* Old versions did trigger checking here.  Removing it breaks
     * compatibility, but I doubt many users will care.  Leaving
     * it in would not be right for /prompt.
     */
    if (xsock == fsock) update_prompt(xsock->prompt, 1);
    if (confirmed) xsock->flags |= SOCKPROMPT;
    else xsock->flags &= ~SOCKPROMPT;
    xsock->numquiet = 0;
}

/* undo the effects of a false prompt */
static void unprompt(sock, update)
    Sock *sock;
    int update;
{
    if (!sock || !sock->prompt) return;
    sock->attrs = sock->prompt->attrs;  /* restore original attrs */
    free_aline(sock->prompt);
    sock->prompt = NULL;
    if (update) update_prompt(sock->prompt, 1);
}

static void f_telnet_recv(cmd, opt)
    int cmd, opt;
{
    if (telopt) {
        char buf[4];
        sprintf(buf, "%c%c%c", TN_IAC, cmd, opt);
        telnet_debug("recv", buf,
            (cmd==(UCHAR)TN_DO || cmd==(UCHAR)TN_DONT ||
             cmd==(UCHAR)TN_WILL || cmd==(UCHAR)TN_WONT) ?
             3 : 2);
    }
}

/* handle input from current socket */
static int handle_socket_input()
{
    char *place, localchar, buffer[4096];
    fd_set readfds;
    int count, n, received = 0;
    struct timeval tv;

    if (xsock->prompt && !(xsock->flags & SOCKPROMPT)) {
        /* We assumed last text was a prompt, but now we have more text, so
         * we now assume that they are both part of the same long line.  (If
         * we're wrong, the previous prompt appears as output.  But if we did
         * the opposite, a real begining of a line would never appear in the
         * output window; that would be a worse mistake.)
         */
        unprompt(xsock, xsock==fsock);
    }

    do {  /* while (n > 0 && !interrupted() && (received += count) < SPAM) */
        do count = recv(xsock->fd, buffer, sizeof(buffer), 0);
            while (count < 0 && errno == EINTR);
        if (count <= 0) {
            int err = errno;
#ifdef SUNOS_5_4
            if (err == EAGAIN || err == EINVAL) {
                /* workaround for bug in (unpatched) Solaris 2.4 */
                static int warned = FALSE;
                if (!warned)
                    tfprintf(tferr, "%% %s%s%s",
                        "There is a socket bug in your Solaris 2.4 system.  "
                        "You should install the recommended Sun patches from "
                        "ftp://sunsite.unc.edu/pub/patches.");
                warned = TRUE;
                return;
            }
#endif
            if (xsock->buffer->len) handle_socket_line();
            killsock(xsock);
            /* On some systems, a socket that failed nonblocking connect selects
             * readable (not writable like most systems).  If SOCKPENDING,
             * that's what happened, so we hook CONFAIL instead of DISCONNECT.
             */
            if (xsock->flags & SOCKPENDING)
                CONFAIL(xsock->world->name, "recv", strerror(err));
            else do_hook(H_DISCONNECT, (count < 0) ?
                    "%% Connection to %s closed: %s: %s" :
                    "%% Connection to %s closed by foreign host.",
                    (count < 0) ? "%s %s %s" : "%s",
                    xsock->world->name, "recv", strerror(err));
            return received;
        }

        for (place = buffer; place - buffer < count; place++) {

            /* We always accept 8-bit data, even though RFCs 854 and 1123
             * say server shouldn't transmit it unless in BINARY mode.  What
             * we do with it depends on the locale.
             */
            localchar = localize(*place);

            if (xsock->state == TN_IAC) {
                switch (xsock->state = *place) {
                case TN_GA: case TN_EOR:
                    /* This is definitely a prompt. */
                    telnet_recv(*place, 0);
                    if (do_hook(H_PROMPT, NULL, "%S", xsock->buffer)) {
                        Stringterm(xsock->buffer, 0);
                    } else {
                        handle_prompt(xsock->buffer->s, TRUE);
                        Stringterm(xsock->buffer, 0);
                    }
                    break;
#if 0
                case TN_SB:
                    if (!xsock->subbuffer) {
                        allocate xsock->subbuffer
                    }
                    xsock->state = '\0';
                    break;
                case TN_SE:
                    if (xsock->subbuffer) {
                        telnet_debug("recv", xsock->subbuffer, n);
                        process subbuffer
                        free subbuffer
                        xsock->subbuffer = NULL;
                    }
                    break;
#endif
                case TN_WILL: case TN_WONT:
                case TN_DO:   case TN_DONT:
                    /* just change state */
                    break;
                case TN_IAC:
                    Stringadd(xsock->buffer, localchar);  /* literal IAC */
                    xsock->state = '\0';
                    break;
                default:
                    /* shouldn't happen; ignore it. */
                    telnet_recv(*place, 0);
                    xsock->state = '\0';
                    break;
                }

            } else if (xsock->state == TN_WILL) {
                telnet_recv(TN_WILL, *place);
                if (IS_DO(xsock, *place)) {
                    /* we're already in the DO state; ignore WILL */
                } else if (*place == TN_ECHO) {
                    /* stop local echo, tell server to DO ECHO */
                    DO(TN_ECHO);
                    sockecho = FALSE;
                } else if (*place == TN_SEND_EOR) {
                    DO(TN_SEND_EOR);
                } else if (*place == TN_BINARY) {
                    DO(TN_BINARY);
#if 0  /* many servers think DO SGA means character-at-a-time mode */
                } else if (*place == TN_SGA) {
                    DO(TN_SGA);
#endif
                } else {
                    /* don't accept other WILL offers */
                    DONT(*place);
                }
                xsock->state = '\0';

            } else if (xsock->state == TN_WONT) {
                telnet_recv(TN_WONT, *place);
                if (!IS_DO(xsock, *place)) {
                    /* already in the DONT state; ignore WONT */
                } else if (*place == TN_ECHO) {
                    /* server WONT ECHO, so we must resume local echo */
                    DONT(TN_ECHO);
                    sockecho = TRUE;
                } else {
                    /* no special handling, just acknowledge. */
                    DONT(*place);
                }
                xsock->state = '\0';

            } else if (xsock->state == TN_DO) {
                telnet_recv(TN_DO, *place);
                if (IS_WILL(xsock, *place)) {
                    /* we're already in the WILL state; ignore DO */
                } else if (*place == TN_NAWS) {
                    /* handle and acknowledge */
                    WILL(TN_NAWS);
                    do_naws(xsock);
                } else if (*place == TN_BINARY) {
                    WILL(TN_BINARY);
                } else {
                    /* refuse other DO requests */
                    WONT(*place);
                }
                xsock->state = '\0';

            } else if (xsock->state == TN_DONT) {
                telnet_recv(TN_DONT, *place);
                if (!IS_WILL(xsock, *place)) {
                    /* already in the WONT state; ignore DONT */
                } else {
                    /* no special handling, just acknowledge. */
                    WONT(*place);
                }
                xsock->state = '\0';

            } else if (*place == TN_IAC) {
                if (!(xsock->flags & SOCKTELNET)) {
                    /* We now know server groks TELNET */
                    xsock->flags |= SOCKTELNET;
                    preferred_telnet_options();
                }
                xsock->state = *place;

            } else if (*place == '\n') {
                /* Complete line received.  Process it. */
                handle_socket_line();
                xsock->state = *place;

            } else if (emulation == EMUL_DEBUG) {
                if (localchar != *place)
                    Stringcat(xsock->buffer, "M-");
                if (is_print(localchar))
                    Stringadd(xsock->buffer, localchar);
                else {
                    Stringadd(xsock->buffer, '^');
                    Stringadd(xsock->buffer, CTRL(localchar));
                }
                xsock->state = *place;

            } else if (*place == '\r' || *place == '\0') {
                /* Ignore CR and NUL. */
                xsock->state = *place;

            } else if (*place == '\b' && emulation >= EMUL_PRINT) {
                if (xsock->state == '*') {
                    /* "*\b" is an LP editor prompt. */
                    if (do_hook(H_PROMPT, NULL, "%S", xsock->buffer)) {
                        Stringterm(xsock->buffer, 0);
                    } else {
                        handle_prompt(xsock->buffer->s, TRUE);
                        Stringterm(xsock->buffer, 0);
                    }
                } else if (xsock->buffer->len && emulation >= EMUL_PRINT) {
                    Stringterm(xsock->buffer, xsock->buffer->len - 1);
                }
                xsock->state = *place;

            } else if (*place == '\t' && emulation >= EMUL_PRINT) {
                Stringnadd(xsock->buffer, ' ',
                    tabsize - xsock->buffer->len % tabsize);
                xsock->state = *place;

            } else if (emulation == EMUL_ANSI_STRIP &&
              ((xsock->state=='\033' && *place=='[') || *place==ANSI_CSI))  {
                /* CSI is either a single character, or "ESC [". */
                xsock->state = ANSI_CSI;

            } else if (emulation == EMUL_ANSI_STRIP &&
              xsock->state == ANSI_CSI &&
              (*place == '?' || *place == ';' || is_alnum(*place))) {
                /* ANSI terminal sequences contain: CSI, an optional '?',
                 * any number of digits and ';'s, and a letter.
                 */
                if (is_alpha(*place)) xsock->state = *place;

            } else if (*place == '\07' && emulation == EMUL_ANSI_ATTR) {
                Stringadd(xsock->buffer, *place);
                xsock->state = *place;

            } else if (!is_print(localchar) &&
              (emulation == EMUL_PRINT || emulation == EMUL_ANSI_STRIP)) {
                /* not printable */
                xsock->state = *place;

            } else {
                /* Normal character.  The is_print() loop is a fast heuristic
                 * to find next potentially interesting character. */
                char *end;
                Stringadd(xsock->buffer, localchar);
                end=++place;
                while (is_print(*end) && *end != TN_IAC && end - buffer < count)
                    end++;
                Stringfncat(xsock->buffer, (char*)place, end - place);
                place = end - 1;
                xsock->state = *place;
            }
        }

        /* See if anything arrived while we were parsing */

        FD_ZERO(&readfds);
        FD_SET(xsock->fd, &readfds);
        tv.tv_sec = tv.tv_usec = 0;

        if (xsock->buffer->len && do_hook(H_PROMPT,NULL,"%S",xsock->buffer)) {
            /* The hook took care of the unterminated line. */
            Stringterm(xsock->buffer, 0);
        } else if (lpflag && xsock->buffer->len && xsock == fsock) {
            /* Wait a little to see if the line gets completed. */
            tv.tv_sec = prompt_sec;
            tv.tv_usec = prompt_usec;
        }

        if ((n = select(xsock->fd + 1, &readfds, NULL, NULL, &tv)) < 0) {
            if (errno != EINTR) die("handle_socket_input: select", errno);
        }

    } while (n > 0 && !interrupted() && (received += count) < SPAM);

    /* If lpflag is on and we got a partial line from the fg world,
     * assume the line is a prompt.
     */
    if (lpflag && xsock == fsock && xsock->buffer->len) {
        handle_prompt(xsock->buffer->s, FALSE);
    }

    return received;
}


static int is_quiet(str)
    CONST char *str;
{
    if (!xsock->numquiet) return 0;
    if (!--xsock->numquiet) return 1;
    /* This should not be hard coded. */
    if ((strncmp("Use the WHO command", str, 19) == 0) ||
        (strcmp("### end of messages ###", str) == 0))
            xsock->numquiet = 0;
    return 1;
}

static int is_bamf(str)
    CONST char *str;
{
    smallstr name, host, port;
    STATIC_BUFFER(buffer);
    World *world;
    Sock *callingsock;  /* like find_and_run_matches(), we must restore xsock */

    callingsock = xsock;

    if (!bamf || restriction >= RESTRICT_WORLD) return 0;
    if (sscanf(str,
        "#### Please reconnect to %64[^ @]@%64s (%*64[^ )]) port %64s ####",
        name, host, port) != 3)
            return 0;

    Stringterm(buffer, 0);
    if (bamf == BAMF_UNTER) Stringadd(buffer, '@');
    Stringcat(buffer, name);
    if (!(world = find_world(buffer->s))) {
        if (bamf == BAMF_UNTER && xsock) {
            world = new_world(buffer->s, xsock->world->character,
                xsock->world->pass, host, port, xsock->world->mfile,
                xsock->world->type, WORLD_TEMP);
        } else {
            world = new_world(buffer->s,"","",host,port,"","",WORLD_TEMP);
        }
    }

    do_hook(H_BAMF, "%% Bamfing to %s", "%s", name);
    if (bamf == BAMF_UNTER && xsock) killsock(xsock);
    if (!opensock(world, TRUE, FALSE))
        eputs("% Connection through portal failed.");
    xsock = callingsock;
    return 1;
}

static void do_naws(sock)
    Sock *sock;
{
    unsigned int width, height, i;
    UCHAR octet[4];
    Sock *old_xsock;

    width = columns;
    height = lines - (visual ? isize+1 : 0);

    Sprintf(telbuf, 0, "%c%c%c", TN_IAC, TN_SB, TN_NAWS);
    octet[0] = (width >> 8);
    octet[1] = (width & 0xFF);
    octet[2] = (height >> 8);
    octet[3] = (height & 0xFF);
    for (i = 0; i < 4; i++) {
        if (octet[i] == (UCHAR)TN_IAC) Stringadd(telbuf, TN_IAC);
        Stringadd(telbuf, octet[i]);
    }
    Sprintf(telbuf, SP_APPEND, "%c%c", TN_IAC, TN_SE);

    old_xsock = xsock;;
    xsock = sock;
    telnet_send(telbuf);
    xsock = old_xsock;
}

static void telnet_debug(dir, str, len)
    CONST char *dir, *str;
    int len;
{
    STATIC_BUFFER(buffer);
    int i;
    char state;

    if (telopt) {
        Sprintf(buffer, 0, "%s %s:", dir, xsock->world->name);
        for (i = 0, state = 0; i < len; i++) {
            if (str[i] == TN_IAC || state == TN_IAC || state == TN_SB ||
                state == TN_WILL || state == TN_WONT ||
                state == TN_DO || state == TN_DONT)
            {
                if (telnet_label[(UCHAR)str[i]])
                    Sprintf(buffer, SP_APPEND, " %s",
                        telnet_label[(UCHAR)str[i]]);
                else
                    Sprintf(buffer, SP_APPEND, " %d", (unsigned int)str[i]);
                state = str[i];
            } else {
                Sprintf(buffer, SP_APPEND, " %d", (unsigned int)str[i]);
                state = 0;
            }
        }
        nolog++;
        norecord++;
        world_output(xsock->world, new_aline(buffer->s, 0));
        norecord--;
        nolog--;
    }
}

static void telnet_send(cmd)
    String *cmd;
{
    transmit(cmd->s, cmd->len);
    telnet_debug("sent", cmd->s, cmd->len);
}

int local_echo(flag)
    int flag;
{
    if (flag < 0)
        return xsock ? !IS_DO(xsock, TN_ECHO) : 1;
    if (!xsock || !(xsock->flags & SOCKTELNET)) return 0;
    if (flag != !IS_DO(xsock, TN_ECHO)) {
        if (flag)
            DONT(TN_ECHO);
        else
            DO(TN_ECHO);
        sockecho = flag;
    }
    return 1;
}


void transmit_window_size()
{
    Sock *sock;

    for (sock = hsock; sock; sock = sock->next)
        if (IS_WILL(sock, TN_NAWS))
            do_naws(sock);
}

static void preferred_telnet_options()
{
    WILL(TN_NAWS);
    set_WONT(TN_NAWS);		/* so when we receive DO, we'll really do it */
#if 0
    WILL(TN_BINARY);		/* allow us to send 8-bit data */
    DO(TN_BINARY);		/* allow server to send 8-bit data */
#endif
}

CONST char *world_info(worldname, fieldname)
    CONST char *worldname, *fieldname;
{
    World *world;
    CONST char *result;
 
    world = worldname ? find_world(worldname) : xworld();
    if (!world) return "";
 
    if (!fieldname || strcmp("name", fieldname) == 0) {
        result = world->name;
    } else if (strcmp("type", fieldname) == 0) {
        result = world_type(world);
    } else if (strcmp("character", fieldname) == 0) {
        result = world_character(world);
    } else if (strcmp("password", fieldname) == 0) {
        result = world_pass(world);
    } else if (strcmp("host", fieldname) == 0) {
        result = worldname ? world->host : world->sock->host;
    } else if (strcmp("port", fieldname) == 0) {
        result = worldname ? world->port : world->sock->port;
    } else if (strcmp("mfile", fieldname) == 0) {
        result = world_mfile(world);
    } else if (strcmp("login", fieldname) == 0) {
        result = world->sock && world->sock->flags & SOCKLOGIN ? "1" : "0";
    } else if (strcmp("proxy", fieldname) == 0) {
        result = world->sock && world->sock->flags & SOCKPROXY ? "1" : "0";
    } else return NULL;
    return result ? result : "";
}

int nactive(worldname)
    CONST char *worldname;
{
    World *w;

    if (!worldname)
        return active_count;
    if (!(w = find_world(worldname)) || !w->sock)
        return 0;
    return w->sock->activity;
}

