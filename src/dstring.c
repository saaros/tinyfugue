/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: dstring.c,v 35004.9 1999/01/31 00:27:39 hawkeye Exp $ */


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
#ifdef DMALLOC
    str->is_static = 0;
#endif
    /* fprintf(stderr, "%s:%d\tinit\t%8u\n", file, line, str->size); */
    return str;
}

void dSfree(str, file, line)
    Stringp str;
    CONST char *file;
    int line;
{
    /* fprintf(stderr, "%s:%d\tfree\t%8u (%8u)\n", file, line, str->size, str->len); */
#ifdef DMALLOC
    if (!str->is_static)
#endif
    if (str->s) FREE(str->s);
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
#ifdef DMALLOC
    if (str->is_static) {
        str->s = (str->s)
            ? (char*) realloc(str->s, str->size * sizeof(char))
            : (char*) malloc(str->size * sizeof(char));
        if (!str->s)
            error_exit("{m,re}alloc(%ld): out of memory.",
                file, line, (long)str->size);
    } else
#endif
    {
        str->s = (str->s)
            ? (char*) xrealloc(str->s, str->size * sizeof(char), file, line)
            : (char*) xmalloc(str->size * sizeof(char), file, line);
    }
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
    /* if (str->size && str->len < len) return str; */
    unsigned int oldlen = str->len;
    str->len = len;
    lcheck(str, file, line);
    if (len < oldlen)
        str->s[len] = '\0';
    else
        str->len = oldlen;
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

/* slow version of dSncat, verifies that length of input >= n */
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

/* fast version of dSncat, assumes length of input >= n */
String *dSfncat(dest, src, n, file, line)
    Stringp dest;
    CONST char *src;
    unsigned int n;
    CONST char *file;
    int line;
{
    unsigned int oldlen = dest->len;

    if ((int)n < 0) core("dSfncat: n==%ld", file, line, (long)n);
    dest->len += n;
    lcheck(dest, file, line);
    strncpy(dest->s + oldlen, src, n);
    dest->s[dest->len] = '\0';
    return dest;
}

