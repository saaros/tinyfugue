/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: malloc.c,v 35004.8 1999/01/31 00:27:48 hawkeye Exp $ */

#include "config.h"
#include "port.h"
#include "signals.h"
#include "malloc.h"

int low_memory_warning = 0;
static char *reserve = NULL;

void init_malloc()
{
    reserve = MALLOC(1024*16);
}

GENERIC *xmalloc(size, file, line)
    long unsigned size;
    CONST char *file;
    CONST int line;
{
    GENERIC *memory;

    if ((long)size <= 0)
        core("xmalloc(%ld).", file, line, (long)size);

    memory = (GENERIC*)dmalloc(size, file, line);
    if (!memory) {
        if (reserve) {
            low_memory_warning = 1;
            FREE(reserve);
            reserve = NULL;
            memory = (GENERIC*)dmalloc(size, file, line);
        }
        if (!memory)
            error_exit("xmalloc(%ld): out of memory.", file, line, (long)size);
    }

    return memory;
}

GENERIC *xrealloc(ptr, size, file, line)
    GENERIC *ptr;
    long unsigned size;
    CONST char *file;
    CONST int line;
{
    GENERIC *memory;

    if ((long)size <= 0)
        core("xrealloc(%ld).", file, line, (long)size);

    memory = (GENERIC*)drealloc(ptr, size, file, line);
    if (!memory) {
        if (reserve) {
            low_memory_warning = 1;
            FREE(reserve);
            reserve = NULL;
            memory = (GENERIC*)drealloc(ptr, size, file, line);
        }
        if (!memory)
            error_exit("xrealloc(%ld): out of memory.", file, line, (long)size);
    }

    return memory;
}

void xfree(ptr, file, line)
    GENERIC *ptr;
    CONST char *file;
    CONST int line;
{
    dfree(ptr, file, line);
    if (!reserve)
        init_malloc();
}

#ifdef DMALLOC
void free_reserve()
{
    FREE(reserve);
}
#endif
