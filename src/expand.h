/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expand.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef EXPAND_H
#define EXPAND_H

#define SUB_NONE	0  /* no subs   \                        */
#define SUB_NEWLINE	1  /* %; subs    >- from command line    */
#define SUB_FULL	2  /* all subs  /                        */
#define SUB_MACRO	3  /* all subs    - from macro           */

extern int   FDECL(process_macro,(char *body, char *args, int subs));
extern int   FDECL(do_shift,(int count));

#endif /* EXPAND_H */
