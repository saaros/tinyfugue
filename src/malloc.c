/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: malloc.c,v 33000.2 1994/04/16 03:19:53 hawkeye Exp $ */

#ifndef DMALLOC

#include "config.h"
#include "port.h"
#include "signals.h"
#include "malloc.h"

GENERIC *dmalloc(size)
    long unsigned size;
{
    GENERIC *ret;

    if ((long)size <= 0) core("internal error: dmalloc: size <= 0");
    if (!(ret = (GENERIC*)malloc(size))) core("% malloc failed");
    return ret;
}

GENERIC *drealloc(ptr, size)
    GENERIC *ptr;
    long unsigned size;
{
    GENERIC *ret;

    if ((long)size <= 0) core("internal error: drealloc: size <= 0");
    if (!(ret = (GENERIC*)realloc(ptr, size))) core("% realloc failed");
    return ret;
}

#endif
