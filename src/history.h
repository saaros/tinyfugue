/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: history.h,v 33000.1 1994/04/26 08:56:29 hawkeye Exp $ */

#ifndef HISTORY_H
#define HISTORY_H

# ifndef NO_HISTORY

typedef struct History {       /* circular list of Alines, and logfile */
    struct Aline **alines;
    int size, maxsize, pos, index, num;
    TFILE *logfile;
    char *logname;
} History;

extern void   NDECL(init_histories);
extern void   FDECL(init_history,(History *hist, int maxsize));
extern void   FDECL(free_history,(History *hist));
extern void   FDECL(record_hist,(History *hist, Aline *aline));
extern void   FDECL(record_global,(Aline *aline));
extern void   FDECL(record_local,(Aline *aline));
extern void   FDECL(record_input,(char *line));
extern int    FDECL(recall_history,(char *args, TFILE *file));
extern int    FDECL(recall_input,(int dir, int searchflag));
extern int    FDECL(is_suppressed,(char *line));
extern int    FDECL(history_sub,(char *pattern));

# else /* NO_HISTORY */

#define init_histories()               /* do nothing */
#define free_history(hist)             /* do nothing */
#define record_hist(hist, aline)       /* do nothing */
#define record_global(aline)           /* do nothing */
#define record_local(aline)            /* do nothing */
#define record_input(line)             /* do nothing */
#define recall_history(args, file)     cmderror("history disabled")
#define recall_input(dir, searchflag)  cmderror("history disabled")
#define is_suppressed(line)            (0)
#define history_sub(pattern)           /* do nothing */

# endif /* NO_HISTORY */

#endif /* HISTORY_H */
