/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.h,v 35004.27 1999/01/31 00:27:50 hawkeye Exp $ */

#ifndef OUTPUT_H
#define OUTPUT_H

#ifdef TERMCAP
# define SCREEN
#endif
#ifdef HARDCODE
# define SCREEN
#endif

/* refresh types */
#define REF_PHYSICAL  1
#define REF_PROMPT    2
#define REF_LOGICAL   3

#define set_refresh_pending(type)	(need_refresh |= (type))
#define moresize			(paused ? tfscreen_size + 1 : 0)

extern unsigned int tfscreen_size;
extern int paused;
extern int lines, columns;
extern int sockecho;
extern TIME_T clock_update;
extern int need_refresh;       /* Does input need refresh? */
extern int need_more_refresh;  /* Does visual more prompt need refresh? */

extern void FDECL(dobell,(int n));
extern void NDECL(init_output);
extern int  NDECL(change_term);
extern void FDECL(setup_screen,(int clearlines));
extern int  NDECL(redraw);
extern void NDECL(oflush);
extern int  NDECL(tog_more);
extern int  FDECL(clear_more,(int new));
extern int  NDECL(ch_visual);
extern int  NDECL(ch_status_fields);
extern void FDECL(update_status_field,(Var *var, int internal));
extern int  NDECL(update_status_line);
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
extern void FDECL(update_prompt,(Aline *newprompt, int display));
extern int  FDECL(wraplen,(CONST char *str, int len, int indent));
extern int  NDECL(ch_hiliteattr);
extern int  NDECL(ch_status_attr);
extern attr_t      FDECL(handle_inline_attr,(Aline *aline, attr_t attrs));
extern attr_t      FDECL(handle_ansi_attr,(Aline *aline, attr_t attrs));
extern CONST char *FDECL(get_keycode,(CONST char *name));

#ifdef DMALLOC
extern void   NDECL(free_output);
#endif

#endif /* OUTPUT_H */
