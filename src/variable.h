/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.h,v 33000.2 1994/04/22 06:05:07 hawkeye Exp $ */

#ifndef VARIABLE_H
#define VARIABLE_H

/*********************************************
 * Internal, user, and environment variables *
 *********************************************/

typedef struct Var {
    char *name;
    char *value;
    int flags;
    char **enumvec;		/* list of valid string values */
    int ival;			/* integer value */
    Toggler *func;		/* called when ival changes */
    struct ListEntry *node;	/* backpointer to node in list */
} Var;

extern void  NDECL(init_variables);
extern int   FDECL(enum2int,(char *str, char **vec, char *msg));
extern char *FDECL(getnearestvar,(char *name, int *np));
extern char *FDECL(getvar,(char *name));
extern char *FDECL(setnearestvar,(char *name, char *value));
extern char *FDECL(setvar,(char *name, char *value, int exportflag));
extern void  FDECL(setivar,(char *name, int value, int exportflag));
extern int   FDECL(do_set,(char *args, int exportflag, int localflag));
extern void  NDECL(newvarscope);
extern void  NDECL(nukevarscope);

/* these must be in same order as special_var (sorted by user name) */
enum Vars {
    VAR_MAIL        ,      /* All caps. */
    VAR_TERM        ,      /* All caps. */
    VAR_TFHELP      ,      /* All caps. */
    VAR_TFLIBDIR    ,      /* All caps. */
    VAR_ALWAYS_ECHO ,
    VAR_BACKGROUND  ,
    VAR_BACKSLASH   ,
    VAR_BAMF        ,
    VAR_BEEP        ,
    VAR_BG_OUTPUT   ,
    VAR_BORG        ,
    VAR_CATCH_CTRLS ,
    VAR_CLEARDONE   ,
    VAR_CLEARFULL   ,
    VAR_CLOCK       ,
    VAR_GAG         ,
    VAR_GPRI        ,
    VAR_HILITE      ,
    VAR_HILITEATTR  ,
    VAR_HOOKFLAG    ,      /* user name is "hook" */
    VAR_HPRI        ,
    VAR_IGNORE_SIGQUIT ,
    VAR_INSERT      ,
    VAR_ISIZE       ,
    VAR_KECHO       ,
    VAR_KPREFIX     ,
    VAR_LOGIN       ,
    VAR_LPFLAG      ,      /* user name is "lp" */
    VAR_LPQUOTE     ,
    VAR_MAILDELAY   ,
    VAR_MATCHING    ,
    VAR_MAX_ITER    ,
    VAR_MAX_RECUR   ,
    VAR_MECHO       ,
    VAR_MORE        ,
    VAR_MPREFIX     ,
    VAR_OLDSLASH    ,
    VAR_PROMPT_SEC  ,
    VAR_PROMPT_USEC ,
    VAR_PROCESS_TIME,      /* user name is "ptime" */
    VAR_QECHO       ,
    VAR_QPREFIX     ,
    VAR_QUIET       ,
    VAR_QUITDONE    ,
    VAR_QUOTED_ARGS ,
    VAR_REDEF       ,
    VAR_REFRESHTIME ,
    VAR_SCROLL      ,
    VAR_SHPAUSE     ,
    VAR_SNARF       ,
    VAR_SOCKMLOAD   ,
    VAR_SUB         ,
    VAR_TELOPT      ,
    VAR_TIME_FORMAT ,
    VAR_VISUAL      ,
    VAR_WATCHDOG    ,
    VAR_WATCHNAME   ,
    VAR_WORDPUNCT   ,
    VAR_WRAPFLAG    ,      /* user name is "wrap" */
    VAR_WRAPLOG     ,
    VAR_WRAPSIZE    ,
    VAR_WRAPSPACE   ,
    NUM_VARS               /* not a Var, but a count */
};

/* Convenient variable access.
 * These macros are READONLY.  Use setvar() to change a value.
 * The cast enforces the readonly-ness in ANSI C (gcc needs -pedantic).
 */
#define MAIL		((char*)special_var[ VAR_MAIL		].value)
#define TERM		((char*)special_var[ VAR_TERM		].value)
#define TFHELP		((char*)special_var[ VAR_TFHELP		].value)
#define TFLIBDIR	((char*)special_var[ VAR_TFLIBDIR	].value)
#define always_echo	((int)	special_var[ VAR_ALWAYS_ECHO	].ival)
#define background	((int)	special_var[ VAR_BACKGROUND	].ival)
#define backslash	((int)	special_var[ VAR_BACKSLASH	].ival)
#define bamf		((int)	special_var[ VAR_BAMF		].ival)
#define beep		((int)	special_var[ VAR_BEEP		].ival)
#define bg_output	((int)	special_var[ VAR_BG_OUTPUT	].ival)
#define borg		((int)	special_var[ VAR_BORG		].ival)
#define catch_ctrls	((int)	special_var[ VAR_CATCH_CTRLS	].ival)
#define cleardone	((int)	special_var[ VAR_CLEARDONE	].ival)
#define clearfull	((int)	special_var[ VAR_CLEARFULL	].ival)
#define clock_flag	((int)	special_var[ VAR_CLOCK		].ival)
#define gag		((int)	special_var[ VAR_GAG		].ival)
#define gpri		((int)	special_var[ VAR_GPRI		].ival)
#define hilite		((int)	special_var[ VAR_HILITE		].ival)
#define hiliteattr	((int)	special_var[ VAR_HILITEATTR	].ival)
#define hookflag	((int)	special_var[ VAR_HOOKFLAG	].ival)
#define hpri		((int)	special_var[ VAR_HPRI		].ival)
#define ignore_sigquit	((int)	special_var[ VAR_IGNORE_SIGQUIT	].ival)
#define insert		((int)	special_var[ VAR_INSERT		].ival)
#define isize		((int)	special_var[ VAR_ISIZE		].ival)
#define kecho		((int)	special_var[ VAR_KECHO		].ival)
#define kprefix		((char*)special_var[ VAR_KPREFIX	].value)
#define login		((int)	special_var[ VAR_LOGIN		].ival)
#define lpflag		((int)	special_var[ VAR_LPFLAG		].ival)
#define lpquote		((int)	special_var[ VAR_LPQUOTE	].ival)
#define maildelay	((int)	special_var[ VAR_MAILDELAY	].ival)
#define matching	((int)	special_var[ VAR_MATCHING	].ival)
#define max_iter	((int)	special_var[ VAR_MAX_ITER	].ival)
#define max_recur	((int)	special_var[ VAR_MAX_RECUR	].ival)
#define mecho		((int)	special_var[ VAR_MECHO		].ival)
#define more		((int)	special_var[ VAR_MORE		].ival)
#define mprefix		((char*)special_var[ VAR_MPREFIX	].value)
#define oldslash	((int)	special_var[ VAR_OLDSLASH	].ival)
#define prompt_sec	((int)	special_var[ VAR_PROMPT_SEC	].ival)
#define prompt_usec	((int)	special_var[ VAR_PROMPT_USEC	].ival)
#define process_time	((int)	special_var[ VAR_PROCESS_TIME	].ival)
#define qecho		((int)	special_var[ VAR_QECHO		].ival)
#define qprefix		((char*)special_var[ VAR_QPREFIX	].value)
#define quiet		((int)	special_var[ VAR_QUIET		].ival)
#define quitdone	((int)	special_var[ VAR_QUITDONE	].ival)
#define quoted_args	((int)	special_var[ VAR_QUOTED_ARGS	].ival)
#define redef		((int)	special_var[ VAR_REDEF		].ival)
#define refreshtime	((int)	special_var[ VAR_REFRESHTIME	].ival)
#define scroll		((int)	special_var[ VAR_SCROLL		].ival)
#define shpause		((int)	special_var[ VAR_SHPAUSE	].ival)
#define snarf		((int)	special_var[ VAR_SNARF		].ival)
#define sockmload	((int)	special_var[ VAR_SOCKMLOAD	].ival)
#define sub		((int)	special_var[ VAR_SUB		].ival)
#define telopt		((int)  special_var[ VAR_TELOPT		].ival)
#define time_format	((char*)special_var[ VAR_TIME_FORMAT	].value)
#define visual		((int)	special_var[ VAR_VISUAL		].ival)
#define watchdog	((int)	special_var[ VAR_WATCHDOG	].ival)
#define watchname	((int)	special_var[ VAR_WATCHNAME	].ival)
#define wordpunct	((char*)special_var[ VAR_WORDPUNCT	].value)
#define wrapflag	((int)	special_var[ VAR_WRAPFLAG	].ival)
#define wraplog		((int)	special_var[ VAR_WRAPLOG	].ival)
#define wrapsize	((int)	special_var[ VAR_WRAPSIZE	].ival)
#define wrapspace	((int)	special_var[ VAR_WRAPSPACE	].ival)

extern Var special_var[];

#endif /* VARIABLE_H */
