/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: socket.h,v 33000.1 1994/04/26 08:52:52 hawkeye Exp $ */

#ifndef SOCKET_H
#define SOCKET_H

#include "world.h"

#define SOCKDEAD     00001     /* connection dead */
#define SOCKPENDING  00002     /* connection not yet established */
#define SOCKLOGIN    00004     /* autologin requested by user */
#define SOCKEOR      00020     /* server will send EOR after prompts */
#define SOCKECHO     00040     /* do local keyboard echo */
#define SOCKEDIT     00100     /* do local editing (not used) */
#define SOCKTRAP     00200     /* do local signal trapping (not used) */
#define SOCKLINEMODE 00400     /* do telnet LINEMODE negotiation (not used) */
#define SOCKTELNET   01000     /* server supports telnet protocol (not used) */
#define SOCKPROMPT   02000     /* last prompt was definitely a prompt */

typedef struct Sock {          /* an open connection to a server */
    int fd;                    /* socket to server */
    short flags;
    short numquiet;            /* # of lines to gag after connecting */
    struct World *world;       /* world to which socket is connected */
    struct Sock *next, *prev;  /* next/prev sockets in linked list */
    Stringp prompt;            /* prompt from server */
    Stringp buffer;            /* buffer for incoming characters */
    struct Queue *queue;       /* buffer for undisplayed lines */
    int activity;              /* number of undisplayed lines */
    unsigned char state;       /* state of parser finite state automaton */
} Sock;


extern void NDECL(main_loop);
extern void NDECL(init_sock);
extern int  FDECL(is_active,(int fd));
extern void FDECL(readers_clear,(int fd));
extern void FDECL(readers_set,(int fd));
extern void NDECL(tog_bg);
extern void FDECL(mapsock,(void FDECL((*func),(struct World *world))));
extern struct World *NDECL(fworld);
extern struct World *NDECL(xworld);
extern int  FDECL(openworld,(char *name, char *port, int autologin, int quietlogin));
extern int  FDECL(opensock,(struct World *w, int autologin, int quietlogin));
extern void NDECL(disconnect_all);
extern void FDECL(world_output,(struct Sock *sock, Aline *aline));
extern int  FDECL(send_line,(char *s, unsigned int len));
extern String *NDECL(fgprompt);
extern void NDECL(tog_lp);

#endif /* SOCKET_H */
