/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1998 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expr.h,v 35004.4 1998/01/02 09:41:35 hawkeye Exp $ */

#ifndef EXPR_H
#define EXPR_H

extern void   FDECL(evalexpr,(Stringp dest, CONST char *args));
extern int    NDECL(expr);

#ifdef DMALLOC
extern void   NDECL(free_expr);
#endif

#endif /* EXPR_H */
