/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tfio.c,v 35004.18 1997/04/02 23:50:20 hawkeye Exp $ */


/***********************************
 * TinyFugue "standard" I/O
 *
 * Written by Ken Keys
 *
 * Provides an interface similar to stdio.
 ***********************************/

#include "config.h"
#include <errno.h>
#include <sys/types.h>
#ifdef SYS_SELECT_H
# include SYS_SELECT_H
#endif
#include <sys/time.h>   /* for struct timeval, in select() */
#define TIME_H          /* prevent <time.h> in "tf.h" */
#include <sys/stat.h>

#ifndef HAVE_PWD_H
# undef HAVE_getpwnam
#else
# ifdef HAVE_getpwnam
#  include <pwd.h>	/* getpwnam() */
# endif
#endif

#include "port.h"
#include "dstring.h"
#include "tf.h"

#include "util.h"

#include "tfio.h"
#include "tfselect.h"
#include "output.h"
#include "macro.h"	/* macro_body() */
#include "history.h"
#include "search.h"	/* queues */
#include "signals.h"	/* shell_status() */
#include "variable.h"	/* getvar() */

extern int errno;
extern int restrict;
extern TFILE *tfscreen;

static TFILE *filemap[FD_SETSIZE];
static int selectable_tfiles = 0;

static void FDECL(fileputs,(CONST char *str, FILE *fp));
static void FDECL(queueputa,(Aline *aline, TFILE *file));


#ifndef HAVE_DRIVES
# define is_absolute_path(path) \
            ((path)[0] == '/' || (path)[0] == '~')
#else
# define is_absolute_path(path) \
            ((path)[0] == '/' || (path)[0] == '~' || \
            (isalpha((path)[0]) && (path)[1] == ':'))
#endif

void init_tfio()
{
    int i;

    for (i = 0; i < sizeof(filemap)/sizeof(*filemap); i++)
        filemap[i] = NULL;
}

/* tfname
 * Use <name> if given, otherwise use body of <macro> as the name.  A leading
 * "~username" followed by '/' or end of string is expanded to <username>'s
 * home directory; a leading "~" is expanded to the user's home directory.
 */
char *tfname(name, macro)
    CONST char *name, *macro;
{
    if (!name || !*name) {
        if (macro) {
            if (!(name=macro_body(macro)) || !*name) {
                eprintf("missing filename, and default macro %s is not defined",
                    macro);
            }
        } else {
            eprintf("missing filename");
        }
    }
    return (name && *name) ? expand_filename(name) : NULL;
}

char *expand_filename(str)
    CONST char *str;
{
    CONST char *dir, *user;
    STATIC_BUFFER(buffer);

    if (str) {
        if (*str != '~') return (char *)str;
        for (user = ++str; *str && *str != '/'; str++);
        if (str == user) {
            dir = getvar("HOME");
        } else {

#ifndef HAVE_getpwnam
            eprintf("warning: \"~user\" filename expansion is not supported.");
#else
            struct passwd *pw;
            Stringncpy(buffer, user, str - user);
            if ((pw = getpwnam(buffer->s)))
                dir = pw->pw_dir;
            else
#endif /* HAVE_getpwnam */
                return (char*)--user;
        }
        Stringcpy(buffer, dir ? dir : "");
        Stringcat(buffer, str);
    } else {
        Stringterm(buffer, 0);
    }
    return buffer->s;
}

/* tfopen - opens a TFILE.
 * Mode "q" will create a TF_QUEUE.
 * Mode "p" will open a command pipe for reading.
 * Modes "w", "r", and "a" open a regular file for read, write, or append.
 * If mode is "r", and the file is not found, will look for a compressed copy.
 * If still not found, and file is relative, tfopen will look in %TFLIBDIR.
 * If tfopen() fails, it will return NULL with errno set as in fopen();
 * if found file is a directory, tfopen() will return NULL with errno==EISDIR.
 */
TFILE *tfopen(name, mode)
    CONST char *name, *mode;
{
    int type = TF_FILE;
    FILE *fp;
    TFILE *result = NULL;
    CONST char *prog, *suffix;
    char *newname;
    STATIC_BUFFER(buffer);
    STATIC_BUFFER(libfile);
    struct stat buf;
    MODE_T st_mode = 0;

    if (*mode == 'q') {
        errno = EAGAIN;  /* in case malloc fails */
        if (!(result = (TFILE *)MALLOC(sizeof(TFILE)))) return NULL;
        result->type = TF_QUEUE;
        result->name = NULL;
        result->u.queue = (Queue *)XMALLOC(sizeof(Queue));
        init_queue(result->u.queue);
        return result;
    }

    if (!name || !*name) {
        errno = ENOENT;
        return NULL;
    }

    if (*mode == 'p') {
        if (!(fp = popen(name, "r"))) return NULL;
        result = (TFILE *)XMALLOC(sizeof(TFILE));
        result->type = TF_PIPE;
        result->name = STRDUP(name);
        result->u.fp = fp;
        result->off = result->len = 0;
        filemap[fileno(fp)] = result;
        selectable_tfiles++;
        return result;
    }

    if ((fp = fopen(name, mode)) && fstat(fileno(fp), &buf) == 0) {
        if (buf.st_mode & S_IFDIR) {
            fclose(fp);
            errno = EISDIR;  /* must be after fclose() */
            return NULL;
        }
        newname = STRDUP(name);
        st_mode = buf.st_mode;
    }

    /* If file did not exist, look for compressed copy. */
    if (!fp && *mode == 'r' && errno == ENOENT && restrict < RESTRICT_SHELL &&
        (suffix = macro_body("compress_suffix")) && *suffix &&
        (prog = macro_body("compress_read")) && *prog)
    {
        newname = (char*)XMALLOC(strlen(name) + strlen(suffix) + 1);
        strcat(strcpy(newname, name), suffix);

        if ((fp = fopen(newname, mode)) != NULL) {  /* test readability */
            fclose(fp);
#ifdef PLATFORM_UNIX
            Sprintf(buffer, 0, "%s %s 2>/dev/null", prog, newname);
#endif
#ifdef PLATFORM_OS2
            Sprintf(buffer, 0, "%s %s 2>nul", prog, newname);
#endif
            fp = popen(buffer->s, mode);
            type = TF_PIPE;
        }
    }


    /* If file did not exist and is relative, look in TFLIBDIR. */
    if (!fp && *mode == 'r' && errno == ENOENT && !is_absolute_path(name)) {
        if (!TFLIBDIR || !*TFLIBDIR || !is_absolute_path(TFLIBDIR)) {
            eprintf("warning: invalid value for %%TFLIBDIR");
        } else {
            Sprintf(libfile, 0, "%s/%s", TFLIBDIR, name);
            return tfopen(expand_filename(libfile->s), mode);
        }
    }

    if (fp) {
        errno = EAGAIN;  /* in case malloc fails */
        if (!(result = (TFILE*)MALLOC(sizeof(TFILE)))) return NULL;
        result->type = type;
        result->name = newname;
        result->u.fp = fp;
        result->off = result->len = 0;
        result->mode = st_mode;
        result->warned = 0;
    }

    return result;
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
        while(file->u.queue->head)
            free_aline((Aline*)unlist(file->u.queue->head, file->u.queue));
        FREE(file->u.queue);
        result = 0;
        break;
    case TF_FILE:
        result = fclose(file->u.fp);
        break;
    case TF_PIPE:
        filemap[fileno(file->u.fp)] = NULL;
        selectable_tfiles--;
        result = shell_status(pclose(file->u.fp));
        break;
    default:
        result = -1;
    }
    FREE(file);
    return result;
}

/* tfselect() is like select(), but also checks buffered TFILEs */
int tfselect(nfds, readers, writers, excepts, timeout)
    int nfds;
    fd_set *readers, *writers, *excepts;
    struct timeval *timeout;
{
    int i, count, tfcount = 0;
    fd_set tfreaders;

    if (!selectable_tfiles)
        return select(nfds, readers, writers, excepts, timeout);

    FD_ZERO(&tfreaders);

    for (i = 0; i < nfds; i++) {
        if (filemap[i] && FD_ISSET(i, readers)) {
            if (filemap[i]->off < filemap[i]->len) {
                FD_SET(i, &tfreaders);
                FD_CLR(i, readers);     /* don't check twice */
                tfcount++;
            }
        }
    }

    if (!tfcount) {
        return select(nfds, readers, writers, excepts, timeout);

    } else {
        /* we found at least one; poll the rest, but don't wait */
        struct timeval zero;
        zero.tv_sec = zero.tv_usec = 0;
        count = select(nfds, readers, writers, excepts, &zero);
        if (count < 0) return count;
        count += tfcount;

        for (i = 0; tfcount && i < nfds; i++) {
            if (FD_ISSET(i, &tfreaders)) {
                FD_SET(i, readers);
                tfcount--;
            }
        }

        return count;
    }
}

/**********
 * Output *
 **********/

/* tfputs
 * Print to a TFILE.
 * Unlike fputs(), tfputs() always appends a newline when writing to a file.
 */
void tfputs(str, file)
    CONST char *str;
    TFILE *file;
{
    if (file->type == TF_NULL) {
        /* do nothing */
    } else if (file->type == TF_QUEUE) {
        queueputa(new_aline(str, 0), file);
    } else {
        fileputs(str, file->u.fp);
    }
}

/* tfputa
 * Print an Aline to a TFILE, with embedded newline handling.
 */
void tfputa(aline, file)
    Aline *aline;
    TFILE *file;
{
    aline->links++;
    if (file->type == TF_NULL) {
        /* do nothing */
    } else if (file->type == TF_QUEUE) {
        queueputa(aline, file);
    } else {
        fileputs(aline->str, file->u.fp);
    }
    free_aline(aline);
}

static void queueputa(aline, file)
    Aline *aline;
    TFILE *file;
{
    aline->links++;
    if (file == tfscreen) {
        record_local(aline);
        record_global(aline);
        screenout(aline);
    } else if (file->type == TF_QUEUE) {
        aline->links++;
        enqueue(file->u.queue, aline);
    }
    free_aline(aline);
}

/* print a string to a file, converting embedded newlines to spaces */
static void fileputs(str, fp)
    CONST char *str;
    FILE *fp;
{
    CONST char *p;

    while ((p = strchr(str, '\n'))) {
        fflush(fp);
        write(fileno(fp), str, p - str);   /* up to newline */
        fputc(' ', fp);
        str = p + 1;
    }
    fputs(str, fp);
    fputc('\n', fp);
}


/* vSprintf
 * Similar to vsprintf, except:
 * second arg is a flag, third arg is format.
 * no length formating for %s.
 * %S is like %s, but takes a Stringp argument.
 * %q takes a char c and a string s; prints s, with \ before each c.
 * string arguments may be NULL.
 * newlines are not allowed in the format string (this is not enforced).
 */

void vSprintf(buf, flags, fmt, ap)
    Stringp buf;
    int flags;
    CONST char *fmt;
    va_list ap;
{
    static smallstr spec, tempbuf;
    CONST char *q, *sval;
    char *specptr, quote;
    String *Sval;

    if (!(flags & SP_APPEND)) Stringterm(buf, 0);
    while (*fmt) {
        if (*fmt != '%' || *++fmt == '%') {
            for (q = fmt + 1; *q && *q != '%'; q++);
            Stringncat(buf, fmt, q - fmt);
            fmt = q;
            continue;
        }

        specptr = spec;
        *specptr++ = '%';
        while (*fmt && !isalpha(*fmt)) *specptr++ = *fmt++;
        if (*fmt == 'h' || lcase(*fmt) == 'l') *specptr++ = *fmt++;
        *specptr = *fmt;
        *++specptr = '\0';

        switch (*fmt++) {
        case 'd':
        case 'i':
            sprintf(tempbuf, spec, va_arg(ap, int));
            Stringcat(buf, tempbuf);
            break;
        case 'x':
        case 'X':
        case 'u':
        case 'o':
            sprintf(tempbuf, spec, va_arg(ap, unsigned int));
            Stringcat(buf, tempbuf);
            break;
#if 0   /* not used */
        case 'f':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            sprintf(tempbuf, spec, va_arg(ap, double));
            Stringcat(buf, tempbuf);
            break;
#endif
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
            Stringcat(buf, spec);
            break;
        }
    }
}

#ifndef oprintf
/* oprintf
 * A newline will appended.  See vSprintf().
 */

void oprintf VDEF((CONST char *fmt, ...))
{
    va_list ap;
#ifndef HAVE_STDARG
    CONST char *fmt;
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
#endif /* oprintf */

/* tfprintf
 * Print to a TFILE.  A newline will appended.  See vSprintf().
 */

void tfprintf VDEF((TFILE *file, CONST char *fmt, ...))
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
void Sprintf VDEF((String *buf, int flags, CONST char *fmt, ...))
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

void eprintf VDEF((CONST char *fmt, ...))
{
    va_list ap;
#ifndef HAVE_STDARG
    CONST char *fmt;
#endif
    STATIC_BUFFER(buffer);
    extern CONST char *current_command;
    extern TFILE *loadfile;
    extern int loadline;

#ifdef HAVE_STDARG
    va_start(ap, fmt);
#else
    va_start(ap);
    fmt = va_arg(ap, char *);
#endif

    Stringcpy(buffer, "% ");
    if (loadfile)
        Sprintf(buffer, SP_APPEND, "%s, line %d: ", loadfile->name, loadline);
    if (current_command)
        Sprintf(buffer, SP_APPEND, "%s: ", current_command);

    vSprintf(buffer, SP_APPEND, fmt, ap);
    va_end(ap);
    eputs(buffer->s);
}


/*********
 * Input *
 *********/

/* read one char from keyboard, with blocking */
char igetchar()
{
    char c;
    fd_set readers;

    FD_ZERO(&readers);
    FD_SET(STDIN_FILENO, &readers);
    while(select(1, &readers, NULL, NULL, NULL) <= 0);
    read(STDIN_FILENO, &c, 1);
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
        int next;

        if (file->len < 0) return NULL;  /* eof or error */

        Stringterm(str, 0);

        do {
            while (file->off < file->len) {
                next = file->off + 1;
                if (file->buf[file->off] == '\n') {
                    file->off++;
                    return str;
                } else if (file->buf[file->off] == '\t') {
                    file->off++;
                    Stringnadd(str, ' ', tabsize - str->len % tabsize);
                }
                while (isprint(file->buf[next]) && next < file->len) next++;
                Stringncat(str, file->buf + file->off, next - file->off);
                file->off = next;
            }
            file->off = 0;
            file->len = read(fileno(file->u.fp), file->buf, sizeof(file->buf));
        } while (file->len > 0);

        file->len = -1;  /* note eof */
        return str->len ? str : NULL;
    }
}

/*
 * For each aline in <src>, record it in global history and,
 * if !quiet, put it on the <tfscreen> output queue.
 */
void flushout_queue(src, quiet)
    Queue *src;
    int quiet;
{
    extern unsigned int tfscreen_size;
    ListEntry *node;
    Queue *dest = tfscreen->u.queue;

    if (!src->head) return;
    for (node = src->tail; node; node = node->prev) {
        record_global((Aline *)node->datum);
        tfscreen_size++;
    }
    if (!quiet) {
        src->tail->next = dest->head;
        *(dest->head ? &dest->head->prev : &dest->tail) = src->tail;
        dest->head = src->head;
    }
    src->head = src->tail = NULL;
    oflush();
}

Aline *dnew_aline(str, attrs, len, file, line)
    CONST char *str;
    CONST char *file;
    attr_t attrs;
    int len, line;
{
    Aline *aline;
    void *memory;

    /* Optimization: allocating aline and aline->str in one chunk is faster,
     * and helps improve locality of reference.  Chars have size 1, so
     * alignment and arithmetic for the offset of str is not a problem.
     */
    memory = xmalloc(sizeof(Aline) + len + 1, file, line);
    aline = (Aline *)memory;
    aline->str = strncpy((char*)memory + sizeof(Aline), str, len);
    aline->str[len] = '\0';
    aline->len = len;
    aline->attrs = attrs;
    aline->partials = NULL;
    aline->links = 0;
    aline->time = time(NULL);
    return aline;
}

void dfree_aline(aline, file, line)
    Aline *aline;
    CONST char *file;
    int line;
{
    if (aline->links <= 0)
        tfprintf(tferr, "Internal error: dfree_aline, %s %d: links == %ld",
            file, line, (long)aline->links);
    else
        aline->links--;

    if (aline->links <= 0) {
        if (aline->partials) FREE(aline->partials);
        FREE(aline);  /* struct and string */
    }
}

