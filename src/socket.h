/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: socket.h,v 35004.22 1999/01/31 00:27:54 hawkeye Exp $ */

#ifndef SOCKET_H
#define SOCKET_H

#define SOCK_RECV	0
#define SOCK_SEND	1

struct World   *world_decl;	/* declares struct World */

extern Aline *incoming_text;
extern int quit_flag;
extern struct Sock *xsock;

extern void    NDECL(main_loop);
extern void    NDECL(init_sock);
extern int     FDECL(is_active,(int fd));
extern void    FDECL(readers_clear,(int fd));
extern void    FDECL(readers_set,(int fd));
extern TIME_T  FDECL(sockidle,(CONST char *name, int dir));
extern int     NDECL(tog_bg);
extern int     FDECL(openworld,(CONST char *name, CONST char *port,
                     int autologin, int quietlogin));
extern int     FDECL(opensock,(struct World *w, int autologin, int quietlogin));
extern void    FDECL(world_output,(struct World *world, Aline *aline));
extern int     FDECL(send_line,(CONST char *s, unsigned int len, int eol_flag));
extern Aline  *NDECL(fgprompt);
extern int     NDECL(tog_lp);
extern void    NDECL(transmit_window_size);
extern int     FDECL(local_echo,(int flag));
extern int     FDECL(handle_send_function,(CONST char *text, CONST char *world,
                     int eol_flag));
extern int     FDECL(nactive,(CONST char *worldname));

extern struct World *NDECL(xworld);
extern CONST char   *NDECL(fgname);
extern CONST char   *FDECL(world_info,(CONST char *worldname, CONST char *fieldname));

#endif /* SOCKET_H */
