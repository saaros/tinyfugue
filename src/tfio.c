/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tfio.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/***********************************
 * TinyFugue "standard" I/O
 *
 * Written by Ken Keys
 *
 * Provides an interface similar to stdio.
 ***********************************/

#include "config.h"
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>   /* for struct timeval, in select() */
#define SYS_TIME_H      /* to prevent <time.h> in "port.h" */
#include "port.h"
#include "dstring.h"
#include "fd_set.h"
#include "tf.h"
#include "util.h"
#include "output.h"
#include "macro.h"
#include "history.h"
#include "search.h"

extern int restrict;

String *error_prefix()
{
    extern char *current_command;
    extern TFILE *current_file;
    extern int current_lineno;
    STATIC_BUFFER(buffer);

    if (current_file)
        Sprintf(buffer, 0, "%% %s, line %d: %s", current_file->name,
            current_lineno, current_command ? current_command : "error");
    else
        Sprintf(buffer, 0, "%% %s",
            current_command ? current_command : "error");
    return buffer;
}

/* tfname
 * Use <name> if given, otherwise use body of <macro> as the name.
 * An initial '~' is expanded to the user's home directory.
 * tfname has no stdio counterpart.
 */
char *tfname(name, macro)
    char *name, *macro;
{
    STATIC_BUFFER(buffer);

    if (!name || !*name) 
        if (!macro || !(name = macro_body(macro)) || !*name) {
            tfputs("% Name of file unknown.", tferr);
            return NULL;
        }
    Stringcpy(buffer, name);
    expand_filename(buffer);
    return buffer->s;
}

String *expand_filename(str)
    Stringp str;
{
    char *env;
    STATIC_BUFFER(buffer);

    Stringterm(buffer, 0);
    if (str->s[0] != '~') return str;
    Sprintf(buffer, 0, "%s%s", (env = getvar("HOME")) ? env : "", str->s + 1);
    SStringcpy(str, buffer);
    return str;
}

/* tfopen - opens a TFILE.
 * Mode "q" will create a TF_QUEUE.
 * Mode "p" will open a command pipe for reading.
 * Modes "w", "r", and "a" open a regular file for read, write, or append.
 * If mode is "r", and the file is not found, will look for a compressed copy.
 * If still not found, and file is relative, tfopen will look in %TFLIBDIR.
 */
TFILE *tfopen(name, mode)
    char *name, *mode;
{
    int type = TF_FILE;
    FILE *fp;
    TFILE *result = NULL;
    char *prog, *suffix;
    STATIC_BUFFER(buffer);
    STATIC_BUFFER(libfile);

    if (*mode == 'q') {
        result = (TFILE *)MALLOC(sizeof(TFILE));
        result->type = TF_QUEUE;
        result->name = NULL;
        result->u.queue = (Queue *)MALLOC(sizeof(Queue));
        init_queue(result->u.queue);
        return result;
    }

    if (!name || !*name) return NULL;

    if (*mode == 'p') {
        if (!(fp = popen(name, "r"))) return NULL;
        result = (TFILE *)MALLOC(sizeof(TFILE));
        result->type = TF_PIPE;
        result->name = STRDUP(name);
        result->u.fp = fp;
        return result;
    }

    fp = fopen(name, mode);

    if (*mode == 'r' && !fp && errno == ENOENT) {
        /* If file did not exist, look for compressed copy. */
        if (restrict < RESTRICT_SHELL &&
          (suffix = macro_body("compress_suffix")) && *suffix &&
          (prog = macro_body("compress_read")) && *prog) {
            Stringcat(Stringcpy(buffer, name), suffix);
            if ((fp = fopen(buffer->s, mode)) != NULL) {  /* test readability */
                fclose(fp);
                Sprintf(buffer, 0, "%s %s%s", prog, name, suffix);
                fp = popen(buffer->s, mode);
                type = TF_PIPE;
            }
        }

        /* If file did not exist and is relative, look in TFLIBDIR. */
        if (!fp && errno == ENOENT && *name != '/') {
            if (!TFLIBDIR || *TFLIBDIR != '/') {
                tfputs("% warning: invalid value for %TFLIBDIR", tferr);
            } else {
                Sprintf(libfile, 0, "%s/%s", TFLIBDIR, name);
                return tfopen(libfile->s, mode);
            }
        }
    }

    if (fp) {
        result = (TFILE*)MALLOC(sizeof(TFILE));
        result->type = type;
        if (type == TF_PIPE) {
            result->name = (char *)MALLOC(strlen(name) + strlen(suffix) + 1);
            strcat(strcpy(result->name, name), suffix);
        } else {
            result->name = STRDUP(name);
        }
        result->u.fp = fp;
    }
    return result;
}

/* tfjump
 * Like fseek(), but only seeks forward.  Not implemented for TF_QUEUEs.
 */
int tfjump(file, offset)
    TFILE *file;
    long offset;
{
    char buffer[BUFSIZ];

    switch(file->type) {
    case TF_QUEUE:
        tfputs("% internal error:  attempted tfjump on queue", tferr);
        return -1;
    case TF_FILE:
        return fseek(file->u.fp, offset, 0);
    case TF_PIPE:
        while (offset > BUFSIZ) {
            if (!fread(buffer, sizeof(char), BUFSIZ, file->u.fp)) return -1;
            offset -= BUFSIZ;
        }
        return fread(buffer, sizeof(char), offset, file->u.fp) ? 0 : -1;
    default:
        return -1;  /* can't happen */
    }
}

/* tfclose
 * Close a TFILE created by tfopen().
 */
int tfclose(file)
    TFILE *file;
{
    int result;

    if (file->name) FREE(file->name);
    switch(file->type) {
    case TF_QUEUE:
        free_queue(file->u.queue);
        FREE(file->u.queue);
        result = 1;
        break;
    case TF_FILE:
        result = fclose(file->u.fp);
        break;
    case TF_PIPE:
        result = pclose(file->u.fp);
        break;
    default:
        result = 0;
        tfputs("% internal error: tfclose(): unknown file type", tferr);
    }
    FREE(file);
    return result;
}


/**********
 * Output *
 **********/

/* tfputs
 * Print to a TFILE.  Unlike fputs(), tfputs() always appends a newline.
 */
void tfputs(str, file)
    char *str;
    TFILE *file;
{
    if (file->type == TF_QUEUE) {
        tfputa(new_aline(str, F_NEWLINE), file);
    } else {
        fputs(str, file->u.fp);
        fputc('\n', file->u.fp);
    }
}

/* tfputa
 * Print an Aline to a TFILE.
 */
void tfputa(aline, file)
    Aline *aline;
    TFILE *file;
{
    extern TFILE *tfscreen;

    if (file == tfscreen) {
        record_local(aline);
        globalout(aline);
    } else if (file->type == TF_QUEUE) {
        aline->links++;
        enqueue(file->u.queue, aline);
    } else {
        fputs(aline->str, file->u.fp);
        fputc('\n', file->u.fp);
    }
}


/* vSprintf
 * Similar to vsprintf, except:
 * second arg is a flag, third arg is format;
 * no length formating for %s;
 * %S is like %s, but takes a Stringp argument;
 * %q takes a char c and a string s; prints s, with \ before each c.
 * string arguments may be NULL;
 * newlines are not allowed in the format string (this is not enforced).
 */

void vSprintf(buf, flags, fmt, ap)
    Stringp buf;
    int flags;
    char *fmt;
    va_list ap;
{
    static smallstr lfmt, tempbuf;
    char *q, *lfmtptr, quote;
    char *sval;
    String *Sval;

    if (!(flags & SP_APPEND)) Stringterm(buf, 0);
    for (; *fmt; fmt++) {
        if (*fmt != '%' || *++fmt == '%') {
            for (q = fmt + 1; *q && *q != '%'; q++);
            Stringncat(buf, fmt, q - fmt);
            fmt = q - 1;
            continue;
        }
        lfmtptr = lfmt;
        *lfmtptr++ = '%';
        while (*fmt && !strchr("dixXuofeEfGcsSq", *fmt)) *lfmtptr++ = *fmt++;
        *lfmtptr++ = *fmt;
        *lfmtptr = '\0';
        switch (*fmt) {
        case 'd':
        case 'i':
            sprintf(tempbuf, lfmt, va_arg(ap, int));
            Stringcat(buf, tempbuf);
            break;
        case 'x':
        case 'X':
        case 'u':
        case 'o':
            sprintf(tempbuf, lfmt, va_arg(ap, unsigned int));
            Stringcat(buf, tempbuf);
            break;
        case 'f':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            sprintf(tempbuf, lfmt, va_arg(ap, double));
            Stringcat(buf, tempbuf);
            break;
        case 'c':
            Stringadd(buf, (char)va_arg(ap, int));
            break;
        case 's':                       /* Sorry, no length formatting */
            if ((sval = va_arg(ap, char *))) Stringcat(buf, sval);
            break;
        case 'S':
            if ((Sval = va_arg(ap, String *))) SStringcat(buf, Sval);
            break;
        case 'q':
            if (!(quote = (char)va_arg(ap, int))) break;
            if (!(sval = va_arg(ap, char *))) break;
            for ( ; *sval; sval = q) {
                if (*sval == quote || *sval == '\\') {
                    Stringadd(buf, '\\');
                    Stringadd(buf, *sval++);
                }
                for (q = sval; *q && *q != quote && *q != '\\'; q++);
                Stringncat(buf, sval, q - sval);
            }
            break;
        default:
            Stringcat(buf, lfmt);
            break;
        }
    }
}

/* oprintf
 * A newline will appended.  See vSprintf().
 */

#ifdef HAVE_STDARG
void oprintf(char *fmt, ...)
#else
/* VARARGS */
void oprintf(va_alist)
va_dcl
#endif
{
    va_list ap;
#ifndef HAVE_STDARG
    char *fmt;
#endif
    STATIC_BUFFER(buffer);

#ifdef HAVE_STDARG
    va_start(ap, fmt);
#else
    va_start(ap);
    fmt = va_arg(ap, char *);
#endif
    vSprintf(buffer, 0, fmt, ap);
    va_end(ap);
    oputs(buffer->s);
}

/* tfprintf
 * Print to a TFILE.  A newline will appended.  See vSprintf().
 */

#ifdef HAVE_STDARG
void tfprintf(TFILE *file, char *fmt, ...)
#else
/* VARARGS */
void tfprintf(va_alist)
va_dcl
#endif
{
    va_list ap;
#ifndef HAVE_STDARG
    char *fmt;
    TFILE *file;
#endif
    STATIC_BUFFER(buffer);

#ifdef HAVE_STDARG
    va_start(ap, fmt);
#else
    va_start(ap);
    file = va_arg(ap, TFILE *);
    fmt = va_arg(ap, char *);
#endif
    vSprintf(buffer, 0, fmt, ap);
    va_end(ap);
    tfputs(buffer->s, file);
}

/* Sprintf
 * Print into a String.  See vSprintf().
 */

#ifdef HAVE_STDARG
void Sprintf(String *buf, int flags, char *fmt, ...)
#else
/* VARARGS */
void Sprintf(va_alist)
va_dcl
#endif
{
    va_list ap;
#ifndef HAVE_STDARG
    String *buf;
    int flags;
    char *fmt;
#endif

#ifdef HAVE_STDARG
    va_start(ap, fmt);
#else
    va_start(ap);
    buf = va_arg(ap, String *);
    flags = va_arg(ap, int);
    fmt = va_arg(ap, char *);
#endif
    vSprintf(buf, flags, fmt, ap);
    va_end(ap);
}


/*********
 * Input *
 *********/

char igetchar()
{
    char c;
    fd_set readers;

    FD_ZERO(&readers);
    FD_SET(0, &readers);
    while(select(1, &readers, NULL, NULL, NULL) <= 0);
    read(0, &c, 1);
    return c;
}


/* Unlike fgets, tfgetS() does not retain terminating newline. */
String *tfgetS(str, file)
    Stringp str;
    TFILE *file;
{
    if (file->type == TF_QUEUE) {
        Aline *aline;
        do {
            if (!(aline = dequeue(file->u.queue))) return NULL;
            if (!((aline->attrs & F_GAG) && gag)) break;
            free_aline(aline);
        } while (1);
        Stringcpy(str, aline->str);
        free_aline(aline);
        return str;
    } else {
        char buf[80];
        char *start, *end;

        Stringterm(str, 0);
        if (fgets(buf, 80, file->u.fp) == NULL) return NULL;
        do {
            for (start = end = buf; *end; start = end + 1) {
                for (end = start; *end && *end != '\t'; ++end);
                Stringncat(str, start, end - start);
                if (*end == '\t') Stringnadd(str, ' ', 8 - str->len % 8);
            }
        } while (str->s[str->len - 1] != '\n' && fgets(buf, 80, file->u.fp));
        if (str->s[str->len - 1] == '\n') str->s[--str->len] = '\0';
        return str;
    }
}

