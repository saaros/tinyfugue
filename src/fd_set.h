/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: fd_set.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef FD_SET_H
#define FD_SET_H

#ifndef FD_ZERO
/* For BSD 4.2 systems. */
/* Assuming ints are 32 bits, this allows 32 open sockets.  That's plenty. */
# define fd_set int
# define FD_SET(n, p) (*p |= (1<<(n)))
# define FD_CLR(n, p) (*p &= ~(1<<(n)))
# define FD_ISSET(n, p) (*p & (1<<(n)))
# define FD_ZERO(p) (*p = 0)
#endif

#ifdef MISSING_DECLS
extern int FDECL(select,(int, fd_set *, fd_set *, fd_set *, struct timeval *));
#else
extern int select();
#endif

#endif /* FD_SET_H */
