/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: dstring.c,v 35004.4 1997/03/27 01:04:22 hawkeye Exp $ */


/*********************************************************************
 * Fugue dynamically allocated string handling                       *
 *                                                                   *
 * dSinit() must be used to initialize a dynamically allocated       *
 * string, and dSfree() to free up the contents.  To minimize        *
 * realloc()s, initialize the size to be a little more than the      *
 * median expected size.                                             *
 *********************************************************************/

#include "config.h"
#include "port.h"
#include "malloc.h"
#include "dstring.h"
#include "signals.h"	/* core() */

#define lcheck(str, file, line) \
        do { if ((str)->len >= (str)->size)  resize(str, file, line); } while(0)

static void  FDECL(resize,(Stringp str, CONST char *file, int line));

String *dSinit(str, size, file, line)
    Stringp str;
    unsigned size;    /* estimate just over median size, to minimize resizes */
    CONST char *file;
    int line;
{
    str->size = ((size + ALLOCSIZE - 1) / ALLOCSIZE) * ALLOCSIZE;
    str->s = (char *) xmalloc(str->size * sizeof(char), file, line);
    str->s[0] = '\0';
    str->len = 0;
    /* fprintf(stderr, "%s:%d\tinit\t%8u\n", file, line, str->size); */
    return str;
}

void dSfree(str, file, line)
    Stringp str;
    CONST char *file;
    int line;
{
    /* fprintf(stderr, "%s:%d\tfree\t%8u (%8u)\n", file, line, str->size, str->len); */
    if (str->s) FREE(str->s);    /* Might have been an unused STATIC_BUFFER */
    str->s = NULL;
    str->size = str->len = 0;
}

static void resize(str, file, line)
    Stringp str;
    CONST char *file;
    int line;
{
    /* fprintf(stderr, "%s:%d\tresize\t%8u to %8lu (%8u)\n",
        file, line, str->size, (str->len/ALLOCSIZE+1)*ALLOCSIZE, str->len); */
    str->size = (str->len / ALLOCSIZE + 1) * ALLOCSIZE;
    str->s = (str->s)
        ? (char*) xrealloc(str->s, str->size * sizeof(char), file, line)
        : (char*) xmalloc(str->size * sizeof(char), file, line);
}

String *dSadd(str, c, file, line)
    Stringp str;
    int c;
    CONST char *file;
    int line;
{
    str->len++;
    lcheck(str, file, line);
    str->s[str->len - 1] = c;
    str->s[str->len] = '\0';
    return str;
}

String *dSnadd(str, c, n, file, line)
    Stringp str;
    int c;
    unsigned int n;
    CONST char *file;
    int line;
{
    if ((int)n < 0) core("dSnadd: n==%ld", file, line, (long)n);
    str->len += n;
    lcheck(str, file, line);
    while (n) str->s[str->len - n--] = c;
    str->s[str->len] = '\0';
    return str;
}

String *dSterm(str, len, file, line)
    Stringp str;
    unsigned int len;
    CONST char *file;
    int line;
{
    if (str->size && str->len < len) return str;
    str->len = len;
    lcheck(str, file, line);
    str->s[len] = '\0';
    return str;
}

String *dScpy(dest, src, file, line)
    Stringp dest;
    CONST char *src;
    CONST char *file;
    int line;
{
    dest->len = strlen(src);
    lcheck(dest, file, line);
    strcpy(dest->s, src);
    return dest;
}

String *dSScpy(dest, src, file, line)
    Stringp dest;
    CONST Stringp src;
    CONST char *file;
    int line;
{
    dest->len = src->len;
    lcheck(dest, file, line);
    strcpy(dest->s, src->s ? src->s : "");
    return dest;
}

String *dSncpy(dest, src, n, file, line)
    Stringp dest;
    CONST char *src;
    unsigned int n;
    CONST char *file;
    int line;
{
    unsigned len = strlen(src);

    if ((int)n < 0) core("dSncpy: n==%ld", file, line, (long)n);
    if (n > len) n = len;
    dest->len = n;
    lcheck(dest, file, line);
    strncpy(dest->s, src, n);
    dest->s[n] = '\0';
    return dest;
}

String *dScat(dest, src, file, line)
    Stringp dest;
    CONST char *src;
    CONST char *file;
    int line;
{
    unsigned int len = dest->len;

    dest->len += strlen(src);
    lcheck(dest, file, line);
    strcpy(dest->s + len, src);
    return dest;
}

String *dSScat(dest, src, file, line)
    Stringp dest;
    CONST Stringp src;
    CONST char *file;
    int line;
{
    unsigned int len = dest->len;

    dest->len += src->len;
    lcheck(dest, file, line);
    strcpy(dest->s + len, src->s ? src->s : "");
    return dest;
}

String *dSncat(dest, src, n, file, line)
    Stringp dest;
    CONST char *src;
    unsigned int n;
    CONST char *file;
    int line;
{
    unsigned int oldlen = dest->len;
    unsigned int len = strlen(src);

    if ((int)n < 0) core("dSncat: n==%ld", file, line, (long)n);
    if (n > len) n = len;
    dest->len += n;
    lcheck(dest, file, line);
    strncpy(dest->s + oldlen, src, n);
    dest->s[dest->len] = '\0';
    return dest;
}

