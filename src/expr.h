/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expr.h,v 35004.14 2003/05/27 01:09:22 hawkeye Exp $ */

#ifndef EXPR_H
#define EXPR_H

extern int    expr(Program *prog);

#if USE_DMALLOC
extern void   free_expr(void);
#endif

#endif /* EXPR_H */
