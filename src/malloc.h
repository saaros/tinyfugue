/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: malloc.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef MALLOC_H
#define MALLOC_H

#ifdef DMALLOC
#define MALLOC(size) dmalloc((size), __FILE__, __LINE__)
#define REALLOC(ptr, size) drealloc((ptr), (size), __FILE__, __LINE__)
#define FREE(ptr) dfree((GENERIC*)(ptr), __FILE__, __LINE__)
#else
#define MALLOC(size) dmalloc((size))
#define REALLOC(ptr, size) drealloc((ptr), (size))
#define FREE(ptr) free((GENERIC*)(ptr))
#endif

#ifdef DMALLOC
extern GENERIC  *FDECL(dmalloc,(long unsigned size, CONST char *file, CONST int line));
extern GENERIC  *FDECL(drealloc,(GENERIC *ptr, long unsigned size, CONST char *file, CONST int line));
extern void   FDECL(dfree,(GENERIC *ptr, CONST char *file, CONST int line));
#else
extern GENERIC  *FDECL(dmalloc,(long unsigned size));
extern GENERIC  *FDECL(drealloc,(GENERIC *ptr, long unsigned size));
#endif

#endif /* MALLOC_H */
