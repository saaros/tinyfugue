/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: tfio.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef TFIO_H
#define TFIO_H

#ifdef HAVE_STDARG
# include <stdarg.h>
#else
# include <varargs.h>
#endif

#include "dstring.h"

/* TFILE types */
#define TF_NULL     0
#define TF_QUEUE    1
#define TF_FILE     2
#define TF_PIPE     3

/* Sprintf flags */
#define SP_APPEND   1

typedef struct TFILE {
    int type;
    char *name;
    union {
        struct Queue *queue;
        FILE *fp;
    } u;
} TFILE;                       /* TF's analogue of stdio's FILE */

extern TFILE *tfout;           /* ... stdout */
extern TFILE *tferr;           /* ... stderr */

#define operror(str)  tfprintf(tferr, "%% %s: %s", str, STRERROR(errno))
#define oputs(str)    tfputs(str, tfout)
#define oputa(aline)  tfputa(aline, tfout)
#define tfputc(c, file) fputc((c), (file)->u.fp)
#define tfflush(file) fflush((file)->u.fp)           /* undefined for QUEUEs */
#define cmderror(msg) tfprintf(tferr, "%S: %s", error_prefix(), (msg))

extern String *NDECL(error_prefix);
extern char   *FDECL(tfname,(char *name, char *macname));
extern String *FDECL(expand_filename,(Stringp str));
extern TFILE  *FDECL(tfopen,(char *name, char *mode));
extern int     FDECL(tfjump,(TFILE *file, long offset));
extern int     FDECL(tfclose,(TFILE *file));
extern void    FDECL(tfputs,(char *str, TFILE *file));
extern void    FDECL(tfputa,(Aline *aline, TFILE *file));
extern void    FDECL(vSprintf,(Stringp buf, int flags, char *fmt, va_list ap));
extern void    VDECL(Sprintf,(Stringp buf, int flags, char *fmt, ...));
extern void    VDECL(oprintf,(char *fmt, ...));
extern void    VDECL(tfprintf,(TFILE *file, char *fmt, ...));
extern char    NDECL(igetchar);
extern String *FDECL(tfgetS,(Stringp str, TFILE *file));

#endif /* TFIO_H */
