/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: socket.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


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
#include <sys/stat.h>
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
#include "fd_set.h"
#include "dstring.h"
#include "tf.h"
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
#include "special.h"
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
static void  NDECL(any_sock);
static int   FDECL(get_host_address,(char *name, struct in_addr *addr));
static int   FDECL(establish,(Sock *new));
static void  NDECL(nuke_dead_socks);
static void  FDECL(nukesock,(Sock *sock));
static void  FDECL(login_hook,(Sock *sock));
static void  FDECL(handle_socket_prompt,(int confirmed));
static void  NDECL(handle_socket_line);
static void  NDECL(handle_socket_input);
static int   FDECL(transmit,(char *s, unsigned int len));
static void  NDECL(flush_output_queue);

#define killsock(s)  do { (s)->flags |= SOCKDEAD; dead_socks++; } while (0)

#ifndef CONN_WAIT
#define CONN_WAIT 500000
#endif

#ifndef PROC_WAIT
#define PROC_WAIT 100000
#endif

extern int restrict;
extern int echoflag;                    /* echo input? */
extern int need_refresh;                /* Does input need refresh? */
#ifndef NO_PROCESS
extern TIME_T proctime;                 /* when next process should run */
#else
# define proctime 0
#endif

static fd_set readers;		/* input file descriptors */
static fd_set active;		/* active file descriptors */
static fd_set writers;			/* pending connections */
static fd_set connected;	/* completed connections */
static int nfds;		/* max # of readers/writers */
static Sock *hsock = NULL;	/* head of socket list */
static Sock *tsock = NULL;	/* tail of socket list */
static int dead_socks = 0;	/* Number of unnuked dead sockets */

#define TELNET_ECHO	'\001'	/* echo option */
#define TELNET_SGA	'\003'	/* suppress GOAHEAD option */
#define TELNET_EOR_OPT	'\031'	/* EOR option */

#define TELNET_EOR	'\357'	/* End-Of-Record */
#define TELNET_GA	'\371'	/* Go Ahead */
#define TELNET_WILL	'\373'	/* I offer to ~, or ack for DO */
#define TELNET_WONT	'\374'	/* I will stop ~ing, or nack for DO */
#define TELNET_DO	'\375'	/* Please do ~?, or ack for WILL */
#define TELNET_DONT	'\376'	/* Stop ~ing!, or nack for WILL */
#define TELNET_IAC	'\377'	/* telnet Is A Command character */

#define ANSI_CSI	'\233'	/* ANSI terminal Command Sequence Intro */

int quit_flag = FALSE;          /* Are we all done? */
Sock *fsock = NULL;		/* foreground socket */
Sock *xsock = NULL;		/* current (transmission) socket */
int active_count = 0;		/* # of (non-current) active sockets */
TIME_T mail_update = 0;		/* next mail check (0==immediately) */
TIME_T clock_update = 0;	/* next clock update (0==immediately) */

#define CONFAIL(where, what, why) \
        do_hook(H_CONFAIL, "%% Connection to %s failed: %s: %s", "%s %s: %s", \
        (where), (what), (why))

/* initialize socket.c data */
void init_sock()
{
    FD_ZERO(&readers);
    FD_ZERO(&active);
    FD_ZERO(&writers);
    FD_ZERO(&connected);
    FD_SET(0, &readers);
    nfds = 1;
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
         * versions of select() count it once, but some can count it multiple
         * times.  We don't do that, so it's not a problem.
         */
        active = readers;
        connected = writers;
        count = select(nfds, &active, &connected, NULL, tvp);

        if (count < 0) {
            /* select() must have exited due to error or interrupt. */
            if (errno != EINTR) {
                perror("select");
                die("% Failed select in main_loop");
            }
        } else if (count == 0) {
            /* select() must have exited due to timeout. */
            if (need_refresh == REF_LOGICAL) logical_refresh();
            else if (need_refresh==REF_PHYSICAL || visual) physical_refresh();
        } else {
            /* check for user input */
            if (FD_ISSET(0, &active)) {
                count--;
                if (need_refresh == REF_LOGICAL)
                    logical_refresh();
                else if (need_refresh==REF_PHYSICAL || visual)
                    physical_refresh();
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

        /* deal with signals caught during last loop */
        process_signals();

        /* garbage collection */
        if (dead_socks) nuke_dead_socks();
        nuke_dead_macros();
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
    else if ((d = get_default_world()) != NULL && *d->mfile)
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

/* display any unseen lines in the output queue for xsock */
static void flush_output_queue()
{
    extern TFILE *tfscreen;
    ListEntry *node;

    for (node = xsock->queue->head; node; node = node->next)
        record_global((Aline *)node->data);
    queuequeue(xsock->queue, tfscreen->u.queue);
    oflush();
}


/* bring a socket into the foreground */
static void fg_sock(sock)
    Sock *sock;
{
    if (sock) {
        xsock = fsock = sock;
        FD_SET(sock->fd, &readers);
        if (sock->flags & SOCKACTIVE) {
            --active_count;
            status_bar(STAT_ACTIVE);
        }
        sock->flags &= ~SOCKACTIVE;
        announce_world(sock);
        flush_output_queue();
        echoflag = (sock->flags & SOCKECHO);
        tog_lp();
        refresh_prompt(sock->prompt);
        if (sock->flags & SOCKDEAD) {
            nukesock(sock);
            fsock = xsock = NULL;
            any_sock();
        }
    } else {
        announce_world(NULL);
        refresh_prompt(NULL);
    }
}

/* put all sockets in the background */
static void bg_sock()
{
    echoflag = TRUE;
    fsock = xsock = NULL;
}

/* put fg socket in the background */
void no_sock()
{
    if (fsock) {
        bg_sock();
        announce_world(NULL);
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
        memcpy(addr, host->h_addr, sizeof(struct in_addr));
        return 1;
    }
#endif
    return 0;
}

/* try to open a new connection */
int opensock(w, autologin, quietlogin)
    World *w;
    int autologin, quietlogin;
{
    int flags;
    Sock *sock;
    struct timeval tv;
    fd_set pending;
    struct sockaddr_in addr;
    struct servent *service;
    int size = sizeof(struct sockaddr_in);

    /* Does a socket to this world already exist? */
    for (sock = hsock; sock != NULL; sock = sock->next) {
        if (sock->world == w &&
          (!(sock->flags & SOCKDEAD) || sock->flags & SOCKACTIVE)) {
            if (sock == fsock) return 1;
            if (sock->flags & SOCKPENDING) {
                oputs("% Connection already in progress.");
                return 0;  /* ??? */
            }
            bg_sock();
            fg_sock(sock);
            if (sock == fsock && sockmload) wload(sock->world);
            return 1;
        }
    }

    /* create and initialize new Sock */
    sock = (Sock *) MALLOC(sizeof(struct Sock));
    sock->prev = tsock;
    if (tsock == NULL) {
        tsock = hsock = sock;
    } else {
        tsock = tsock->next = sock;
    }
    sock->fd = -1;
    sock->state = '\0';
    sock->flags = SOCKECHO | SOCKEDIT | SOCKTRAP | (autologin ? SOCKLOGIN : 0);
    sock->world = w;
    sock->world->socket = sock;
    if (quietlogin && autologin && *sock->world->character)
        sock->numquiet = MAXQUIET;
    else
        sock->numquiet = 0;
    Stringinit(sock->buffer);
    Stringinit(sock->prompt);
    sock->queue = (Queue *)MALLOC(sizeof(Queue));
    init_queue(sock->queue);
    sock->next = NULL;

    addr.sin_family = AF_INET;

    if (isdigit(*w->port)) {
        addr.sin_port = htons(atoi(w->port));
#ifndef NO_NETDB
    } else if ((service = getservbyname(w->port, "tcp"))) {
        addr.sin_port = service->s_port;
#endif
    } else {
        CONFAIL(w->name, w->port, "no such service");
        nukesock(sock);
        return 0;
    }

    if (!get_host_address(w->address, &addr.sin_addr)) {
        CONFAIL(w->name, w->address, "can't find host");
        nukesock(sock);
        return 0;
    }

    if ((sock->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        CONFAIL(w->name, "socket", STRERROR(errno));
        nukesock(sock);
        return 0;
    }
    if (sock->fd >= nfds) nfds = sock->fd + 1;

#ifdef TF_NONBLOCK
    if ((flags = fcntl(sock->fd, F_GETFL)) < 0)
        operror("Can't make socket nonblocking: F_GETFL fcntl");
    else if ((fcntl(sock->fd, F_SETFL, flags | TF_NONBLOCK)) < 0)
        operror("Can't make socket nonblocking: F_SETFL fcntl");
#endif

    if (connect(sock->fd, (struct sockaddr*)&addr, size) == 0) {
        /* The connection completed successfully. */
        return establish(sock);

#ifdef EINPROGRESS
    } else if (errno == EINPROGRESS) {
        /* The connection needs more time.  It will select() as writable when
         * it has completed.  We select on it for a fraction of a second here
         * so "immediate" and "relatively fast" look the same to the user.
         */
        sock->flags |= SOCKPENDING;
        FD_ZERO(&pending);
        FD_SET(sock->fd, &pending);
        tv.tv_sec = 0;
        tv.tv_usec = CONN_WAIT;
        if (select(sock->fd + 1, NULL, &pending, NULL, &tv) > 0) {
            /* The connection completed successfully. */
            return establish(sock);
        } else {
            /* select() must have returned 0, or -1 and errno==EINTR.  In
             * either case, the connection still needs more time.  So we add
             * the fd to the set being watched by the select() in main_loop(),
             * and don't waste any more time waiting here.
             */
            FD_SET(sock->fd, &writers);
            do_hook(H_PENDING, "%% Connection to %s in progress.", "%s",
                sock->world->name);
            return 1;  /* Maybe this should be something else. */
        }
#endif

    } else if (errno == EAGAIN) {
        /* A bug in SVR4.2 causes nonblocking connect() to (sometimes?)
         * incorrectly fail with EAGAIN.  The only thing we can do about
         * it is to try a blocking connect().
         */
        close (sock->fd);
        if ((sock->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            CONFAIL(w->name, "socket", STRERROR(errno));
            nukesock(sock);
            return 0;
        }
        if (connect(sock->fd, (struct sockaddr*)&addr, size) < 0) {
            CONFAIL(w->name, "connect", STRERROR(errno));
            nukesock(sock);
            return 0;
        }
        return establish(sock);

    } else {
        /* The connection failed.  Give up. */
        CONFAIL(w->name, "connect", STRERROR(errno));
        nukesock(sock);
        return 0;
    }
}

/* Establish a sock for which connect() has completed. */
static int establish(sock)
    Sock *sock;
{
    int was_pending = FALSE;
    int err = 0;
    struct sockaddr addr;
    int len;

#ifdef EINPROGRESS
    if (sock->flags & SOCKPENDING) {
        /* If socket isn't connected, getpeername() will fail with ENOTCONN
         * (we don't care about the addr, just the errno).  If that happens,
         * we use getsockopt() to find out why the connect() failed.
         */
        len = sizeof(addr);
        if (getpeername(sock->fd, &addr, &len) < 0) {
            char *errmsg;
            len = sizeof(err);
            if (errno != ENOTCONN) {
                errmsg = "getpeername";
                err = errno;
#ifdef SO_ERROR
            /* SO_ERROR isn't defined on hpux?  Screw 'em. */
            } else if (getsockopt(sock->fd,SOL_SOCKET,SO_ERROR,&err,&len) < 0) {
                errmsg = "getsockopt";
                err = errno;
#endif
            } else errmsg = "connect";
            killsock(sock);
            CONFAIL(sock->world->name, errmsg, STRERROR(err));
            return 0;
        }

        /* connect() worked.  Clear the pending stuff, and get on with it. */
        sock->flags &= ~SOCKPENDING;
        FD_CLR(sock->fd, &writers);
        was_pending = TRUE;
    }
#endif /* EINPROGRESS */

#ifndef NO_HISTORY
    /* skip any old undisplayed lines */
    sock->world->history->index = sock->world->history->pos;
#endif

    xsock = sock;
    wload(sock->world);
    if (was_pending) {
        FD_SET(sock->fd, &readers);
        do_hook(H_CONNECT, "%% Connection to %s established.", "%s",
            sock->world->name);
    } else {
        bg_sock();
        fg_sock(sock);
        do_hook(H_CONNECT, NULL, "%s", sock->world->name);
    }
    login_hook(sock);
    xsock = fsock;
    return 1;
}

/* Bring next or previous socket in list into foreground */
int movesock(dir)
    int dir;
{
    Sock *sock, *stop;

    reset_outcount();  /* ??? */
    if (!hsock) return 0;
    stop = sock = (fsock ? fsock : hsock);
    do {
        if (dir > 0) sock = sock && sock->next ? sock->next : hsock;
        else sock = sock && sock->prev ? sock->prev : tsock;
    } while ((sock->flags & SOCKPENDING) && sock != stop);
    if (sock != fsock) {
        if (fsock && (sock->flags & SOCKECHO) != (fsock->flags & SOCKECHO))
            need_refresh = REF_LOGICAL;
        bg_sock();
        fg_sock(sock);
        if (sockmload) wload(fsock->world);
        return 1;
    }
    return 0;
}

/* nukesock
 * Remove socket from list and free memory.  Should only be called on a
 * Sock which is known to have no references other than the socket list.
 */
static void nukesock(sock)
    Sock *sock;
{
    sock->world->socket = NULL;
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
        if (sock->flags & SOCKACTIVE) {
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
    int reconnect = FALSE;

    if (fsock && (fsock->flags & SOCKDEAD)) {
        bg_sock();
        reconnect = TRUE;
    }
    for (sock = hsock; sock; sock = next) {
        next = sock->next;
        if (sock->flags & SOCKDEAD) {
            if (sock->flags & SOCKACTIVE) FD_CLR(sock->fd, &readers);
            else {
                nukesock(sock);
                dead_socks--;
            }
        }
    }
    if (quitdone && !hsock) quit_flag = 1;
    else if (reconnect) any_sock();
}

/* find any socket that can be brought into foreground, and do it */
static void any_sock()
{
    Sock *sock;

    if ((sock = find_sock(NULL))) {
        fg_sock(sock);
        if (sockmload && fsock) wload(fsock->world);
    } else {
        announce_world(NULL);
        refresh_prompt(NULL);
    }
}

/* close all sockets */
void disconnect_all()
{
    Sock *sock, *next;

    for (sock = hsock; sock; sock = next) {
        next = sock->next;
        nukesock(sock);
    }
    bg_sock();
    hsock = tsock = NULL;
    if (quitdone) quit_flag = 1;
}

/* disconnect from a world */
int handle_dc_command(args)
    char *args;
{
    Sock *s;

    if (!*args) {
        if (fsock) killsock(fsock);
        else return 0;
    } else if (cstrcmp(args, "-all") == 0) {
        if (hsock) {
            disconnect_all();
            announce_world(NULL);
        } else return 0;
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
    char *state, buffer[81];

    if (hsock == NULL) {
        oputs("% Not connected to any sockets.");
        return 0;
    }

    for (sock = hsock; sock != NULL; sock = sock->next) {
        if (sock == xsock) state = "current";
        else if (sock->flags & SOCKPENDING) state = "pending";
        else if (sock->flags & SOCKDEAD)    state = "dead";
        else if (sock->flags & SOCKACTIVE)  state = "active";
        else state = "idle";
        sprintf(buffer, "%% [%7s]  %15s %30s %s", state,
            sock->world->name, sock->world->address, sock->world->port);
        oputs(buffer);
    }
    return 1;
}

/* call the background hook */
void background_hook(line)
    char *line;
{
    if (xsock != fsock && background <= 1)
        do_hook(H_BACKGROUND, "%% Trigger in world %s", "%s %s",
            xsock->world->name, line);
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
                    "%% Connection to %s closed by foreign host: %s",
                    "%s %s", xsock->world->name, STRERROR(errno));
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
        if (xsock == fsock) refresh_prompt(xsock->prompt);
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
    world_output(xsock, special_hook(xsock->buffer->s));
    Stringterm(xsock->buffer, 0);
}

/* log, record, and display string as if it came from world w */
void world_output(sock, aline)
    Sock *sock;
    Aline *aline;
{
    aline->links++;
    aline->attrs |= F_NEWLINE;
    record_hist(sock->world->history, aline);
    if (!(gag && (aline->attrs & F_GAG))) {
        if (sock == fsock) {
            globalout(aline);
        } else {
            if (bg_output) {
                aline->links++;
                enqueue(sock->queue, aline);
            }
            if (!(sock->flags & SOCKACTIVE) && !(aline->attrs & F_NOHISTORY)) {
                sock->flags |= SOCKACTIVE;
                ++active_count;
                status_bar(STAT_ACTIVE);
                do_hook(H_ACTIVITY, "%% Activity in world %s", "%s",
                    sock->world->name);
            }
        }
    }
    free_aline(aline);
}

/* get the prompt for the fg sock */
String *fgprompt()
{
    return fsock ? fsock->prompt : NULL;
}

void tog_lp()
{
    if (!fsock) return;
    if (lpflag) {
        if (fsock->buffer->len) {
            SStringcat(fsock->prompt, fsock->buffer);
            Stringterm(fsock->buffer, 0);
            need_refresh = REF_PHYSICAL;
        }
    } else {
        if (fsock->prompt->len && !(fsock->flags & SOCKPROMPT)) {
            SStringcpy(fsock->buffer, fsock->prompt);
            Stringterm(fsock->prompt, 0);
            need_refresh = REF_PHYSICAL;
        }
    }
}

static void handle_socket_prompt(confirmed)
    int confirmed;
{
    if (lpquote) runall(time(NULL));
    /* Be careful with termination if you ever implement /prompt */
    if (xsock->flags & SOCKPROMPT) Stringterm(xsock->prompt, 0);
    SStringcat(xsock->prompt, xsock->buffer);
    Stringterm(xsock->buffer, 0);
    check_trigger(xsock->prompt->s, 0);
    if (xsock == fsock) refresh_prompt(xsock->prompt);

    if (confirmed) xsock->flags |= SOCKPROMPT;
    else xsock->flags &= ~SOCKPROMPT;
}

/* handle input from current socket */
static void handle_socket_input()
{
    char *place;
    fd_set readfds;
    int count = 1;
    char buffer[1024];
    struct timeval tv;
    char cmd[4];

    if (xsock->prompt->len && !(xsock->flags & SOCKPROMPT)) {
        /* We assumed last text was a prompt, but now we have more text.
         * We must now assume that the previous unterminated text was
         * really the beginning of a longer line.  (If we're wrong, the
         * previous prompt appears as output.  But if we made the
         * opposite assumption, a real beginning of a line would never
         * appear in the output window; that would be a worse mistake.)
         */
        SStringcpy(xsock->buffer, xsock->prompt);
        Stringterm(xsock->prompt, 0);
        if (xsock == fsock) refresh_prompt(xsock->prompt);
    }

    while (count > 0) {
        do count = recv(xsock->fd, buffer, sizeof(buffer), 0);
            while (count < 0 && errno == EINTR);
        if (count <= 0) {
            if (count < 0) operror("recv failed");
            if (xsock->buffer->len) handle_socket_line();
            killsock(xsock);
            do_hook(H_DISCONNECT, "%% Connection to %s closed.",
                "%s", xsock->world->name);
            return;
        }

        place = buffer;
        while (place - buffer < count) {
            if (xsock->state == TELNET_IAC) {
                switch (xsock->state = *place++) {
                case TELNET_IAC:
                    /* Literal IAC.  Ignore it. */
                    xsock->state = '\0';
                    break;
                case TELNET_GA: case TELNET_EOR:
                    /* This is definitely a prompt. */
                    handle_socket_prompt(TRUE);
                    break;
                case TELNET_DO: case TELNET_DONT:
                case TELNET_WILL: case TELNET_WONT:
                    break;
                default:
                    /* shouldn't happen; ignore it. */
                    break;
                }
            } else if (xsock->state == TELNET_WILL) {
                if (*place == TELNET_ECHO) {
                    if (xsock->flags & SOCKECHO) {
                        /* stop local echo, and acknowledge */
                        echoflag = FALSE;
                        xsock->flags &= ~SOCKECHO;
                        sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_DO, *place);
                        transmit(cmd, 3);
                    } else {
                        /* we already said DO ECHO, so ignore WILL ECHO */
                    }
                } else if (*place == TELNET_EOR_OPT) {
                    if (!(xsock->flags & SOCKEOR)) {
                        xsock->flags |= SOCKEOR;
                        sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_DO, *place);
                        transmit(cmd, 3);
                    } else {
                        /* we already said DO EOR_OPT, so ignore WILL EOR_OPT */
                    }
                } else {
                    /* don't accept other WILL offers */
                    sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_DONT, *place);
                    transmit(cmd, 3);
                }
                xsock->state = '\0';
                place++;
            } else if (xsock->state == TELNET_WONT) {
                if (*place == TELNET_ECHO) {
                    if (xsock->flags & SOCKECHO) {
                        /* we're already echoing, so ignore WONT ECHO */
                    } else {
                        /* resume local echo, and acknowledge */
                        echoflag = TRUE;
                        xsock->flags |= SOCKECHO;
                        sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_DONT, *place);
                        transmit(cmd, 3);
                    }
                } else if (*place == TELNET_EOR_OPT) {
                    if (!(xsock->flags & SOCKEOR)) {
                        /* we're in DONT EOR_OPT state, ignore WONT EOR_OPT */
                    } else {
                        /* acknowledge */
                        xsock->flags &= ~SOCKEOR;
                        sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_DONT, *place);
                        transmit(cmd, 3);
                    }
                } else {
                    /* we're already in the WONT state, so ignore WONT */
                }
                xsock->state = '\0';
                place++;
            } else if (xsock->state == TELNET_DO) {
                /* refuse all DO requests */
                sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_WONT, *place);
                transmit(cmd, 3);
                xsock->state = '\0';
                place++;
            } else if (xsock->state == TELNET_DONT) {
                /* ignore all DONT requests (we're already in the DONT state) */
                xsock->state = '\0';
                place++;
            } else if (*place == TELNET_IAC) {
                xsock->state = *place++;
                if (!(xsock->flags & SOCKTELNET)) {
                    xsock->flags |= SOCKTELNET;
                    /* sprintf(cmd, "%c%c%c", TELNET_IAC, TELNET_WILL,
                        TELNET_LINEMODE); */
                    /* transmit(cmd, 3); */
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
                handle_socket_prompt(TRUE);
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
                Stringadd(xsock->buffer, *place);
                xsock->state = *place++;
            }
        }

        FD_ZERO(&readfds);
        FD_SET(xsock->fd, &readfds);
        if (lpflag && xsock->buffer->len && xsock == fsock) {
            tv.tv_sec = prompt_sec;
            tv.tv_usec = prompt_usec;
        } else tv.tv_sec = tv.tv_usec = 0;

        /* See if anything arrived while we were parsing */
        while ((count = select(xsock->fd + 1, &readfds, NULL, NULL, &tv)) < 0) {
            if (errno != EINTR) {
                operror("TF/receive/select");
                die("% Failed select");
            }
        }

    }

    /* If lpflag is on and we got a partial line from the fg world,
     * assume the line is a prompt.
     */
    if (lpflag && xsock == fsock && xsock->buffer->len) {
        handle_socket_prompt(FALSE);
    }
}

