/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: process.h,v 35004.8 1999/01/31 00:27:51 hawkeye Exp $ */

#ifndef PROCESS_H
#define PROCESS_H

# ifndef NO_PROCESS

extern void FDECL(kill_procs_by_world,(struct World *world));
extern void NDECL(kill_procs);
extern void NDECL(nuke_dead_procs);
extern int  NDECL(runall);

extern TIME_T proctime;         /* when next process should run */

# else

#define kill_procs_by_world(world)     /* do nothing */
#define kill_procs()                   /* do nothing */
#define nuke_dead_procs()              /* do nothing */
#define runall(now)                    /* do nothing */
#define proctime 0

# endif /* NO_PROCESS */

#endif /* PROCESS_H */
