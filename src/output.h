/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: output.h,v 35004.58 2003/12/10 02:20:37 hawkeye Exp $ */

#ifndef OUTPUT_H
#define OUTPUT_H

#if TERMCAP
# define SCREEN
#endif
#if HARDCODE
# define SCREEN
#endif

/* refresh types */
typedef enum { REF_NONE, REF_PHYSICAL, REF_PROMPT, REF_LOGICAL } ref_type_t;

#define set_refresh_pending(type)	(need_refresh |= (type))
#define display_screen	(virtscreen ? fg_screen : default_screen)

extern int lines, columns;
extern struct timeval clock_update;	/* when to update clock */
extern ref_type_t need_refresh;		/* Input needs refresh? */
extern int need_more_refresh;		/* Visual more prompt needs refresh? */
extern struct timeval alert_timeout;	/* when to clear alert */
extern unsigned long alert_id;

extern void dobell(int n);
extern void init_output(void);
extern int  change_term(void);
extern void setup_screen(void);
extern int  winlines(void);
extern int  redraw(void);
extern int  redraw_window(Screen *screen, int already_clear);
extern int  clear_display_screen(void);
extern void oflush(void);
extern int  tog_more(void);
extern int  tog_keypad(void);
extern int  clear_more(int new);
extern int  pause_screen(void);
extern int  ch_visual(void);
extern int  ch_wrap(void);
extern int  ch_status_int(void);
extern int  ch_status_fields(void);
extern void update_status_field(Var *var, stat_id_t internal);
extern void format_status_line(void);
extern int  display_status_line(void);
extern int  update_status_line(void);
extern int handle_status_width_func(const char *name);
extern void fix_screen(void);
extern void minimal_fix_screen(void);
extern void iput(int len);
extern void inewline(void);
extern void idel(int place);
extern int  igoto(int place);
extern int  dokey_page(void);
extern int  dokey_hpage(void);
extern int  dokey_line(void);
extern int  screen_end(int need_redraw);
extern int  selflush(void);
extern void do_refresh(void);
extern void logical_refresh(void);
extern void physical_refresh(void);
extern void reset_outcount(void);
extern void enscreen(Screen *screen, String *line);
extern void screenout(String *line);
extern void update_prompt(String *newprompt, int display);
extern int  wraplen(const char *str, int len, int indent);
extern int  ch_hiliteattr(void);
extern int  ch_status_attr(void);
extern int  ch_alert_attr(void);
extern String *decode_attr(const String *src, attr_t attrs);
extern String *decode_ansi(const char *src, attr_t attrs, int emul,
    attr_t *final_attr);
extern const char *get_keycode(const char *name);

extern int moresize(Screen *screen);
extern int screen_has_filter(struct Screen *screen);
extern void clear_screen_filter(struct Screen *screen);
extern int enable_screen_filter(struct Screen *screen);
extern void set_screen_filter(struct Screen *screen, Pattern *pat,
    attr_t attr_flag, int sense);
extern void alert(String *msg);
extern void clear_alert(void);


#if USE_DMALLOC
extern void   free_output(void);
#endif

#endif /* OUTPUT_H */
