/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1998 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: signals.h,v 35004.6 1998/01/02 09:41:45 hawkeye Exp $ */

#ifndef SIGNALS_H
#define SIGNALS_H

extern void NDECL(init_signals);
extern void NDECL(process_signals);
extern int  FDECL(shell_status,(int result));
extern int  FDECL(shell,(CONST char *cmd));
extern int  NDECL(suspend);
extern int  NDECL(interrupted);
extern void FDECL(crash,(int internal, CONST char *fmt,
    CONST char *file, int line, long n));

#define core(fmt, file, line, n)	crash(TRUE, fmt, file, line, n)
#define error_exit(fmt, file, line, n)	crash(FALSE, fmt, file, line, n)

#endif /* SIGNALS_H */
