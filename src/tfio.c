/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tfio.c,v 35004.60 1999/01/31 00:27:55 hawkeye Exp $ */


/***********************************
 * TinyFugue "standard" I/O
 *
 * Written by Ken Keys
 *
 * Provides an interface similar to stdio.
 ***********************************/

#include "config.h"
#include <sys/types.h>
#ifdef SYS_SELECT_H
# include SYS_SELECT_H
#endif
/* #include <sys/time.h> */   /* for struct timeval, in select() */
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
#include "keyboard.h"	/* keyboard_pos */
#include "expand.h"	/* current_command */
#include "commands.h"

TFILE *loadfile = NULL; /* currently /load'ing file */
int loadline = 0;       /* line number in /load'ing file */
int loadstart = 0;      /* line number of start of command in /load'ing file */
int read_depth = 0;     /* nesting level of user kb reads */
int readsafe = 0;       /* safe to do a user kb read? */
TFILE *tfkeyboard;      /* user input */
TFILE *tfscreen;        /* text waiting to be displayed */
TFILE *tfin;            /* pointer to current input queue */
TFILE *tfout;           /* pointer to current output queue */
TFILE *tferr;           /* pointer to current error queue */

static TFILE *filemap[FD_SETSIZE];
static int selectable_tfiles = 0;
static List userfilelist[1];
static int max_fileid = 0;

static void FDECL(fileputs,(CONST char *str, FILE *fp));
static void FDECL(queueputa,(Aline *aline, TFILE *file));


void init_tfio()
{
    int i;

    for (i = 0; i < sizeof(filemap)/sizeof(*filemap); i++)
        filemap[i] = NULL;
    init_list(userfilelist);

    /* tfkeyboard's queue is never actually used, it's just a place holder */
    tfin = tfkeyboard = tfopen("<tfkeyboard>", "q");
    tfkeyboard->mode = S_IRUSR;

    tfout = tferr = tfscreen = tfopen("<tfscreen>", "q");
    tfscreen->mode = S_IWUSR;
    tfscreen_size = 0;
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
    char *newname = NULL;
    STATIC_BUFFER(buffer);
    struct stat buf;
    MODE_T st_mode = 0;

    if (*mode == 'q') {
        errno = EAGAIN;  /* in case malloc fails */
        if (!(result = (TFILE *)MALLOC(sizeof(TFILE)))) return NULL;
        result->type = TF_QUEUE;
        result->name = name ? STRDUP(name) : NULL;
        result->id = -1;
        result->node = NULL;
        result->mode = S_IRUSR | S_IWUSR;
        result->tfmode = *mode;
        result->autoflush = 1;
        result->u.queue = (Queue *)XMALLOC(sizeof(Queue));
        init_queue(result->u.queue);
        return result;
    }

    if (!name || !*name) {
        errno = ENOENT;
        return NULL;
    }

    if (*mode == 'p') {
#ifdef __CYGWIN32__
        eprintf("TF does not support pipes under cygwin32.");
        errno = EPIPE;
        return NULL;
#endif
        if (!(fp = popen(name, "r"))) return NULL;
        result = (TFILE *)XMALLOC(sizeof(TFILE));
        result->type = TF_PIPE;
        result->name = STRDUP(name);
        result->id = -1;
        result->mode = S_IRUSR;
        result->tfmode = *mode;
        result->autoflush = 1;
        result->node = NULL;
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
    if (!fp && *mode == 'r' && errno == ENOENT && restriction < RESTRICT_SHELL &&
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

    if (fp) {
        errno = EAGAIN;  /* in case malloc fails */
        if (!(result = (TFILE*)MALLOC(sizeof(TFILE)))) return NULL;
        result->type = type;
        result->name = newname;
        result->id = 0;
        result->node = NULL;
        result->tfmode = *mode;
        result->autoflush = 1;
        result->u.fp = fp;
        result->off = result->len = 0;
        result->mode = st_mode;
        if (*mode == 'r' || *mode == 'p') {
            result->mode |= S_IRUSR;
            result->mode &= ~S_IWUSR;
        } else {
            result->mode &= ~S_IRUSR;
            result->mode |= S_IWUSR;
        }
        result->warned = 0;
    } else {
        if (newname) FREE(newname);
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

    if (!file) return -1;
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
    if (file->node)
        unlist(file->node, userfilelist);
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
    if (!file || file->type == TF_NULL) {
        /* do nothing */
    } else if (file->type == TF_QUEUE) {
        queueputa(new_aline(str, 0), file);
    } else {
        fileputs(str, file->u.fp);
        if (file->autoflush) tfflush(file);
    }
}

/* tfputansi
 * Print to a TFILE, with embedded ANSI display codes.
 */
attr_t tfputansi(str, file, attrs)
    CONST char *str;
    TFILE *file;
    attr_t attrs;
{
    Aline *aline;

    if (file && file->type != TF_NULL) {
        (aline = new_aline(str, 0))->links++;
        attrs = handle_ansi_attr(aline, attrs);
        if (attrs >= 0)
            tfputa(aline, file);
        free_aline(aline);
    }
    return attrs;
}

/* tfputa
 * Print an Aline to a TFILE, with embedded newline handling.
 */
void tfputa(aline, file)
    Aline *aline;
    TFILE *file;
{
    aline->links++;
    if (!file || file->type == TF_NULL) {
        /* do nothing */
    } else if (file->type == TF_QUEUE) {
        queueputa(aline, file);
    } else {
        fileputs(aline->str, file->u.fp);
        if (file->autoflush) tfflush(file);
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
    } else if (!file) {
        /* do nothing */
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
 * %S is like %s, but takes a Stringp argument.
 * %q takes a char c and a string s; prints s, with \ before each c.
 * %s, %S, and %q arguments may be NULL.
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
    int len, min, max, leftjust;

    if (!(flags & SP_APPEND)) Stringterm(buf, 0);
    while (*fmt) {
        if (*fmt != '%' || *++fmt == '%') {
            for (q = fmt + 1; *q && *q != '%'; q++);
            Stringfncat(buf, fmt, q - fmt);
            fmt = q;
            continue;
        }

        specptr = spec;
        *specptr++ = '%';
        while (*fmt && !is_alpha(*fmt)) *specptr++ = *fmt++;
        if (*fmt == 'h' || lcase(*fmt) == 'l') *specptr++ = *fmt++;
        *specptr = *fmt;
        *++specptr = '\0';

        switch (*fmt) {
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
        case 's':
        case 'S':
            sval = NULL;
            Sval = NULL;
            min = 0;
            max = -1;

            specptr = &spec[1];
            if ((leftjust = (*specptr == '-')))
                specptr++;
            if (*specptr == '*') {
                ++specptr;
                min = va_arg(ap, int);
            } else if (isdigit(*specptr)) {
                min = strtoint(&specptr);
            }
            if (*specptr == '.') {
                ++specptr;
                if (*specptr == '*') {
                    ++specptr;
                    max = va_arg(ap, int);
                } else if (isdigit(*specptr)) {
                    max = strtoint(&specptr);
                }
            }

            if (*fmt == 's') {
                sval = va_arg(ap, char *);
                len = sval ? strlen(sval) : 0;
            } else {
                Sval = va_arg(ap, String *);
                len = Sval ? Sval->len : 0;
            }

            if (max >= 0 && len > max) len = max;
            if (!leftjust && len < min) Stringnadd(buf, ' ', min - len);
            Stringfncat(buf, Sval ? Sval->s : sval ? sval : "", len);
            if (leftjust && len < min) Stringnadd(buf, ' ', min - len);
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
                Stringfncat(buf, sval, q - sval);
            }
            break;
        default:
            Stringcat(buf, spec);
            break;
        }
        fmt++;
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

void eprefix(buffer)
    String *buffer;
{
    Stringcpy(buffer, "% ");
    if (loadfile) {
        Sprintf(buffer, SP_APPEND, "%s, line", loadfile->name);
        if (loadstart == loadline)
            Sprintf(buffer, SP_APPEND, " %d: ", loadline);
        else
            Sprintf(buffer, SP_APPEND, "s %d-%d: ", loadstart, loadline);
    }
    if (current_command && *current_command != '\b')
        Sprintf(buffer, SP_APPEND, "%s: ", current_command);
}

void eprintf VDEF((CONST char *fmt, ...))
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

    eprefix(buffer);
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
    if (!file) {
        return NULL;

    } else if (file == tfkeyboard) {
        /* This is a hack.  It's a useful feature, but doing it correctly
         * without blocking tf would require making the macro language
         * suspendable, which would have required a major redesign.  The
         * nested main_loop() method was easy to add, but leads to a few
         * quirks, like the odd handling of /dokey newline.
         */
        TFILE *oldtfout, *oldtfin;

        if (!readsafe) {
            eprintf("keyboard can only be read from a command line command.");
            return NULL;
        }
        if (read_depth) eprintf("warning: nested keyboard read");
        oldtfout = tfout;
        oldtfin = tfin;
        tfout = tfscreen;
        tfin = tfkeyboard;
        readsafe = 0;
        read_depth++; update_status_field(NULL, STAT_READ);
        main_loop();
        read_depth--; update_status_field(NULL, STAT_READ);
        readsafe = 1;
        tfout = oldtfout;
        tfin = oldtfin;
        if (interrupted())
            return NULL;

        SStringcpy(str, keybuf);
        Stringterm(keybuf, keyboard_pos = 0);
        return str;

    } else if (file->type == TF_QUEUE) {
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
                while (is_print(file->buf[next]) && next < file->len) next++;
                Stringfncat(str, file->buf + file->off, next - file->off);
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
    ListEntry *node;
    Queue *dest = tfscreen->u.queue;
    int count = 0;

    if (!src->head) return;
    for (node = src->tail; node; node = node->prev) {
        record_global((Aline *)node->datum);
        count++;
    }
    if (!quiet) {
        tfscreen_size += count;
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
    aline->tv.tv_sec = -1;  /* this will be set by caller, if caller needs it */
    aline->tv.tv_usec = 0;
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


/**************
 * User level *
 **************/

int handle_tfopen_func(name, mode)
    CONST char *name, *mode;
{
    TFILE *file;

    if (restriction >= RESTRICT_FILE) {
        eprintf("restricted");
        return -1;
    }

    if (mode[1] || !strchr("rwapq", mode[0])) {
        eprintf("invalid mode '%s'", mode);
        return -1;
    }
    file = tfopen(expand_filename(name), mode);
    if (!file) {
        eprintf("%s: %s", name, strerror(errno));
        return -1;
    }

    file->node = inlist(file, userfilelist, userfilelist->tail);
    if (!file->node) {
        eprintf("%s: %s", name, strerror(errno));
        return -1;
    }
    file->id = ++max_fileid;
    return file->id;
}

TFILE *find_tfile(handle)
    CONST char *handle;
{
    ListEntry *node;
    int id;

    if (isalpha(handle[0]) && !handle[1]) {
        switch(lcase(handle[0])) {
            case 'i':  return tfin;
            case 'o':  return tfout;
            case 'e':  return tferr;
            default:   break;
        }
    } else {
        id = atoi(handle);
        for (node = userfilelist->head; node; node = node->next) {
            if (((TFILE*)node->datum)->id == id)
                return (TFILE*)node->datum;
        }
    }
    eprintf("%s: bad handle", handle);
    return NULL;
}

TFILE *find_usable_tfile(handle, mode)
    CONST char *handle;
    int mode;
{
    TFILE *tfile;

    if (!(tfile = find_tfile(handle)))
        return NULL;

    if (mode) {
        if (!(tfile->mode & mode) ||
            (mode & S_IRUSR && (tfile == tfout || tfile == tferr)) ||
            (mode & S_IWUSR && (tfile == tfin))) {
            eprintf("stream %s is not %sable", handle,
                mode == S_IRUSR ? "read" : "writ");
            return NULL;
        }
    }

    return tfile;
}

struct Value *handle_liststreams_command(args)
    char *args;
{
    int count = 0;
    TFILE *file;
    ListEntry *node;

    if (!userfilelist->head) {
        oprintf("% No open streams.");
        return 0;
    }
    oprintf("HANDLE MODE FLUSH NAME");
    for (node = userfilelist->head; node; node = node->next) {
        file = (TFILE*)node->datum;
        oprintf("%6d   %c   %3s  %s", file->id, file->tfmode,
            (file->tfmode == 'w' || file->tfmode == 'a') ?
                enum_flag[file->autoflush] : "",
            file->name ? file->name : "");
        count++;
    }

    return newint(count);
}

