/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: socket.c,v 33000.10 1994/04/23 23:29:00 hawkeye Exp $ */


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
#include <sys/time.h>
#define SYS_TIME_H         /* prevent <time.h> in "port.h" */
#include <ctype.h>
#include <fcntl.h>
#include <sys/file.h>      /* for FNONBLOCK on SVR4, hpux, ... */
#include <sys/socket.h>

#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_SYS_IN_H
# include <sys/in.h>
#endif
#ifdef HAVE_SYS_NETINET_IN_H
# include <sys/netinet/in.h>
#endif

#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#ifdef HAVE_SYS_INET_H
# include <sys/inet.h>
#endif

#ifndef NO_NETDB
# include <netdb.h>
#endif

#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "fd_set.h"
#include "util.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "output.h"
#include "process.h"
#include "macro.h"
#include "keyboard.h"
#include "command.h"
#include "commands.h"
#include "signals.h"
#include "search.h"

#ifndef INADDR_NONE
# define INADDR_NONE 0xffffffff     /* should be in <netinet/in.h> */
#endif

#ifdef FNDELAY                      /* most BSD-like systems (4.2 and later). */
# define TF_NONBLOCK FNDELAY
#else
# ifdef O_NDELAY                    /* most BSD-like systems (4.2 and later). */
#  define TF_NONBLOCK O_NDELAY
# else
#  ifdef FNONBLOCK                  /* POSIX? */
#   define TF_NONBLOCK FNONBLOCK
#  else
#   ifdef O_NONBLOCK                /* POSIX.  (Doesn't work on AIX?) */
#    define TF_NONBLOCK O_NONBLOCK
#   else
#    ifdef FNBIO                    /* SysV?  (Doesn't work on SunOS...) */
#     define TF_NONBLOCK FNBIO
#    else
#     ifdef FNONBIO                 /* ???  (Doesn't work on SunOS...) */
#      define TF_NONBLOCK FNONBIO
#     else
#      ifdef FNONBLK                /* ??? */
#       define TF_NONBLOCK FNONBLK
#      endif
#     endif
#    endif
#   endif
#  endif
# endif
#endif

static void  FDECL(wload,(World *w));
static Sock *FDECL(find_sock,(char *name));
static void  FDECL(announce_world,(Sock *s));
static void  FDECL(fg_sock,(Sock *sock));
static void  NDECL(bg_sock);
static int   FDECL(get_host_address,(char *name, struct in_addr *addr));
static int   FDECL(establish,(Sock *new));
static void  NDECL(nuke_dead_socks);
static void  FDECL(nukesock,(Sock *sock));
static void  FDECL(login_hook,(Sock *sock));
static void  FDECL(handle_prompt,(char *str, int confirmed));
static void  NDECL(handle_socket_line);
static void  NDECL(handle_socket_input);
static int   FDECL(transmit,(char *s, unsigned int len));
static void  FDECL(telnet_send,(int cmd, int opt));
static void  FDECL(telnet_recv,(int cmd, int opt));
static int   FDECL(keep_quiet,(char *what));
static int   FDECL(handle_portal,(char *what));

#define killsock(s)  (((s)->flags |= SOCKDEAD), dead_socks++)

#ifndef CONN_WAIT
#define CONN_WAIT 500000
#endif

#ifndef PROC_WAIT
#define PROC_WAIT 100000
#endif

extern int restrict;
extern int echoflag;		/* echo input? */
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
static int dead_socks = 0;	/* Number of unnuked dead sockets */
static char *telnet_label[256];

#define TN_ECHO		0001	/* echo option */
#define TN_SGA		0003	/* suppress GOAHEAD option */
#define TN_STATUS	0005  /* not used */
#define TN_TIMING_MARK	0006	/* not used */
#define TN_TTYPE	0030	/* not used */
#define TN_EOR_OPT	0031	/* EOR option */
#define TN_NAWS		0037	/* not used */
#define TN_TSPEED	0040	/* not used */
#define TN_LINEMODE	0042	/* not used */

#define TN_EOR		0357	/* End-Of-Record */
#define TN_SE		0360	/* not used */
#define TN_NOP		0361	/* not used */
#define TN_DATA_MARK	0362	/* not used */
#define TN_BRK		0363	/* not used */
#define TN_IP		0364	/* not used */
#define TN_AO		0365	/* not used */
#define TN_AYT		0366	/* not used */
#define TN_EC		0367	/* not used */
#define TN_EL		0370	/* not used */
#define TN_GA		0371	/* Go Ahead */
#define TN_SB		0372	/* not used */
#define TN_WILL		0373	/* I offer to ~, or ack for DO */
#define TN_WONT		0374	/* I will stop ~ing, or nack for DO */
#define TN_DO		0375	/* Please do ~?, or ack for WILL */
#define TN_DONT		0376	/* Stop ~ing!, or nack for WILL */
#define TN_IAC		0377	/* telnet Is A Command character */

#define ANSI_CSI	0233	/* ANSI terminal Command Sequence Intro */

int quit_flag = FALSE;          /* Are we all done? */
Sock *fsock = NULL;		/* foreground socket */
Sock *xsock = NULL;		/* current (transmission) socket */
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
    FD_SET(0, &readers);
    nfds = 1;

    for (i = 0; i < 256; i++) telnet_label[i] = NULL;

    telnet_label[TN_ECHO]		= "ECHO";
    telnet_label[TN_SGA]		= "SGA";
    telnet_label[TN_STATUS]		= "STATUS";
    telnet_label[TN_TIMING_MARK]	= "TIMING_MARK";
    telnet_label[TN_TTYPE]		= "TTYPE";
    telnet_label[TN_EOR_OPT]		= "EOR_OPT";
    telnet_label[TN_NAWS]		= "NAWS";
    telnet_label[TN_TSPEED]		= "TSPEED";
    telnet_label[TN_LINEMODE]		= "LINEMODE";
    telnet_label[TN_EOR]		= "EOR";
    telnet_label[TN_SE]			= "SE";
    telnet_label[TN_NOP]		= "NOP";
    telnet_label[TN_DATA_MARK]		= "DATA_MARK";
    telnet_label[TN_BRK]		= "BRK";
    telnet_label[TN_IP]			= "IP";
    telnet_label[TN_AO]			= "AO";
    telnet_label[TN_AYT]		= "AYT";
    telnet_label[TN_EC]			= "EC";
    telnet_label[TN_EL]			= "EL";
    telnet_label[TN_GA]			= "GA";
    telnet_label[TN_SB]			= "SB";
    telnet_label[TN_WILL]		= "WILL";
    telnet_label[TN_WONT]		= "WONT";
    telnet_label[TN_DO]			= "DO";
    telnet_label[TN_DONT]		= "DONT";
    telnet_label[TN_IAC]		= "IAC";
}

/* main_loop
 * Here we mostly sit in select(), waiting for something to happen.
 * The select timeout is set for the earliest process, mail check,
 * or refresh event.  Signal processing and garbage collection is
 * done at the end of the loop, where we're in a "clean" state.
 */
void main_loop()
{
    int count;
    struct timeval tv, *tvp;
    TIME_T now, earliest;

    while (!quit_flag) {

        /* deal with pending signals */
        process_signals();

        /* garbage collection */
        if (dead_socks) nuke_dead_socks();
        nuke_dead_macros();

        now = time(NULL);
        /* note:  can't use now>=proctime as a criterion for runall:  we won't
         * catch command procs which are past due but just became readable.  */
        if (!lpquote) runall(now);

        /* figure out when next event is so select() can timeout then */
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
            if (now >= clock_update) {
                status_bar(STAT_CLOCK);
            }
            if (!earliest || (clock_update < earliest))
                earliest = clock_update;
        }

        /* flush pending tfscreen output */
        oflush();

        if (earliest) {
            tvp = &tv;
            tv.tv_sec = earliest - now;
            tv.tv_usec = 0;
            if (tv.tv_sec <= 0) {
                tv.tv_sec = 0;
            } else if (tv.tv_sec == 1) {
                tv.tv_sec = 0;
                tv.tv_usec = PROC_WAIT;
            }
        } else tvp = NULL;

        if (need_refresh) {
            if (!tvp || tvp->tv_sec > 0 || tvp->tv_usec > refreshtime) {
                tvp = &tv;
                tv.tv_sec = 0;
                tv.tv_usec = refreshtime;
            }
        }

        /* Find descriptors that need to be read.
         * Note: if the same descriptor appears in more than one fd_set, some
         * versions of select() count it only once, but some count it once
         * for each appearance in a set.
         */
        structcpy(active, readers);
        structcpy(connected, writers);
        count = select(nfds, &active, &connected, NULL, tvp);

        if (count < 0) {
            /* select() must have exited due to error or interrupt. */
            if (errno != EINTR) {
                perror("select");
                die("% Failed select in main_loop");
            }
        } else if (count == 0) {
            /* select() must have exited due to timeout. */
            do_refresh();
        } else {
            /* check for user input */
            if (FD_ISSET(0, &active)) {
                count--;
                do_refresh();
                handle_keyboard_input();
            }
            for (xsock = hsock; count && xsock; xsock = xsock->next) {
                if (FD_ISSET(xsock->fd, &connected)) {
                    count--;
                    establish(xsock);
                } else if (FD_ISSET(xsock->fd, &active)) {
                    count--;
                    if (xsock == fsock || background) handle_socket_input();
                    else FD_CLR(xsock->fd, &readers);
                }
            }
            xsock = fsock;
        }
    }
    disconnect_all();
    cleanup();

#ifdef DMALLOC
    {
        free_macros();
        handle_purgeworld_command("*");
        free_histories();
        free_term();
        free_vars();
        free_keyboard();
        debug_mstats("tf");
    }
#endif
    /* exit program */
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

/* find existing open socket to world <name> */
static Sock *find_sock(name)
    char *name;
{
    Sock *sock;

    for (sock = hsock; sock; sock = sock->next) {
        if (sock->flags & (SOCKDEAD | SOCKPENDING)) continue;
        if (!name || cstrcmp(sock->world->name, name) == 0) break;
    }
    return sock;
}

void tog_bg()
{
    Sock *sock;
    if (background)
        for (sock = hsock; sock; sock = sock->next)
            if (!(sock->flags & (SOCKDEAD | SOCKPENDING)))
                FD_SET(sock->fd, &readers);
}

/* Perform (*func)(world) on every open world */
void mapsock(func)
    void FDECL((*func),(World *world));
{
    Sock *sock;

    for (sock = hsock; sock; sock = sock->next)
        if (!(sock->flags & (SOCKDEAD | SOCKPENDING))) (*func)(sock->world);
}

/* get foreground world */
World *fworld()
{
    return fsock ? fsock->world : NULL;
}

/* get current operational world */
World *xworld()
{
    return xsock ? xsock->world : NULL;
}
  
/* load macro file for a world */
static void wload(w)
    World *w;
{
    World *d;

    if (restrict >= RESTRICT_FILE) return;
    if (*w->mfile) do_file_load(w->mfile, FALSE);
    else if ((d = get_default_world()) && *d->mfile)
        do_file_load(d->mfile, FALSE);
}

/* announce foreground world */
static void announce_world(s)
    Sock *s;
{
    status_bar(STAT_WORLD);
    if (!s)
        do_hook(H_WORLD, "---- No world ----", "");
    else if (s->flags & SOCKDEAD)
        do_hook(H_WORLD, "---- World %s (dead) ----", "%s", s->world->name);
    else do_hook(H_WORLD, "---- World %s ----", "%s", s->world->name);
}


/* bring a socket into the foreground */
static void fg_sock(sock)
    Sock *sock;
{
    Sock *oldsock = xsock;

    if (((fsock ? fsock->flags : 0) ^ (sock ? sock->flags : 0)) & SOCKECHO)
        set_refresh_pending(REF_LOGICAL);

    xsock = fsock = sock;
    if (sock) {
        FD_SET(sock->fd, &readers);
        if (sock->activity) {
            --active_count;
            status_bar(STAT_ACTIVE);
        }
        sock->activity = 0;
        announce_world(sock);
        flushout_queue(sock->queue);
        sock->activity = 0;
        echoflag = (sock->flags & SOCKECHO);
        tog_lp();
        update_prompt(sock->prompt);
        if (sockmload) wload(sock->world);
    } else {
        announce_world(NULL);
        update_prompt(NULL);
    }
    xsock = oldsock;
}

/* put fg socket in the background */
static void bg_sock()
{
    echoflag = TRUE;
    fsock = NULL;
}

int handle_fg_command(args)
    char *args;
{
    int opt, nosock = FALSE, silent = FALSE, dir = 0;
    World *world;
    Sock *sock;

    startopt(args, "nlqs<>");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
        case 'n':  nosock = TRUE;  break;
        case 's':  silent = TRUE;  break;
        case 'l':
        case 'q':  break;  /* accepted and ignored */
        case '<':  dir = -1;  break;
        case '>':  dir =  1;  break;
        default:   return 0;
        }
    }

    if (nosock) {
        if (fsock) {
            bg_sock();
            announce_world(NULL);
        }
        return 1;
    }

    if (dir) {
        Sock *stop;
        if (!hsock) return 0;
        stop = sock = (fsock ? fsock : hsock);
        do {
            sock = (dir > 0) ? (sock->next ? sock->next : hsock) :
                               (sock->prev ? sock->prev : tsock);
        } while ((sock->flags & SOCKPENDING) && sock != stop);

    } else if (!(world = find_world(*args ? args : NULL))) {
        if (!silent) tfprintf(tferr, "%S: no world %s", error_prefix(), args);
        return 0;

    } else if (!world->sock || world->sock->flags & SOCKPENDING) {
        if (!silent)
            tfprintf(tferr, "%S: not connected to %s", error_prefix(),
                world->name);
        return 0;

    } else {
        sock = world->sock;
    }

    if (sock == fsock) return 2;  /* already there */
    bg_sock();
    fg_sock(sock);
    return 1;
}

/* openworld
 * If (name && port), they are used as hostname and port number.
 * If (!port), name is used as the name of a world.  A NULL or empty name
 * corresponds to the default world.  Additionally, if name is NULL,
 * the CONFAIL hook will not be called if openworld() fails.
 */
int openworld(name, port, autologin, quietlogin)
    char *name, *port;
    int autologin, quietlogin;
{
    World *world = NULL;

    if (!port) {
        world = find_world(name);
        if (!world && name)
            do_hook(H_CONFAIL, "%% Connection to %s failed: %s", "%s %s",
                *name ? name : "default world", "no such world");
    } else {
        if (restrict >= RESTRICT_WORLD)
            tfputs("% \"/connect <host> <port>\" restricted", tferr);
        else {
            world = new_world(NULL, "", "", name, port, "", "");
            world->flags |= WORLD_TEMP;
        }
    }

    return world ? opensock(world, autologin, quietlogin) : 0;
}

int opensock(world, autologin, quietlogin)
    World *world;
    int autologin, quietlogin;
{
    int flags;
    Sock *sock;
    struct timeval tv;
    fd_set writeable;
    struct sockaddr_in addr;
    struct servent *service;
    int size = sizeof(struct sockaddr_in);
    static int can_nonblock = TRUE;

    if (world->sock && !(world->sock->flags & SOCKDEAD)) {
        tfprintf(tferr, "%S: socket to %s already exists", error_prefix(),
            world->name);
        return 0;
    }

    /* create and initialize new Sock */
    world->sock = sock = (Sock *) MALLOC(sizeof(struct Sock));
    sock->world = world;
    sock->prev = tsock;
    if (tsock == NULL) {
        tsock = hsock = sock;
    } else {
        tsock = tsock->next = sock;
    }
    sock->fd = -1;
    sock->state = '\0';
    sock->flags = SOCKECHO | SOCKEDIT | SOCKTRAP | (autologin ? SOCKLOGIN : 0);
    sock->activity = 0;
    if (quietlogin && autologin && *sock->world->character)
        sock->numquiet = MAXQUIET;
    else
        sock->numquiet = 0;
    Stringinit(sock->buffer);
    Stringinit(sock->prompt);
    init_queue(sock->queue = (Queue *)MALLOC(sizeof(Queue)));
    sock->next = NULL;

    addr.sin_family = AF_INET;

    if (isdigit(*world->port)) {
        addr.sin_port = htons(atoi(world->port));
#ifndef NO_NETDB
    } else if ((service = getservbyname(world->port, "tcp"))) {
        addr.sin_port = service->s_port;
#endif
    } else {
        CONFAIL(world->name, world->port, "no such service");
        nukesock(sock);
        return 0;
    }

    if (!get_host_address(world->address, &addr.sin_addr)) {
        CONFAIL(world->name, world->address, "can't find host");
        nukesock(sock);
        return 0;
    }

    /* Jump back here if we start a nonblocking connect and then discover
     * that the platform has a broken read() or select().
     */
    retry:

    if ((sock->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        CONFAIL(world->name, "socket", STRERROR(errno));
        nukesock(sock);
        return 0;
    }
    if (sock->fd >= nfds) nfds = sock->fd + 1;

#ifdef TF_NONBLOCK
    if (can_nonblock) {
        if ((flags = fcntl(sock->fd, F_GETFL)) < 0) {
            operror("Can't make socket nonblocking: F_GETFL fcntl");
            can_nonblock = FALSE;
        } else if ((fcntl(sock->fd, F_SETFL, flags | TF_NONBLOCK)) < 0) {
            operror("Can't make socket nonblocking: F_SETFL fcntl");
            can_nonblock = FALSE;
        }
    }
#endif

    if (connect(sock->fd, (struct sockaddr*)&addr, size) == 0) {
        /* The connection completed successfully. */
        return establish(sock);

#ifdef EINPROGRESS
    } else if (errno == EINPROGRESS) {
        /* The connection needs more time.  It will select() as writable
         * when it has connected, or readable when it has failed.  We
         * select on it for a fraction of a second here so "immediate"
         * and "relatively fast" look the same to the user.
         */
        sock->flags |= SOCKPENDING;
        FD_ZERO(&writeable);
        FD_SET(sock->fd, &writeable);
        tv.tv_sec = 0;
        tv.tv_usec = CONN_WAIT;
        if (select(sock->fd + 1, NULL, &writeable, NULL, &tv) > 0) {
#if 0
            /* Is this really necessary??  (It is if select is broken and lies
             * about the descriptor being ready.  All /connects on such a system
             * will fail with "Socket is not connected" unless we do this).
             */
            char buf[1];
            if (read(sock->fd, buf, 0) < 0) {
                if (errno == ENOTCONN) {
                    /* select() is broken, so we try a blocking connect. */
                    close(sock->fd);
                    can_nonblock = FALSE;
                    goto retry; /* try again */
                } else {
                    CONFAIL(world->name, "connect/read", STRERROR(errno));
                    nukesock(sock);
                    return 0;
                }
            } else
#endif
            {
                /* The connection completed. */
                return establish(sock);
            }
        } else {
            /* select() must have returned 0, or -1 and errno==EINTR.  In
             * either case, the connection still needs more time.  So we
             * add the fd to the set being watched by the select() in
             * main_loop(), and don't waste any more time waiting here.
             */
            FD_SET(sock->fd, &writers);
            FD_SET(sock->fd, &readers);
            do_hook(H_PENDING, "%% Connection to %s in progress.", "%s",
                sock->world->name);
            return 2;
        }
#endif

    } else if (errno == EAGAIN) {
        /* A bug in SVR4.2 causes nonblocking connect() to (sometimes?)
         * incorrectly fail with EAGAIN.  The only thing we can do about
         * it is to try a blocking connect().
         */
        close(sock->fd);
        can_nonblock = FALSE;
        goto retry; /* try again */

    } else {
        /* The connection failed.  Give up. */
        CONFAIL(world->name, "connect", STRERROR(errno));
        nukesock(sock);
        return 0;
    }

}

/* Convert name or ip number string to an in_addr */
static int get_host_address(name, addr)
    char *name;
    struct in_addr *addr;
{
#ifndef NO_NETDB
    struct hostent *host;
#endif

    if ((addr->s_addr = inet_addr(name)) != INADDR_NONE) return 1;
#ifndef NO_NETDB
    /* Numeric format failed.  Try name format. */
    if ((host = gethostbyname(name))) {
        memcpy((GENERIC *)addr, (GENERIC *)host->h_addr, sizeof(addr));
        return 1;
    }
#endif
    return 0;
}

/* Establish a sock for which connect() has completed. */
static int establish(sock)
    Sock *sock;
{
    Sock *oldsock;

#ifdef EINPROGRESS
    if (sock->flags & SOCKPENDING) {
# if 0
        /* This method _should_ work, and indeed does on many systems.
         * But on some broken socket implementations (notably SunOS 5.x)
         * read() of 0 bytes on the socket _always_ fails with EAGAIN.
         */
        char buf[1];

        if (read(sock->fd, buf, 0) < 0) {
            killsock(sock);
            CONFAIL(sock->world->name, "connect/read", STRERROR(errno));
            return 0;
        }
# else
        /* If socket isn't connected, getpeername() will fail with ENOTCONN
         * (we don't care about the addr, just the errno).  If that happens,
         * we use getsockopt() to find out why the connect() failed.  Some
         * broken socket implementations give the wrong errno, but there's
         * nothing we can do about that.
         */
        int len, err = 0;
        struct sockaddr addr;
        char *errmsg;

        len = sizeof(addr);
        if (getpeername(sock->fd, &addr, &len) < 0) {
            len = sizeof(err);
            if (errno != ENOTCONN) {
                errmsg = "getpeername";
                err = errno;
#  ifdef SO_ERROR
            /* SO_ERROR isn't defined on hpux?  Screw 'em. */
            } else if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR,
              (GENERIC*)&err, &len) < 0) {
                errmsg = "getsockopt";
                err = errno;
#  endif /* SO_ERROR */
            } else errmsg = "connect/getsockopt";
            killsock(sock);
            CONFAIL(sock->world->name, errmsg, STRERROR(err));
            return 0;
        }
# endif /* 0 */

        /* connect() worked.  Clear the pending stuff, and get on with it. */
        sock->flags &= ~SOCKPENDING;
        FD_CLR(sock->fd, &writers);
    }
#endif /* EINPROGRESS */

#ifndef NO_HISTORY
    /* skip any old undisplayed lines */
    sock->world->history->index = sock->world->history->pos;
#endif

    oldsock = xsock;
    xsock = sock;
    wload(xsock->world);
    do_hook(H_CONNECT, "%% Connection to %s established.", "%s",
        xsock->world->name);
    login_hook(xsock);
    xsock = oldsock;
    return 1;
}

/* nukesock
 * Remove socket from list and free memory.  Should only be called on a
 * Sock which is known to have no references other than the socket list.
 */
static void nukesock(sock)
    Sock *sock;
{
    if (sock->world->sock == sock) {
        /* false if /connect follows close in same interation of main loop */
        sock->world->sock = NULL;
    }
    if (sock->world->flags & WORLD_TEMP) {
        nuke_world(sock->world);
        sock->world = NULL;
    }
    if (sock == hsock) hsock = sock->next;
    else sock->prev->next = sock->next;
    if (sock == tsock) tsock = sock->prev;
    else sock->next->prev = sock->prev;
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
    Stringfree(sock->prompt);
    free_queue(sock->queue);
    FREE(sock->queue);
    FREE(sock);
}

/* delete all dead sockets */
static void nuke_dead_socks()
{
    Sock *sock, *next;

    while (fsock && (fsock->flags & SOCKDEAD)) {
        /* bg the dead fg sock and find another sock to put in fg */
        bg_sock();
        if ((sock = find_sock(NULL))) {
            fg_sock(sock);
        } else {
            announce_world(NULL);
            update_prompt(NULL);
        }
    }
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

/* close all sockets */
void disconnect_all()
{
    Sock *sock, *next;

    bg_sock();
    for (sock = hsock; sock; sock = next) {
        next = sock->next;
        nukesock(sock);
    }
    hsock = tsock = NULL;
    if (quitdone) quit_flag = 1;
}

/* disconnect from a world */
int handle_dc_command(args)
    char *args;
{
    Sock *s;

    if (!*args) {
        if (!fsock) return 0;
        killsock(fsock);
    } else if (cstrcmp(args, "-all") == 0) {
        if (!hsock) return 0;
        disconnect_all();
        announce_world(NULL);
    } else {
        for (s = hsock; s; s = s->next) {
            if (cstrcmp(s->world->name, args) == 0 && !(s->flags & SOCKDEAD))
                break;
        }
        if (s) {
            killsock(s);
            oprintf ("%% Connection to %s closed.", s->world->name);
        } else {
            tfprintf(tferr, "%% Not connected to %s", args);
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
    char buffer[81];

    if (hsock == NULL) {
        oputs("% Not connected to any sockets.");
        return 0;
    }

    for (sock = hsock; sock != NULL; sock = sock->next) {
        sprintf(buffer, "%c%c",
            (sock == xsock) ? '*' : ' ',
            (sock->flags & SOCKPENDING) ? '?' :
                ((sock->flags & SOCKDEAD) ? '!' : ' '));
        if (sock == fsock)  strcpy(buffer+2, "[foregnd]");
        else if (!sock->activity)  strcpy(buffer+2, "[   idle]");
        else sprintf(buffer+2, "[%7d]", sock->activity);
        sprintf(buffer+11, " %15s %30s %s",
            sock->world->name, sock->world->address, sock->world->port);
        oputs(buffer);
    }
    return 1;
}

int handle_send_command(args)
    char *args;
{
    Sock *save = xsock, *sock = xsock;
    unsigned int len;
    int opt, Wflag = FALSE, nflag = 0;

    if (!hsock) {
        tfputs("% Not connected to any sockets.", tferr);
        return 0;
    }
    startopt(args, "w:Wn");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
        case 'w':
            if (*args) {
                if (!(sock = find_sock(args))) {
                    tfprintf(tferr, "%% Not connected to %s", args);
                    return 0;
                }
            } else sock = xsock;
            break;
        case 'W':
            Wflag = TRUE;
            break;
        case 'n':
            nflag = 1;
            break;
        default:
            return 0;
        }
    }
    
    args[len = strlen(args)] = '\n';            /* be careful */
    if (Wflag) {
        for (xsock = hsock; xsock; xsock = xsock->next)
            send_line(args, len + 1 - nflag);
    } else {
        xsock = sock;
        send_line(args, len + 1 - nflag);
    }
    args[len] = '\0';                           /* restore end of string */
    xsock = save;
    return 1;
}

/* tramsmit text to current socket */
static int transmit(str, numtowrite)
    char *str;
    unsigned int numtowrite;
{
    int numwritten;

    if (!xsock || xsock->flags & (SOCKDEAD | SOCKPENDING)) return 0;
    while (numtowrite) {
        numwritten = send(xsock->fd, str, numtowrite, 0);
        if (numwritten < 0) {
#ifdef EWOULDBLOCK
            if (errno == EWOULDBLOCK || errno == EAGAIN) numwritten = 0;
#else
            if (errno == EAGAIN) numwritten = 0;
#endif
            else {
                killsock(xsock);
                do_hook(H_DISCONNECT,
                    "%% Connection to %s closed: %s", "%s %s",
                    xsock->world->name, STRERROR(errno));
                return 0;
            }
        }
        numtowrite -= numwritten;
        str += numwritten;
        if (numtowrite) sleep(1);
    }
    return 1;
}

/* send_line
 * Send a line to the server on the current socket.  If there is a prompt
 * associated with the current socket, clear it.
 */
int send_line(str, numtowrite)
    char *str;
    unsigned int numtowrite;
{
    if (xsock && xsock->prompt->len) {
        Stringterm(xsock->prompt, 0);
        if (xsock == fsock) update_prompt(xsock->prompt);
    }
    return transmit(str, numtowrite);
}

/* call login hook if appropriate */
static void login_hook(sock)
    Sock *sock;
{
    World *w;

    if (login && sock->flags & SOCKLOGIN) {
        w = (*sock->world->character) ? sock->world : get_default_world();
        if (w && *w->character)
            do_hook(H_LOGIN, NULL, "%s %s %s", sock->world->name,
                w->character, w->pass);
    }
}

static void handle_socket_line()
{
    xsock->flags |= SOCKPROMPT;
    (incoming_text = new_aline(xsock->buffer->s, 0))->links = 1;
    Stringterm(xsock->buffer, 0);

    if (borg || hilite || gag)
        if (find_and_run_matches(incoming_text->str, 0, incoming_text))
            if (xsock != fsock)
                do_hook(H_BACKGROUND, "%% Trigger in world %s", "%s %S",
                    xsock->world->name, incoming_text);

    if (keep_quiet(incoming_text->str))    incoming_text->attrs |= F_GAG;
    if (is_suppressed(incoming_text->str)) incoming_text->attrs |= F_GAG;
    if (handle_portal(incoming_text->str)) incoming_text->attrs |= F_GAG;

    world_output(xsock, incoming_text);
    free_aline(incoming_text);
    incoming_text = NULL;
}

/* log, record, and display aline as if it came from sock */
void world_output(sock, aline)
    Sock *sock;
    Aline *aline;
{
    aline->links++;
    record_hist(sock->world->history, aline);
    if (!(gag && (aline->attrs & F_GAG))) {
        if (sock == fsock) {
            globalout(aline);
        } else {
            if (bg_output) {
                aline->links++;
                enqueue(sock->queue, aline);
            }
            if (!sock->activity) {
                ++active_count;
                status_bar(STAT_ACTIVE);
                do_hook(H_ACTIVITY, "%% Activity in world %s", "%s",
                    sock->world->name);
            }
            sock->activity++;
        }
    }
    free_aline(aline);
}

/* get the prompt for the fg sock */
String *fgprompt()
{
    return (fsock) ? fsock->prompt : NULL;
}

void tog_lp()
{
    if (!fsock) return;
    if (lpflag) {
        if (fsock->buffer->len) {
            SStringcat(fsock->prompt, fsock->buffer);
            Stringterm(fsock->buffer, 0);
            set_refresh_pending(REF_PHYSICAL);
        }
    } else {
        if (fsock->prompt->len && !(fsock->flags & SOCKPROMPT)) {
            SStringcpy(fsock->buffer, fsock->prompt);
            Stringterm(fsock->prompt, 0);
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
    char *str;
    int confirmed;
{
    if (lpquote) runall(time(NULL));
    if (xsock->flags & SOCKPROMPT) Stringterm(xsock->prompt, 0);
    Stringcat(xsock->prompt, str);
    Stringterm(xsock->buffer, 0);
    /* Old versions did trigger checking here.  Removing it breaks
     * compatibility, but I doubt many users will care.  Leaving
     * it in would not be right for /prompt.
     */
    if (xsock == fsock) update_prompt(xsock->prompt);
    if (confirmed) xsock->flags |= SOCKPROMPT;
    else xsock->flags &= ~SOCKPROMPT;
}

static void telnet_send(cmd, opt)
    int cmd, opt;
{
    char buf[4];
    sprintf(buf, "%c%c%c", (char)TN_IAC, (char)cmd, (char)opt);
    transmit(buf, 3);
    if (telopt) {
        if (telnet_label[opt])
            oprintf("sent IAC %s %s", telnet_label[cmd], telnet_label[opt]);
        else oprintf("sent IAC %s %d", telnet_label[cmd], opt);
    }
}

static void telnet_recv(cmd, opt)
    int cmd, opt;
{
    if (telopt) {
        if (telnet_label[opt])
            oprintf("rcvd IAC %s %s", telnet_label[cmd], telnet_label[opt]);
        else oprintf("rcvd IAC %s %d", telnet_label[cmd], opt);
    }
}

/* handle input from current socket */
static void handle_socket_input()
{
    unsigned char *place, buffer[1024];
    fd_set readfds;
    int count, total = 0;
    struct timeval tv;

#define SPAM 10240       /* break loop if this many chars are received */

    if (xsock->prompt->len && !(xsock->flags & SOCKPROMPT)) {
        /* We assumed last text was a prompt, but now we have more text.
         * We must now assume that the previous unterminated text was
         * really the beginning of a longer line.  (If we're wrong, the
         * previous prompt appears as output.  But if we did the opposite,
         * a real beginning of a line would never appear in the output
         * window; that would be a worse mistake.)
         */
        SStringcpy(xsock->buffer, xsock->prompt);
        Stringterm(xsock->prompt, 0);
        if (xsock == fsock) update_prompt(xsock->prompt);
    }

    do {  /* while (count > 0 && !interrupted()) */
        do count = recv(xsock->fd, buffer, sizeof(buffer), 0);
            while (count < 0 && errno == EINTR);
        if (count <= 0) {
            if (xsock->buffer->len) handle_socket_line();
            killsock(xsock);
            do_hook(H_DISCONNECT, (count < 0) ?
                "%% Connection to %s closed: %s: %s" :
                "%% Connection to %s closed by foreign host.",
                (count < 0) ? "%s %s %s" : "%s",
                xsock->world->name, "recv", STRERROR(errno));
            return;
        }

        place = buffer;
        while (place - buffer < count) {
            if (xsock->state == TN_IAC) {
                switch (*place) {
                case TN_IAC:
                    /* Literal IAC.  Ignore it. */
                    xsock->state = '\0';
                    break;
                case TN_GA: case TN_EOR:
                    /* This is definitely a prompt. */
                    if (telopt)
                        oprintf("rcvd IAC %s", telnet_label[xsock->state]);
                    if (do_hook(H_PROMPT, NULL, "%S", xsock->buffer)) {
                        Stringterm(xsock->buffer, 0);
                    } else {
                        handle_prompt(xsock->buffer->s, TRUE);
                    }
                    break;
                case TN_SB:   /* currently impossible */
                case TN_WILL: case TN_WONT:
                case TN_DO:   case TN_DONT:
                    break;
                default:
                    /* shouldn't happen; ignore it. */
                    if (telopt) {
                        if (telnet_label[xsock->state])
                            oprintf("rcvd IAC %s",
                                telnet_label[xsock->state]);
                        else oprintf("rcvd IAC %d", xsock->state);
                    }
                    break;
                }
                xsock->state = *place++;

            } else if (xsock->state == TN_WILL) {
                telnet_recv(TN_WILL, *place);
                if (*place == TN_ECHO) {
                    if (xsock->flags & SOCKECHO) {
                        /* stop local echo, and acknowledge */
                        echoflag = FALSE;
                        xsock->flags &= ~SOCKECHO;
                        telnet_send(TN_DO, TN_ECHO);
                    } else {
                        /* we already said DO ECHO, so ignore WILL ECHO */
                    }
                } else if (*place == TN_EOR_OPT) {
                    if (!(xsock->flags & SOCKEOR)) {
                        xsock->flags |= SOCKEOR;
                        telnet_send(TN_DO, TN_EOR_OPT);
                    } else {
                        /* we already said DO EOR_OPT, so ignore WILL EOR_OPT */
                    }
                } else {
                    /* don't accept other WILL offers */
                    telnet_send(TN_DONT, *place);
                }
                xsock->state = '\0';
                place++;
            } else if (xsock->state == TN_WONT) {
                telnet_recv(TN_WONT, *place);
                if (*place == TN_ECHO) {
                    if (xsock->flags & SOCKECHO) {
                        /* we're already echoing, so ignore WONT ECHO */
                    } else {
                        /* resume local echo, and acknowledge */
                        echoflag = TRUE;
                        xsock->flags |= SOCKECHO;
                        telnet_send(TN_DONT, TN_ECHO);
                    }
                } else if (*place == TN_EOR_OPT) {
                    if (!(xsock->flags & SOCKEOR)) {
                        /* we're in DONT EOR_OPT state, ignore WONT EOR_OPT */
                    } else {
                        /* acknowledge */
                        xsock->flags &= ~SOCKEOR;
                        telnet_send(TN_DONT, TN_EOR);
                    }
                } else {
                    /* we're already in the WONT state, so ignore WONT */
                }
                xsock->state = '\0';
                place++;
            } else if (xsock->state == TN_DO) {
                telnet_recv(TN_DO, *place);
                {
                    /* refuse all DO requests */
                    telnet_send(TN_WONT, *place);
                }
                xsock->state = '\0';
                place++;
            } else if (xsock->state == TN_DONT) {
                /* ignore all DONT requests (we're already in the DONT state) */
                telnet_recv(TN_DONT, *place);
                xsock->state = '\0';
                place++;
#if 0
            } else if (xsock->state == TN_SB) {
                telnet_recv(TN_SB, *place);
                xsock->state = *place++;
                /* now in FOOBAR-option state */
            } else if (xsock->state == TN_FOOBAR) {
                /* start subnegotiation for FOOBAR option */
                xsock->state = *place++;
#endif
            } else if (*place == TN_IAC) {
                xsock->state = *place++;
                if (!(xsock->flags & SOCKTELNET)) {
                    xsock->flags |= SOCKTELNET;
                    /* telnet_send(TN_WILL, TN_LINEMODE); */
                }
            } else if (*place == '\n') {
                /* Complete line received.  Process it. */
                handle_socket_line();
                xsock->state = *place++;
            } else if (*place == '\r' || *place == '\0') {
                /* Ignore CR and NUL. */
                xsock->state = *place++;
            } else if (*place == '\b' && xsock->state == '*') {
                /* "*\b" is an LP editor prompt. */
                if (do_hook(H_PROMPT, NULL, "%S", xsock->buffer)) {
                    Stringterm(xsock->buffer, 0);
                } else {
                    handle_prompt(xsock->buffer->s, TRUE);
                }
                xsock->state = *place++;
            } else if (*place == '\b' && catch_ctrls > 0) {
                if (xsock->buffer->len && catch_ctrls > 1)
                    Stringterm(xsock->buffer, xsock->buffer->len - 1);
                xsock->state = *place++;
            } else if (*place == '\t') {
                Stringnadd(xsock->buffer, ' ', 8 - xsock->buffer->len % 8);
                xsock->state = *place++;
            } else if (catch_ctrls > 1 &&
              ((xsock->state=='\033' && *place=='[') || *place==ANSI_CSI))  {
                /* CSI is either a single character, or "ESC [". */
                xsock->state = ANSI_CSI;
                place++;
            } else if (catch_ctrls > 1 && xsock->state == ANSI_CSI &&
              (*place == '?' || *place == ';' || isalnum(*place))) {
                /* ANSI terminal sequences contain: CSI, an optional '?',
                 * any number of digits and ';'s, and a letter.
                 */
                if (isalpha(*place)) xsock->state = *place;
                place++;
            } else if (catch_ctrls > 0 && !isprint(*place)) {
                /* not printable */
                xsock->state = *place++;
            } else {
                /* normal character */
                Stringadd(xsock->buffer, (char)*place);
                xsock->state = *place++;
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

        if ((count = select(xsock->fd + 1, &readfds, NULL, NULL, &tv)) < 0) {
            if (errno != EINTR) {
                operror("select");
                die("% Failed select in handle_socket_input");
            }
        }

    } while (count > 0 && !interrupted() && (total += count) < SPAM);

    /* If lpflag is on and we got a partial line from the fg world,
     * assume the line is a prompt.
     */
    if (lpflag && xsock == fsock && xsock->buffer->len) {
        handle_prompt(xsock->buffer->s, FALSE);
    }
}


static int keep_quiet(what)
    char *what;
{
    if (!xsock->numquiet) return FALSE;
    if (!cstrncmp(what, "Use the WHO command", 19) ||
      !cstrncmp(what, "### end of messages ###", 23)) {
        xsock->numquiet = 0;
    } else (xsock->numquiet)--;
    return TRUE;
}

static int handle_portal(what)
    char *what;
{
    smallstr name, address, port;
    STATIC_BUFFER(buffer);
    World *world;

    if (!bamf) return(0);
    if (sscanf(what,
        "#### Please reconnect to %64[^ @]@%64s (%*64[^ )]) port %64s ####",
        name, address, port) != 3)
            return 0;
    if (restrict >= RESTRICT_WORLD) {
        tfputs("% bamfing is restricted.", tferr);
        return 0;
    }

    if (bamf == 1) {
        Sprintf(buffer, 0, "@%s", name);
        world = fworld();
        world = new_world(buffer->s, world->character, world->pass,
            address, port, world->mfile, "");
        world->flags |= WORLD_TEMP;
    } else if (!(world = find_world(name))) {
        world = new_world(name, "", "", address, port, "", "");
        world->flags |= WORLD_TEMP;
    }

    do_hook(H_BAMF, "%% Bamfing to %s", "%s", name);
    if (bamf != 2) handle_dc_command("");
    if (!opensock(world, TRUE, FALSE))
        tfputs("% Connection through portal failed.", tferr);
    return 1;
}
