/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef OUTPUT_H
#define OUTPUT_H

#ifdef TERMCAP
# define SCREEN
#endif
#ifdef HARDCODE
# define SCREEN
#endif

/* status_bar segments */
#define STAT_MORE	0001
#define STAT_WORLD	0002
#define STAT_CLOCK	0004
#define STAT_ACTIVE	0010
#define STAT_LOGGING	0020
#define STAT_MAIL	0040
#define STAT_INSERT	0100
#define STAT_BAR	0200   /* parts of bar not covered by others */
#define STAT_ALL	0377   /* the entire status bar */

/* refresh types */
#define REF_PHYSICAL  1
#define REF_LOGICAL   2

extern void NDECL(clr);
extern void FDECL(bell,(int n));
extern void NDECL(init_output);
extern void NDECL(change_term);
extern void NDECL(setup_screen);
extern int  NDECL(redraw);
extern void NDECL(oflush);
extern void NDECL(tog_more);
extern void NDECL(tog_visual);
extern void NDECL(tog_insert);
extern void NDECL(change_isize);
extern void FDECL(status_bar,(int seg));
extern void NDECL(fix_screen);
extern void NDECL(clear_input_line);
extern void FDECL(iputs,(char *s));
extern void NDECL(inewline);
extern void FDECL(idel,(int place));
extern int  FDECL(newpos,(int place));
extern int  NDECL(dokey_refresh);
extern int  NDECL(dokey_page);
extern int  NDECL(dokey_hpage);
extern int  NDECL(dokey_line);
extern int  NDECL(dokey_flush);
extern void NDECL(logical_refresh);
extern void NDECL(physical_refresh);
extern void NDECL(reset_outcount);
extern void FDECL(globalout,(Aline *aline));
extern void FDECL(screenout,(Aline *aline));
extern int  NDECL(getwrap);
extern void FDECL(refresh_prompt,(String *newprompt));
extern char *FDECL(wrap,(char **ptr, unsigned int *lenp, int *firstp));
extern void NDECL(ch_clock);
extern void NDECL(ch_hilite);

#endif /* OUTPUT_H */
