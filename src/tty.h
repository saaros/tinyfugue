/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tty.h,v 35004.3 1997/03/27 01:04:51 hawkeye Exp $ */

#ifndef TTY_H
#define TTY_H

extern void NDECL(init_tty);
extern void NDECL(cbreak_noecho_mode);
extern void NDECL(reset_tty);
extern int  NDECL(get_window_size);

#endif /* TTY_H */
