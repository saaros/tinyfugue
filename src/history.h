/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: history.h,v 35004.12 1999/01/31 00:27:45 hawkeye Exp $ */

#ifndef HISTORY_H
#define HISTORY_H

# ifndef NO_HISTORY

extern void   NDECL(init_histories);
extern struct History *FDECL(init_history,(struct History *hist, int maxsize));
extern void   FDECL(free_history,(struct History *hist));
extern void   FDECL(recordline,(struct History *hist, Aline *aline));
extern void   FDECL(record_input,(CONST char *line, struct timeval *tv));
extern Aline *FDECL(recall_input,(int dir, int searchflag));
extern int    FDECL(is_watchdog,(struct History *hist, Aline *aline));
extern int    FDECL(is_watchname,(struct History *hist, Aline *aline));
extern String*FDECL(history_sub,(CONST char *pattern));
extern void   NDECL(sync_input_hist);
extern int    FDECL(do_recall,(char *args));

#ifdef DMALLOC
extern void   NDECL(free_histories);
#endif

#define record_global(aline)  recordline(globalhist, (aline))
#define record_local(aline)   recordline(localhist, (aline))

extern struct History globalhist[], localhist[];
extern int log_count, norecord, nolog;

# else /* NO_HISTORY */

#define init_histories()               /* do nothing */
#define free_history(hist)             /* do nothing */
#define recordline(hist, aline)        /* do nothing */
#define record_global(line)            /* do nothing */
#define record_local(line)             /* do nothing */
#define record_input(line, tv)         /* do nothing */
#define recall_history(args, file)     (eprintf("history disabled"), 0)
#define recall_input(dir, searchflag)  (eprintf("history disabled"), 0)
#define check_watch(hist, aline)       /* do nothing */
#define history_sub(pattern)           (0)
#define is_watchdog(hist, aline)       (0)
#define is_watchname(hist, aline)      (0)

#define log_count                      (0)
static int norecord = 0, nolog = 0;

# endif /* NO_HISTORY */

#endif /* HISTORY_H */
