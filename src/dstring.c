/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: dstring.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/*********************************************************************
 * Fugue dynamically allocated string handling                       *
 *                                                                   *
 * Stringinit() must be used to initialize a dynamically allocated   *
 * string, and Stringfree() to free up the contents so as to prevent *
 * memory leaks.                                                     *
 *                                                                   *
 *********************************************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "malloc.h"
#include "dstring.h"

#define ALLOCSIZE 32L

#define lcheck(str) do { if ((int)(str)->len > (int)(str)->maxlen) resize(str); } while(0)

static void  FDECL(resize,(Stringp str));

#ifdef DMALLOC
String *dStringinit(str, file, line)
    Stringp str;
    char *file;
    int line;
{
    str->s = (char *) dmalloc(ALLOCSIZE * sizeof(char), file, line);
    str->s[0] = '\0';
    str->len = 0;
    str->maxlen = ALLOCSIZE - 1;
    return str;
}
#else
String *Stringinit(str)
    Stringp str;
{
    str->s = (char *) MALLOC(ALLOCSIZE * sizeof(char));
    str->s[0] = '\0';
    str->len = 0;
    str->maxlen = ALLOCSIZE - 1;
    return str;
}
#endif

void Stringfree(str)
    Stringp str;
{
    FREE(str->s);
    str->s = NULL;
    str->maxlen = str->len = 0;
}

static void resize(str)
    Stringp str;
{
    int size;

    size = (str->len + 1) / ALLOCSIZE + 1;
    str->maxlen = size * ALLOCSIZE - 1;
    str->s = (str->s)
        ? (char*) REALLOC(str->s, ((str->maxlen + 1) * sizeof(char)))
        : (char*) MALLOC((str->maxlen + 1) * sizeof(char));
}

String *Stringadd(str, c)
    Stringp str;
    char c;
{
    str->len++;
    lcheck(str);
    str->s[str->len - 1] = c;
    str->s[str->len] = '\0';
    return str;
}

String *Stringnadd(str, c, n)
    Stringp str;
    char c;
    unsigned int n;
{
    if (n <= 0) return str;
    str->len += n;
    lcheck(str);
    while (n) str->s[str->len - n--] = c;
    str->s[str->len] = '\0';
    return str;
}

String *Stringterm(str, len)
    Stringp str;
    unsigned int len;
{
    if (str->len < len) return str;
    str->len = len;
    lcheck(str);
    str->s[len] = '\0';
    return str;
}

String *Stringcpy(dest, src)
    Stringp dest;
    char *src;
{
    dest->len = strlen(src);
    lcheck(dest);
    strcpy(dest->s, src);
    return dest;
}

String *SStringcpy(dest, src)
    Stringp dest, src;
{
    dest->len = src->len;
    lcheck(dest);
    strcpy(dest->s, src->s);
    return dest;
}

String *Stringncpy(dest, src, len)
    Stringp dest;
    char *src;
    unsigned int len;
{
    dest->len = strlen(src);
    if (len < dest->len) dest->len = len;
    lcheck(dest);
    strncpy(dest->s, src, dest->len);
    dest->s[dest->len] = '\0';
    return dest;
}

String *Stringcat(dest, src)
    Stringp dest;
    char *src;
{
    unsigned int len = dest->len;

    dest->len += strlen(src);
    lcheck(dest);
    strcpy(dest->s + len, src);
    return dest;
}

String *SStringcat(dest, src)
    Stringp dest, src;
{
    unsigned int len = dest->len;

    dest->len += src->len;
    lcheck(dest);
    strcpy(dest->s + len, src->s);
    return dest;
}

String *Stringncat(dest, src, len)
    Stringp dest;
    char *src;
    unsigned int len;
{
    unsigned int oldlen = dest->len;

    dest->len += len;
    lcheck(dest);
    strncpy(dest->s + oldlen, src, len);
    dest->s[dest->len] = '\0';
    return dest;
}

/* make sure buffer ends in <n> newlines */
String *newline_package(buffer, n)
    Stringp buffer;
    unsigned int n;
{
    while (buffer->len > 0 && buffer->s[buffer->len - 1] == '\n')
        buffer->len--;
    buffer->s[buffer->len] = '\0';
    Stringnadd(buffer, '\n', n);
    return buffer;
}

