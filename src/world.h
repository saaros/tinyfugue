/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1998 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: world.h,v 35004.11 1998/01/02 09:41:51 hawkeye Exp $ */

#ifndef WORLD_H
#define WORLD_H

#define WORLD_TEMP     001
#define WORLD_NOPROXY  002

typedef struct World {         /* World structure */
    int flags;
    struct World *next;
    char *name;                /* name of world */
    char *character;           /* login name */
    char *pass;                /* password */
    char *host;                /* host name */
    char *port;                /* port number or service name */
    char *mfile;               /* macro file */
    char *type;                /* user-defined server type (tiny, lp...) */
    struct Sock *sock;         /* open socket, if any */
#ifndef NO_HISTORY
    struct History *history;   /* history and logging info */
#endif
} World;


extern World *FDECL(new_world,(CONST char *name, CONST char *character,
                    CONST char *pass, CONST char *host, CONST char *port,
                    CONST char *mfile, CONST char *type, int flags));
extern int    FDECL(nuke_world,(World *w));
extern World *NDECL(get_default_world);
extern World *FDECL(find_world,(CONST char *name));
extern void   FDECL(mapworld,(void FDECL((*func),(struct World *world))));

#ifdef DMALLOC
extern void   NDECL(free_worlds);
#endif

#endif /* WORLD_H */
