/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tfselect.h,v 35000.3 1997/03/27 01:04:50 hawkeye Exp $ */

#ifndef TFSELECT_H
#define TFSELECT_H


#ifndef FD_ZERO                     /* For BSD 4.2 systems. */

# ifdef OPEN_MAX
#  define FD_SETSIZE OPEN_MAX
# else
#  define FD_SETSIZE 1024
# endif

VEC_TYPEDEF(fd_vector, FD_SETSIZE); /* in case fd_set is already typedef'd */
# undef fd_set                      /* in case fd_set is already #define'd */
# define fd_set fd_vector

# define FD_SET(n, p)    VEC_SET(n, p)
# define FD_CLR(n, p)    VEC_CLR(n, p)
# define FD_ISSET(n, p)  VEC_ISSET(n, p)
# define FD_ZERO(p)      VEC_ZERO(p)
extern int select();

#endif /* ndef FD_ZERO */


#ifdef SOCKS
# define connect Rconnect
# ifdef SOCKS_NONBLOCK
#  define select Rselect
# else
#  undef TF_NONBLOCK
# endif
#endif


extern int FDECL(tfselect,(int nfds, fd_set *readers, fd_set *writers,
    fd_set *excepts, struct timeval *timeout));


#endif /* TFSELECT_H */
