/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tfio.h,v 35004.30 1999/01/31 00:27:55 hawkeye Exp $ */

#ifndef TFIO_H
#define TFIO_H

#ifdef _POSIX_VERSION
# include <sys/types.h>
# define MODE_T mode_t
#else
# define MODE_T unsigned long
#endif

#ifdef HAVE_STDARG
# include <stdarg.h>
#else
# include <varargs.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_IROTH
# define S_IWUSR 00200
# define S_IRUSR 00400
# define S_IRGRP 00040
# define S_IROTH 00004
#endif

/* TFILE types */
#define TF_NULL     0
#define TF_QUEUE    1
#define TF_FILE     2
#define TF_PIPE     3

/* Sprintf flags */
#define SP_APPEND   1

/* TF's analogue of stdio's FILE */
typedef struct TFILE {
    int id;
    struct ListEntry *node;
    int type;
    char *name;
    union {
        struct Queue *queue;
        FILE *fp;
    } u;
    char buf[1024];
    int off, len;
    MODE_T mode;
    char tfmode;
    short warned;
    short autoflush;
} TFILE;


#ifndef HAVE_DRIVES
# define is_absolute_path(path) \
            ((path)[0] == '/' || (path)[0] == '~')
#else
# define is_absolute_path(path) \
            ((path)[0] == '/' || (path)[0] == '~' || \
            (is_alpha((path)[0]) && (path)[1] == ':'))
#endif


extern TFILE *loadfile;    /* currently /load'ing file */
extern int loadline;       /* line number of /load'ing file */
extern int loadstart;      /* line number of command start in /load'ing file */
extern TFILE *tfin;        /* tf input queue */
extern TFILE *tfout;       /* tf output queue */
extern TFILE *tferr;       /* tf error queue */
extern TFILE *tfkeyboard;  /* keyboard, where tfin usually points */
extern TFILE *tfscreen;    /* screen queue, where tfout & tferr usually point */
extern int    read_depth;  /* depth of user kb reads */
extern int    readsafe;    /* safe to to a user kb read? */

#define operror(str)  eprintf("%s: %s", str, strerror(errno))
#define oputa(aline)  tfputa(aline, tfout)
#define oputs(str)    tfputs(str, tfout)
#define eputs(str)    tfputs(str, tferr)
#define tfputc(c, file) fputc((c), (file)->u.fp)
#define tfflush(file) \
    ((file->type==TF_FILE || file->type==TF_PIPE) ? fflush((file)->u.fp) : 0)

extern void   NDECL(init_tfio);
extern char  *FDECL(tfname,(CONST char *name, CONST char *macname));
extern char  *FDECL(expand_filename,(CONST char *str));
extern TFILE *FDECL(tfopen,(CONST char *name, CONST char *mode));
extern int    FDECL(tfclose,(TFILE *file));
extern void   FDECL(tfputs,(CONST char *str, TFILE *file));
extern attr_t FDECL(tfputansi,(CONST char *str, TFILE *file, attr_t attrs));
extern int    FDECL(tfputp,(CONST char *str, TFILE *file));
extern void   FDECL(tfputa,(struct Aline *aline, TFILE *file));
extern void   FDECL(vSprintf,(struct String *buf, int flags,
                     CONST char *fmt, va_list ap));
extern void   VDECL(Sprintf,(struct String *buf, int flags,
                     CONST char *fmt, ...)) format_printf(3, 4);
extern void   VDECL(oprintf,(CONST char *fmt, ...)) format_printf(1, 2);
extern void   VDECL(tfprintf,(TFILE *file, CONST char *fmt, ...))
                     format_printf(2, 3);
extern void   FDECL(eprefix,(String *buffer));
extern void   VDECL(eprintf,(CONST char *fmt, ...)) format_printf(1, 2);
extern char   NDECL(igetchar);
extern int    FDECL(handle_tfopen_func,(CONST char *name, CONST char *mode));
extern TFILE *FDECL(find_tfile,(CONST char *handle));
extern TFILE *FDECL(find_usable_tfile,(CONST char *handle, int mode));
extern struct String *FDECL(tfgetS,(struct String *str, TFILE *file));

extern void   FDECL(flushout_queue,(struct Queue *queue, int quiet));

extern Aline *FDECL(dnew_aline,(CONST char *str, attr_t attrs, int len,
                    CONST char *file, int line));
extern void   FDECL(dfree_aline,(Aline *aline, CONST char *file, int line));

#define new_alinen(s,a,n)     dnew_aline((s), (a), (n), __FILE__, __LINE__)
#define new_aline(s,a)     dnew_aline((s), (a), strlen(s), __FILE__, __LINE__)
#define free_aline(a)      dfree_aline((a), __FILE__, __LINE__)

#endif /* TFIO_H */
