/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: globals.h,v 35000.18 1999/01/31 00:27:43 hawkeye Exp $ */

#ifndef GLOBALS_H
#define GLOBALS_H

/*************************
 * Global user variables *
 *************************/

typedef int NDECL((Toggler));

typedef struct Var {
    CONST char *name;
    CONST char *value;
    int len;
    int flags;
    CONST char **enumvec;	/* list of valid string values */
    long ival;			/* integer value */
    Toggler *func;		/* called when ival changes */
    struct ListEntry *node;	/* backpointer to node in list */
    GENERIC *status;		/* status line field to update on change */
} Var;


enum Vars {
#define varcode(id, name, val, type, enums, ival, func)      id
#include "varlist.h"
#undef varcode
};


/* Convenient variable access.
 * The get* macros are READONLY.  Use setvar() to change a value.  The cast
 * enforces the readonly-ness in standard C (gcc needs -pedantic to warn).
 */

#define intvar(id)	(special_var[(id)].ival)
#define strvar(id)	(special_var[(id)].value)

#define getintvar(id)	((long) intvar(id))
#define getstrvar(id)	((char*)strvar(id))

#define MAIL		getstrvar(VAR_MAIL)
#define TERM		getstrvar(VAR_TERM)
#define TFLIBDIR	getstrvar(VAR_TFLIBDIR)
#define TFPATH		getstrvar(VAR_TFPATH)
#define TFMAILPATH	getstrvar(VAR_TFMAILPATH)
#define auto_fg		getintvar(VAR_auto_fg)
#define background	getintvar(VAR_background)
#define backslash	getintvar(VAR_backslash)
#define bamf		getintvar(VAR_bamf)
#define beep		getintvar(VAR_beep)
#define bg_output	getintvar(VAR_bg_output)
#define binary_eol	getintvar(VAR_binary_eol)
#define borg		getintvar(VAR_borg)
#define cleardone	getintvar(VAR_cleardone)
#define clearfull	getintvar(VAR_clearfull)
#define clock_flag	getintvar(VAR_clock)
#define emulation 	getintvar(VAR_emulation)
#define gag		getintvar(VAR_gag)
#define async_name	getintvar(VAR_async_name)
#define async_conn	getintvar(VAR_async_conn)
#define gpri		getintvar(VAR_gpri)
#define hilite		getintvar(VAR_hilite)
#define histsize	getintvar(VAR_histsize)
#define hookflag	getintvar(VAR_hook)
#define hpri		getintvar(VAR_hpri)
#define insert		getintvar(VAR_insert)
#define isize		getintvar(VAR_isize)
#define istrip		getintvar(VAR_istrip)
#define kecho		getintvar(VAR_kecho)
#define kprefix		getstrvar(VAR_kprefix)
#define login		getintvar(VAR_login)
#define lpflag		getintvar(VAR_lp)
#define lpquote		getintvar(VAR_lpquote)
#define maildelay	getintvar(VAR_maildelay)
#define matching	getintvar(VAR_matching)
#define max_iter	getintvar(VAR_max_iter)
#define max_recur	getintvar(VAR_max_recur)
#define mecho		getintvar(VAR_mecho)
#define meta_esc	getintvar(VAR_meta_esc)
#define more		getintvar(VAR_more)
#define mprefix		getstrvar(VAR_mprefix)
#define oldslash	getintvar(VAR_oldslash)
#define pedantic	getintvar(VAR_pedantic)
#define prompt_sec	getintvar(VAR_prompt_sec)
#define prompt_usec	getintvar(VAR_prompt_usec)
#define proxy_host	getstrvar(VAR_proxy_host)
#define proxy_port	getstrvar(VAR_proxy_port)
#define process_time	getintvar(VAR_ptime)
#define qecho		getintvar(VAR_qecho)
#define qprefix		getstrvar(VAR_qprefix)
#define quietflag	getintvar(VAR_quiet)
#define quitdone	getintvar(VAR_quitdone)
#define redef		getintvar(VAR_redef)
#define refreshtime	getintvar(VAR_refreshtime)
#define scroll		getintvar(VAR_scroll)
#define shpause		getintvar(VAR_shpause)
#define snarf		getintvar(VAR_snarf)
#define sockmload	getintvar(VAR_sockmload)
#define status_fields	getstrvar(VAR_status_fields)
#define status_pad	getstrvar(VAR_status_pad)
#define sub		getintvar(VAR_sub)
#define tabsize		getintvar(VAR_tabsize)
#define telopt		getintvar(VAR_telopt)
#define time_format	getstrvar(VAR_time_format)
#define watchdog	getintvar(VAR_watchdog)
#define watchname	getintvar(VAR_watchname)
#define wordpunct	getstrvar(VAR_wordpunct)
#define wrapflag	getintvar(VAR_wrap)
#define wraplog		getintvar(VAR_wraplog)
#define wrapsize	getintvar(VAR_wrapsize)
#define wrapspace	getintvar(VAR_wrapspace)

/* visual is special: initial value of -1 indicates it was never explicitly
 * set, but is still treated like "off".
 */
#define visual		((long)(getintvar(VAR_visual) > 0))

extern Var special_var[];

#endif /* GLOBALS_H */
