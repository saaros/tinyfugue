/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: process.h,v 33000.0 1994/03/05 09:34:14 hawkeye Exp $ */

#ifndef PROCESS_H
#define PROCESS_H

#include "world.h"

# ifndef NO_PROCESS

extern void FDECL(kill_procs_by_world,(struct World *world));
extern void NDECL(kill_procs);
extern void FDECL(runall,(TIME_T now));

# else

#define kill_procs_by_world(world)     /* do nothing */
#define kill_procs()                   /* do nothing */
#define runall(now)                    /* do nothing */

# endif /* NO_PROCESS */

#endif /* PROCESS_H */
