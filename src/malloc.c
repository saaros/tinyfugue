/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: malloc.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#include "config.h"
#include "port.h"
#include "signals.h"

#ifndef DMALLOC
GENERIC  *FDECL(dmalloc,(long unsigned size));
GENERIC  *FDECL(drealloc,(GENERIC *ptr, long unsigned size));

GENERIC *dmalloc(size)
    long unsigned size;
{
    GENERIC *ret;

    if ((long)size <= 0) core("internal error: dmalloc: size <= 0");
    if ((ret = (GENERIC*)malloc(size)) == NULL) core("% malloc failed");
    return ret;
}

GENERIC *drealloc(ptr, size)
    GENERIC *ptr;
    long unsigned size;
{
    GENERIC *ret;

    if ((long)size <= 0) core("internal error: drealloc: size <= 0");
    if ((ret = (GENERIC*)realloc(ptr, size)) == NULL) core("% realloc failed");
    return ret;
}
#endif
