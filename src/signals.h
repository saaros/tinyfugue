/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: signals.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef SIGNALS_H
#define SIGNALS_H

extern void NDECL(init_signals);
extern void NDECL(process_signals);
extern int  FDECL(shell,(char *cmd));
extern int  NDECL(suspend);
extern int  NDECL(interrupted);
extern void FDECL(core,(CONST char *why));
extern void NDECL(tog_sigquit);

#endif /* SIGNALS_H */
