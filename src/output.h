/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.h,v 35004.13 1997/03/27 01:04:38 hawkeye Exp $ */

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
#define STAT_READ	0004
#define STAT_ACTIVE	0010
#define STAT_LOGGING	0020
#define STAT_MAIL	0040
#define STAT_INSERT	0100
#define STAT_CLOCK	0200
#define STAT_BAR	0400   /* parts of bar not covered by others */
#define STAT_ALL	0777   /* the entire status bar */

/* refresh types */
#define REF_PHYSICAL  1
#define REF_PROMPT    2
#define REF_LOGICAL   3

#define set_refresh_pending(type)	(need_refresh |= (type))
#define moresize			(paused ? tfscreen_size + 1 : 0)

extern unsigned int tfscreen_size;
extern int paused;
extern int lines, columns;

extern void FDECL(bell,(int n));
extern void NDECL(init_tfscreen);
extern void NDECL(init_output);
extern void NDECL(change_term);
extern void FDECL(setup_screen,(int clearlines));
extern int  NDECL(redraw);
extern void NDECL(oflush);
extern void NDECL(tog_more);
extern void NDECL(ch_visual);
extern void NDECL(tog_insert);
extern void FDECL(status_bar,(int seg));
extern void NDECL(fix_screen);
extern void NDECL(panic_fix_screen);
extern void FDECL(iput,(int len));
extern void NDECL(inewline);
extern void FDECL(idel,(int place));
extern int  FDECL(igoto,(int place));
extern int  NDECL(dokey_page);
extern int  NDECL(dokey_hpage);
extern int  NDECL(dokey_line);
extern int  FDECL(screen_flush,(int selective));
extern void NDECL(do_refresh);
extern void NDECL(logical_refresh);
extern void NDECL(physical_refresh);
extern void NDECL(reset_outcount);
extern void FDECL(screenout,(Aline *aline));
extern void FDECL(update_prompt,(Aline *newprompt));
extern int  FDECL(wraplen,(CONST char *str, int len, int indent));
extern void NDECL(tog_clock);
extern void NDECL(ch_hilite);
extern attr_t      FDECL(handle_ansi_attr,(Aline *aline, attr_t attrs));
extern CONST char *FDECL(get_keycode,(CONST char *name));

#endif /* OUTPUT_H */
