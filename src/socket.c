/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: socket.c,v 35004.46 1997/04/04 02:21:46 hawkeye Exp $ */


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
#include <errno.h>
#include <sys/types.h>
#ifdef SYS_SELECT_H
# include SYS_SELECT_H
#endif
#ifdef _POSIX_VERSION
# include <sys/wait.h>
#endif
#include <sys/time.h>
#define TIME_H		/* prevent <time.h> in "tf.h" */
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


#ifdef HAVE_h_errno
# ifndef h_errno
extern int h_errno;
# endif
#else
static int h_errno;
#endif

#ifndef NO_NETDB
# include NETDB_H
# ifdef NONBLOCKING_GETHOST
   static int FDECL(nonblocking_gethost,(CONST char *name, struct in_addr *sin_addr));
# else
#  define nonblocking_gethost blocking_gethost
# endif
  static int FDECL(blocking_gethost,(CONST char *name, struct in_addr *sin_addr));
#else /* NO_NETDB */
# define nonblocking_gethost(name, sin_addr) (-1)
# define blocking_gethost(name, sin_addr) (-1)
#endif /* NO_NETDB */

#ifndef INADDR_NONE
# define INADDR_NONE 0xffffffff     /* should be in <netinet/in.h> */
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
    TIME_T time;		/* time of last activity */
    char state;			/* state of parser finite state automaton */
} Sock;


static Sock *FDECL(find_sock,(CONST char *name));
static void  FDECL(wload,(World *w));
static int   FDECL(fg_sock,(Sock *sock, int quiet));
static int   FDECL(get_host_address,(CONST char *name, struct in_addr *sin_addr));
static int   FDECL(openconn,(Sock *new));
static int   FDECL(establish,(Sock *new));
static void  NDECL(fg_live_sock);
static void  NDECL(nuke_dead_socks);
static void  FDECL(nukesock,(Sock *sock));
static void  FDECL(handle_prompt,(CONST char *str, int confirmed));
static void  NDECL(handle_socket_line);
static void  NDECL(handle_socket_input);
static int   FDECL(transmit,(CONST char *s, unsigned int len));
static void  FDECL(telnet_send,(String *cmd));
static void  FDECL(telnet_recv,(int cmd, int opt));
static int   FDECL(is_quiet,(CONST char *str));
static int   FDECL(is_bamf,(CONST char *str));
static void  FDECL(do_naws,(Sock *sock));
static void  FDECL(telnet_debug,(CONST char *dir, CONST char *str, int len));
static void  NDECL(preferred_telnet_options);

#define killsock(s) \
    (((s)->flags |= SOCKDEAD), ((s)->world->sock = NULL), dead_socks++)

#ifndef CONN_WAIT
#define CONN_WAIT 400000
#endif

#ifndef PROC_WAIT
#define PROC_WAIT 100000
#endif

extern int errno;
extern int restrict;
extern int sockecho;		/* echo input? */
extern int need_refresh;	/* Does input need refresh? */
#ifndef NO_PROCESS
extern TIME_T proctime;		/* when next process should run */
#else
# define proctime 0
#endif

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

#define TN_BINARY	'\000'	/* transmit binary */
#define TN_ECHO		'\001'	/* echo option */
#define TN_SGA		'\003'	/* suppress GOAHEAD option */
#define TN_STATUS	'\005'	/* not used */
#define TN_TIMING_MARK	'\006'	/* not used */
#define TN_TTYPE	'\030'	/* not used */
#define TN_SEND_EOR	'\031'	/* transmit EOR */
#define TN_NAWS		'\037'	/* negotiate about window size */
#define TN_TSPEED	'\040'	/* not used */
#define TN_LINEMODE	'\042'	/* not used */

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
TIME_T mail_update = 0;		/* next mail check (0==immediately) */
TIME_T clock_update = 0;	/* next clock update (0==immediately) */
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

    setivar("connect", !!TF_NONBLOCK, FALSE);

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
    static TIME_T now, earliest;    /* static, so main_loop() can recurse */
    int count;
    static int depth = 0;
    struct timeval tv, *tvp;
    struct timeval refresh_tv;
    extern int pending_line, pending_input;
    extern int read_depth;
    extern int low_memory_warning;

    depth++;
    while (!quit_flag) {
        if (depth > 1 && interrupted()) break;

        /* deal with pending signals */
        /* at loop beginning in case of signals before main_loop() */ 
        process_signals();

        /* run processes */
        /* at loop beginning in case of processes before main_loop() */ 
        now = time(NULL);
        if (proctime && proctime <= now) runall();

        if (low_memory_warning) {
            low_memory_warning = 0;
            oputs("% WARNING: memory is low.  Try reducing history sizes.");
        }

        if (quit_flag) break;

        /* figure out when next event is so select() can timeout then */
        now = time(NULL);
        earliest = proctime;
        if (maildelay > 0) {
            if (now >= mail_update) {
                check_mail();
                mail_update = now + maildelay;
            }
            if (!earliest || (mail_update < earliest))
                earliest = mail_update;
        }
        if (visual && clock_flag) {
            if (now >= clock_update)
                status_bar(STAT_CLOCK);
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
            tv.tv_sec = earliest - now;
            tv.tv_usec = 0;
            if (tv.tv_sec <= 0) {
                tv.tv_sec = 0;
            } else if (proctime && proctime - now == 1) {
                tv.tv_sec = 0;
                tv.tv_usec = PROC_WAIT;
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
            if (errno != EINTR) die("main_loop: select", errno);

        } else {
            if (count == 0) {
                /* select() must have exited due to timeout. */
                do_refresh();
            }

            /* check for user input */
            if (pending_input || FD_ISSET(STDIN_FILENO, &active)) {
                if (FD_ISSET(STDIN_FILENO, &active)) count--;
                do_refresh();
                if (!handle_keyboard_input()) {
                    /* input is closed, stop reading it */
                    FD_CLR(STDIN_FILENO, &readers);
                }
            }

            /* check for socket completion/activity */
            for (xsock = hsock; count && xsock; xsock = xsock->next) {
                if (FD_ISSET(xsock->fd, &connected) && !(xsock->flags & SOCKRESOLVING)) {
                    count--;
                    establish(xsock);
                } else if (FD_ISSET(xsock->fd, &active)) {
                    count--;
                    if (xsock->flags & SOCKRESOLVING) {
                        openconn(xsock);
                    } else if (xsock->flags & SOCKPENDING) {
                        establish(xsock);
                    } else if (xsock == fsock || background) {
                        handle_socket_input();
                    } else {
                        FD_CLR(xsock->fd, &readers);
                    }
                }
            }

            /* fsock may have changed during loop above; xsock certainly did. */
            xsock = fsock;

            /* other active fds must be from command /quotes. */
            if (count) proctime = time(NULL);
        }

        /* handle "/dokey newline" or keyboard '\n' */
        if (pending_line) {
            pending_line = FALSE;
            if (read_depth) break;  /* end of tf read() */
            handle_input_line();
        }

        /* garbage collection */
        if (dead_socks) fg_live_sock();
        if (depth == 1) {
            if (dead_socks) nuke_dead_socks(); /* at end in case of quitdone */
            nuke_dead_macros();
        }
    }

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

void tog_bg()
{
    Sock *sock;
    if (background)
        for (sock = hsock; sock; sock = sock->next)
            if (!(sock->flags & (SOCKDEAD | SOCKPENDING)))
                FD_SET(sock->fd, &readers);
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

TIME_T sockidle(name)
    CONST char *name;
{
    Sock *sock;

    sock = *name ? find_sock(name) : xsock;
    return sock ? time(NULL) - sock->time : -1;
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
    if (restrict >= RESTRICT_FILE) return;
    if (*w->mfile || ((w = get_default_world()) && *w->mfile))
        do_file_load(w->mfile, FALSE);
}


/* bring a socket into the foreground */
static int fg_sock(sock, quiet)
    Sock *sock;
    int quiet;
{
    int oldecho = sockecho;
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

    /* The display sequence is optimized to minimize output codes. */
    status_bar(STAT_WORLD);

    if (sock) {
        FD_SET(sock->fd, &readers);
        if (sock->activity) {
            --active_count;
            status_bar(STAT_ACTIVE);
        }
        do_hook(H_WORLD, (sock->flags & SOCKDEAD) ?
            "---- World %s (dead) ----" : "---- World %s ----",
           "%s", sock->world->name);
        if (sock->activity) {
            flushout_queue(sock->queue, quiet);
            sock->activity = 0;
        }
        tog_lp();
        update_prompt(sock->prompt);
        if (sockmload) wload(sock->world);
    } else {
        do_hook(H_WORLD, "---- No world ----", "");
        update_prompt(NULL);
    }
    depth--;
    return 1;
}

int handle_fg_command(args)
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
        default:   return 0;
        }
    }

    if (nosock) {
        return fg_sock(NULL, quiet);

    } else if (dir) {
        Sock *stop;
        if (!hsock) return 0;
        stop = sock = (fsock ? fsock : hsock);
        do {
            sock = (dir > 0) ? (sock->next ? sock->next : hsock) :
                               (sock->prev ? sock->prev : tsock);
        } while ((sock->flags & SOCKPENDING) && sock != stop);
        return fg_sock(sock, quiet);
    }

    sock = (!*args) ? hsock : find_sock(args);
    if (!sock || sock->flags & SOCKPENDING) {
        if (!noerr) eprintf("not connected to '%s'", args);
        return 0;
    }
    return fg_sock(sock, quiet);
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
        if (restrict >= RESTRICT_WORLD)
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

    if (world->sock) {
        eprintf("socket to %s already exists", world->name);
        return 0;
    }

    /* create and initialize new Sock */
    if (!(xsock = world->sock = (Sock *) MALLOC(sizeof(struct Sock)))) {
        eprintf("opensock: not enough memory");
        return 0;
    }
    xsock->world = world;
    xsock->prev = tsock;
    tsock = *(tsock ? &tsock->next : &hsock) = xsock;
    xsock->fd = -1;
    xsock->state = '\0';
    xsock->attrs = 0;
    VEC_ZERO(&xsock->tn_do);
    VEC_ZERO(&xsock->tn_will);
    xsock->flags = (autologin ? SOCKLOGIN : 0);
    xsock->activity = 0;
    xsock->time = time(NULL);
    quietlogin = quietlogin && autologin && *world->character;
    xsock->numquiet = quietlogin ? MAXQUIET : 0;
    Stringinit(xsock->buffer);
    xsock->prompt = NULL;
    init_queue(xsock->queue = (Queue *)XMALLOC(sizeof(Queue)));
    xsock->next = NULL;

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

    if (isdigit(*xsock->port)) {
        xsock->addr.sin_port = htons(atoi(xsock->port));
#ifndef NO_NETDB
    } else if ((service = getservbyname(xsock->port, "tcp"))) {
        xsock->addr.sin_port = service->s_port;
#endif
    } else {
        CONFAIL(world->name, xsock->port, "no such service");
        nukesock(xsock);
        return 0;
    }

    xsock->flags |= SOCKRESOLVING;
    xsock->fd = get_host_address(xsock->host, &xsock->addr.sin_addr);
    if (xsock->fd == 0) {
        /* The name lookup succeeded */
        xsock->flags &= ~SOCKRESOLVING;
        return openconn(xsock);
    } else if (xsock->fd < 0) {
        /* The name lookup failed */
        CONFAIL(world->name, xsock->host,
            h_errno ? hstrerror(h_errno) : "name resolution failed");
        nukesock(xsock);
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
    if (xsock->flags & SOCKRESOLVING) {
        xsock->flags &= ~SOCKRESOLVING;
        FD_CLR(xsock->fd, &readers);
        if (read(xsock->fd, &err, sizeof(err)) < 0 || err != 0) {
            if (!err)
                CONFAIL(xsock->world->name, "read", strerror(errno));
            else
                CONFAIL(xsock->world->name, xsock->host, hstrerror(err));
            close(xsock->fd);
            killsock(xsock);
#ifdef PLATFORM_UNIX
            wait(NULL);
#endif /* PLATFORM_UNIX */
            return 0;
        }
        read(xsock->fd, (char*)&xsock->addr.sin_addr, sizeof(struct in_addr));
        close(xsock->fd);
#ifdef PLATFORM_UNIX
        wait(NULL);
#endif /* PLATFORM_UNIX */
    }

    /* Jump back here if we start a nonblocking connect and then discover
     * that the platform has a broken read() or select().
     */
    retry:

    if ((xsock->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        CONFAIL(xsock->world->name, "socket", strerror(errno));
        killsock(xsock);
        return 0;
    }
    if (xsock->fd >= nfds) nfds = xsock->fd + 1;

    if (!TF_NONBLOCK) {
        setivar("connect", 0, FALSE);
    } else if (async_conn) {
        /* note: 3rd arg to fcntl() is optional on Unix, but required by OS/2 */
        if ((flags = fcntl(xsock->fd, F_GETFL, 0)) < 0) {
            operror("Can't make socket nonblocking: F_GETFL fcntl");
            setivar("connect", 0, FALSE);
        } else if ((fcntl(xsock->fd, F_SETFL, flags | TF_NONBLOCK)) < 0) {
            operror("Can't make socket nonblocking: F_SETFL fcntl");
            setivar("connect", 0, FALSE);
        }
    }

    xsock->flags |= SOCKPENDING;
    if (connect(xsock->fd, (struct sockaddr*)&xsock->addr, sizeof(xsock->addr))
        == 0)
    {
        /* The connection completed successfully. */
        xsock->flags &= ~SOCKPENDING;
        return establish(xsock);

#ifdef EINPROGRESS
    } else if (errno == EINPROGRESS) {
        /* The connection needs more time.  It will select() as writable when
         * it has connected, or readable when it has failed.  We select() it
         * briefly here so "relatively fast" looks "immediate" to the user.
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
 * connected to a name lookup process or thread.
 */
static int get_host_address(name, sin_addr)
    CONST char *name;
    struct in_addr *sin_addr;
{
    h_errno = 0;  /* for systems that don't set h_errno */

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
    return (async_name) ?
        nonblocking_gethost(name, sin_addr) : blocking_gethost(name, sin_addr);
}

#ifndef NO_NETDB

static int blocking_gethost(name, sin_addr)
    CONST char *name;
    struct in_addr *sin_addr;
{
    struct hostent *host;

    if ((host = gethostbyname(name))) {
        h_errno = 0;
        memcpy((GENERIC *)sin_addr, (GENERIC *)host->h_addr, sizeof(*sin_addr));
        return 0;
    }
    return -1;
}

# ifdef PLATFORM_OS2
typedef struct _threadpara {
    char *hostname;
    int   pipe;
} threadpara;

void os2waitforhostname(targs)
    threadpara *targs;
{
    struct hostent *host;

    if ((host = gethostbyname(targs->hostname))) {
        h_errno = 0;
        write(targs->pipe, &h_errno, sizeof(h_errno));
        write(targs->pipe,(GENERIC *)host->h_addr, sizeof(struct in_addr));
    } else {
        write(targs->pipe, &h_errno, sizeof(h_errno));
    }
    close(targs->pipe);
    FREE(targs->hostname);
    FREE(targs);
}
# endif /* PLATFORM_OS2 */

#ifdef NONBLOCKING_GETHOST
static int nonblocking_gethost(name, sin_addr)
    CONST char *name;
    struct in_addr *sin_addr;
{
    int fds[2];

    h_errno = 1;  /* for systems that don't set h_errno */

    if (pipe(fds) < 0) return -1;

#ifdef PLATFORM_UNIX
 {
    int pid;
    struct hostent *host;

    pid = fork();
    if (pid > 0) {
        /* parent */
        close(fds[1]);
        return fds[0];
    } else if (pid == 0) {
        /* child */
        close(fds[0]);
        if ((host = gethostbyname(name))) {
            h_errno = 0;
            write(fds[1], &h_errno, sizeof(h_errno));
            write(fds[1], (GENERIC *)host->h_addr, sizeof(struct in_addr));
        } else {
            write(fds[1], &h_errno, sizeof(h_errno));
        }
        exit(0);
    }
 }
#endif
#ifdef PLATFORM_OS2
 {
    threadpara *tpara;
  
    setmode(fds[0],O_BINARY);
    setmode(fds[1],O_BINARY);
    if ((tpara = XMALLOC(sizeof(threadpara)))) {
        tpara->pipe = fds[1];
        tpara->hostname = STRDUP(name);

        if (_beginthread(os2waitforhostname,NULL,0x8000,(void *)tpara) != -1)
            return(fds[0]);
    }
 }

#endif
    /* failed */
    close(fds[0]);
    close(fds[1]);
    return -1;
}
#endif /* NONBLOCKING_GETHOST */

#endif /* NO_NETDB */


/* Establish a sock for which connect() has completed. */
static int establish(sock)
    Sock *sock;
{
    xsock = sock;
#ifdef EINPROGRESS
    if (xsock->flags & SOCKPENDING) {
        /* Old method: If read(fd, buf, 0) fails, the connect() failed, and
         * errno will explain why.  Problem: on some systems, a read() of
         * 0 bytes is always successful.
         */
        /* CURRENT METHOD: If a second connect() fails with EISCONN, the first
         * connect() worked.  On the slim chance that the first failed, but
         * the second worked, go with that.  Otherwise, use getsockopt() to
         * find out why the first failed.  Some broken socket implementations
         * give the wrong errno, but there's nothing we can do about that.
         * If SO_ERROR isn't available, use read() to get errno.
         * This method works for all systems, as well as SOCKS 4.2beta.
         * Disadvantage: extra net traffic on failure.
         */
        /* Alternative: replace second connect() with getpeername(), and
         * check for ENOTCONN.  Disadvantage: doesn't work with SOCKS, etc.
         */

        if ((connect(xsock->fd, (struct sockaddr*)&xsock->addr,
            sizeof(xsock->addr)) < 0) && errno != EISCONN)
        {
            CONST char *errmsg = "nonblocking connect";
            int err, len = sizeof(err);

            killsock(xsock);
# ifdef SO_ERROR
            if (getsockopt(xsock->fd, SOL_SOCKET, SO_ERROR,
                (GENERIC*)&err, &len) < 0)
            {
                errmsg = "getsockopt";
                err = errno;
            }
# else
            {
                char ch;
                read(xsock->fd, &ch, 1);   /* must fail */
                err = errno;
                errmsg = "nonblocking connect/read";
            }
# endif /* SO_ERROR */
            CONFAIL(xsock->world->name, errmsg, strerror(err));
            return 0;
        }

        /* connect() worked.  Clear the pending stuff, and get on with it. */
        xsock->flags &= ~SOCKPENDING;
        FD_CLR(xsock->fd, &writers);
    }
#endif /* EINPROGRESS */

#ifndef NO_HISTORY
    /* skip any old undisplayed lines */
    flush_hist(xsock->world->history);
#endif

    if (xsock->flags & SOCKPROXY) {
        FREE(xsock->host);
        FREE(xsock->port);
        xsock->host = STRDUP(xsock->world->host);
        xsock->port = STRDUP(xsock->world->port);
        do_hook(H_PROXY, "", "%s", xsock->world->name);
    }

    wload(xsock->world);

    if (!(xsock->flags & SOCKPROXY)) {
        do_hook(H_CONNECT, "%% Connection to %s established.", "%s",
            xsock->world->name);

        if (login && xsock->flags & SOCKLOGIN) {
            World *w;
            w = (*xsock->world->character) ? xsock->world : get_default_world();
            if (w && *w->character)
                do_hook(H_LOGIN, NULL, "%s %s %s", xsock->world->name,
                    w->character, w->pass);
            xsock->flags &= ~SOCKLOGIN;
        }
    }

    FD_SET(xsock->fd, &readers);
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
    if (sock->fd > 0) {
        FD_CLR(sock->fd, &readers);
        FD_CLR(sock->fd, &writers);
        close(sock->fd);
        if (sock->activity) {
            --active_count;
            status_bar(STAT_ACTIVE);
        }
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
int handle_dc_command(args)
    char *args;
{
    Sock *s;

    if (!*args) {
        if (!xsock) {
            eprintf("no current socket");
            return 0;
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
            return 0;
        }
    }
    return 1;
}

/* display list of open sockets and their state. */
int handle_listsockets_command(args)
    char *args;
{
    Sock *sock;
    char buffer[81], *s;
    TIME_T now;
    int idle, opt;
    int count = 0, shortflag = FALSE;

    startopt(args, "s");
    while ((opt = nextopt(&args, NULL))) {
        switch(opt) {
        case 's': shortflag = TRUE; break;
        default:  return 0;
        }
    }

    if (shortflag) {
        for (sock = hsock; sock; sock = sock->next) {
            count++;
            oputs(sock->world->name);
        }
        return count;
    }

    if (!hsock) {
        oputs("% Not connected to any sockets.");
        return 0;
    }

    now = time(NULL);

    oputs("     LINES IDLE TYPE      NAME            HOST                       PORT");
    for (sock = hsock; sock; sock = sock->next) {
        count++;
        buffer[0] = ((sock == xsock) ? '*' : ' ');
        buffer[1] = ((sock->flags & SOCKDEAD) ? '!' :
            (sock->flags & (SOCKPENDING | SOCKRESOLVING)) ? '?' :
            (sock->flags & (SOCKPROXY)) ? '@' : ' ');
        s = buffer+2;
        if (sock == fsock)
            sprintf(s, " %7s", "foregnd");
        else
            sprintf(s, " %7d", sock->activity);
        s = strchr(s, '\0');
        idle = now - sock->time;
        if (idle < (60))
            sprintf(s, " %3ds", idle);
        else if ((idle /= 60) < 60)
            sprintf(s, " %3dm", idle);
        else if ((idle /= 60) < 24)
            sprintf(s, " %3dh", idle);
        else if ((idle /= 24) < 1000)
            sprintf(s, " %3dd", idle);
        else
            sprintf(s, " long");
        s = strchr(s, '\0');
        sprintf(s, " %-9.9s %-15.15s %-26.26s %-6.6s",
            sock->world->type, sock->world->name, sock->host, sock->port);
        oputs(buffer);
    }
    return count;
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
    int numwritten;

    if (!xsock || xsock->flags & (SOCKDEAD | SOCKPENDING)) return 0;
    while (numtowrite) {
        numwritten = send(xsock->fd, str, numtowrite, 0);
        if (numwritten < 0) {
            if (errno == EAGAIN
#ifdef EWOULDBLOCK
                                || errno == EWOULDBLOCK
#endif
                                                       ) {
                fd_set writefds;
                numwritten = 0;
                FD_ZERO(&writefds);
                FD_SET(xsock->fd, &writefds);
                if (select(xsock->fd + 1, NULL, &writefds, NULL, NULL) < 0)
                    if (errno == EINTR) break;
            } else {
                killsock(xsock);
                do_hook(H_DISCONNECT,
                    "%% Connection to %s closed: %s: %s", "%s %s %s",
                    xsock->world->name, "send", strerror(errno));
                return 0;
            }
        }
        numtowrite -= numwritten;
        str += numwritten;
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
        Stringterm(xsock->buffer, 0);
        free_aline(xsock->prompt);
        xsock->prompt = NULL;
        if (xsock == fsock) update_prompt(xsock->prompt);
    }

    buf = XMALLOC(2 * len + 3);
    while (j < len) {
        if (src[j] == TN_IAC)
            buf[i++] = TN_IAC;    /* double IAC */
        buf[i] = unmapchar(src[j]);
        i++, j++;
    }

    if (eol_flag) {
        /* Append CR LF in telnet NVT mode, or LF in telnet BINARY mode */
        if (!IS_WILL(xsock, TN_BINARY))
            buf[i++] = '\r';
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
    xsock->time = incoming_text->time;
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
    if (world->sock && !(gag && (aline->attrs & F_GAG))) {
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
                status_bar(STAT_ACTIVE);
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

void tog_lp()
{
    if (!fsock) return;
    if (lpflag) {
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
            free_aline(fsock->prompt);
            fsock->prompt = NULL;
            set_refresh_pending(REF_PHYSICAL);
        }
    }
}

int handle_prompt_command(args)
    char *args;
{
    if (xsock) handle_prompt(args, TRUE);
    return !!xsock;
}

static void handle_prompt(str, confirmed)
    CONST char *str;
    int confirmed;
{
    if (lpquote) proctime = time(NULL);
    if (xsock->prompt) {
        free_aline(xsock->prompt);
        xsock->prompt = NULL;
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
    if (xsock == fsock) update_prompt(xsock->prompt);
    if (confirmed) xsock->flags |= SOCKPROMPT;
    else xsock->flags &= ~SOCKPROMPT;
    xsock->numquiet = 0;
}

static void telnet_recv(cmd, opt)
    int cmd, opt;
{
    if (telopt) {
        char buf[4];
        sprintf(buf, "%c%c%c", TN_IAC, cmd, opt);
        telnet_debug("recv", buf, 3);
    }
}

/* handle input from current socket */
static void handle_socket_input()
{
    char *place, localchar, buffer[4096];
    fd_set readfds;
    int count, n, total = 0;
    struct timeval tv;

#define SPAM (4*1024)       /* break loop if this many chars are received */

    if (xsock->prompt && !(xsock->flags & SOCKPROMPT)) {
        /* We assumed last text was a prompt, but now we have more text, so
         * we now assume that they are both part of the same long line.  (If
         * we're wrong, the previous prompt appears as output.  But if we did
         * the opposite, a real begining of a line would never appear in the
         * output window; that would be a worse mistake.)
         */
        free_aline(xsock->prompt);
        xsock->prompt = NULL;
        if (xsock == fsock) update_prompt(xsock->prompt);
    }

    do {  /* while (n > 0 && !interrupted() && (total += count) < SPAM) */
        do count = recv(xsock->fd, buffer, sizeof(buffer), 0);
            while (count < 0 && errno == EINTR);
        if (count <= 0) {
#ifdef SUNOS_5_4
            if (errno == EAGAIN || errno == EINVAL) {
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
                CONFAIL(xsock->world->name, "recv", strerror(errno));
            else do_hook(H_DISCONNECT, (count < 0) ?
                    "%% Connection to %s closed: %s: %s" :
                    "%% Connection to %s closed by foreign host.",
                    (count < 0) ? "%s %s %s" : "%s",
                    xsock->world->name, "recv", strerror(errno));
            return;
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
                    if (telopt)
                        oprintf("rcvd IAC %s",
                            telnet_label[(UCHAR)*place]);
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
                    if (telopt)
                        oprintf("rcvd unexpected IAC %d", *place);
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
                } else if (*place == TN_SGA) {
                    DO(TN_SGA);
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
                if (isprint(localchar))
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
              (*place == '?' || *place == ';' || isalnum(*place))) {
                /* ANSI terminal sequences contain: CSI, an optional '?',
                 * any number of digits and ';'s, and a letter.
                 */
                if (isalpha(*place)) xsock->state = *place;

            } else if (*place == '\07' && emulation == EMUL_ANSI_ATTR) {
                Stringadd(xsock->buffer, *place);
                xsock->state = *place;

            } else if (!isprint(localchar) &&
              (emulation == EMUL_PRINT || emulation == EMUL_ANSI_STRIP)) {
                /* not printable */
                xsock->state = *place;

            } else {
                /* Normal character.  The isprint() loop is a fast heuristic
                 * to find next potentially interesting character. */
                char *end;
                Stringadd(xsock->buffer, localchar);
                end=++place;
                while (isprint(*end) && *end != TN_IAC && end - buffer < count)
                    end++;
                Stringncat(xsock->buffer, (char*)place, end - place);
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

    } while (n > 0 && !interrupted() && (total += count) < SPAM);

    /* If lpflag is on and we got a partial line from the fg world,
     * assume the line is a prompt.
     */
    if (lpflag && xsock == fsock && xsock->buffer->len) {
        handle_prompt(xsock->buffer->s, FALSE);
    }
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

    if (!bamf || restrict >= RESTRICT_WORLD) return 0;
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
                    Sprintf(buffer, SP_APPEND, " %d", (int)str[i]);
                state = str[i];
            } else {
                Sprintf(buffer, SP_APPEND, " %d", (int)str[i]);
                state = 0;
            }
        }
        oputs(buffer->s);
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

CONST char *world_info(fieldname)
    CONST char *fieldname;
{
    World *world, *def;
 
    world = xworld();
    if (!world) return "";
    if (!(def = get_default_world())) def = world;
 
    if (strcmp("name", fieldname) == 0) {
        return world->name;
    } else if (strcmp("character", fieldname) == 0) {
        return (*world->character) ? world->character : def->character;
    } else if (strcmp("password", fieldname) == 0) {
        return (*world->pass) ? world->pass : def->pass;
    } else if (strcmp("host", fieldname) == 0) {
        return world->sock->host;
    } else if (strcmp("port", fieldname) == 0) {
        return world->sock->port;
    } else if (strcmp("mfile", fieldname) == 0) {
        return (*world->mfile) ? world->mfile : def->mfile;
    } else if (strcmp("type", fieldname) == 0) {
        return (*world->type) ? world->type : def->type;
    } else return NULL;
}

