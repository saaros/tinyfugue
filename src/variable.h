/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef VARIABLE_H
#define VARIABLE_H

/*********************************************
 * Internal, user, and environment variables *
 *********************************************/

typedef struct Var {
    char *name;
    char *value;
    int flags;
    char **enumvec;                /* list of valid string values */
    int ival;                      /* integer value */
    Toggler *func;                 /* called when ival changes */
    struct ListEntry *node;        /* backpointer to node in List */
} Var;

extern void  NDECL(init_variables);
extern void  NDECL(init_values);
extern int   FDECL(enum2int,(char *str, char **vec, char *msg));
extern char *FDECL(getnearestvar,(char *name, int *np));
extern char *FDECL(getvar,(char *name));
extern char *FDECL(setnearestvar,(char *name, char *value));
extern char *FDECL(setlocalvar,(char *name, char *value));
extern char *FDECL(setvar,(char *name, char *value, int exportflag));
extern void  FDECL(setivar,(char *name, int value, int exportflag));
extern void  FDECL(listvar,(int exportflag));
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
    VAR_TIME_FORMAT ,
    VAR_VISUAL      ,
    VAR_WATCHDOG    ,
    VAR_WATCHNAME   ,
    VAR_WRAPFLAG    ,      /* user name is "wrap" */
    VAR_WRAPLOG     ,
    VAR_WRAPSIZE    ,
    VAR_WRAPSPACE   ,
    NUM_VARS               /* not a Var, but a count */
};

/* Convenient variable access.
 * These macros are READ ONLY.  Use setvar() to change a value.
 */
#define MAIL		(special_var[ VAR_MAIL		].value)
#define TERM		(special_var[ VAR_TERM		].value)
#define TFHELP		(special_var[ VAR_TFHELP	].value)
#define TFLIBDIR	(special_var[ VAR_TFLIBDIR	].value)
#define always_echo	(special_var[ VAR_ALWAYS_ECHO	].ival)
#define background	(special_var[ VAR_BACKGROUND	].ival)
#define backslash	(special_var[ VAR_BACKSLASH	].ival)
#define bamf		(special_var[ VAR_BAMF		].ival)
#define beep		(special_var[ VAR_BEEP		].ival)
#define bg_output	(special_var[ VAR_BG_OUTPUT	].ival)
#define borg		(special_var[ VAR_BORG		].ival)
#define catch_ctrls	(special_var[ VAR_CATCH_CTRLS	].ival)
#define cleardone	(special_var[ VAR_CLEARDONE	].ival)
#define clearfull	(special_var[ VAR_CLEARFULL	].ival)
#define clock_flag	(special_var[ VAR_CLOCK		].ival)
#define gag		(special_var[ VAR_GAG		].ival)
#define gpri		(special_var[ VAR_GPRI		].ival)
#define hilite		(special_var[ VAR_HILITE	].ival)
#define hiliteattr	(special_var[ VAR_HILITEATTR	].ival)
#define hookflag	(special_var[ VAR_HOOKFLAG	].ival)
#define hpri		(special_var[ VAR_HPRI		].ival)
#define ignore_sigquit	(special_var[ VAR_IGNORE_SIGQUIT].ival)
#define insert		(special_var[ VAR_INSERT	].ival)
#define isize		(special_var[ VAR_ISIZE		].ival)
#define kecho		(special_var[ VAR_KECHO		].ival)
#define kprefix		(special_var[ VAR_KPREFIX	].value)
#define login		(special_var[ VAR_LOGIN		].ival)
#define lpflag		(special_var[ VAR_LPFLAG	].ival)
#define lpquote		(special_var[ VAR_LPQUOTE	].ival)
#define maildelay	(special_var[ VAR_MAILDELAY	].ival)
#define matching	(special_var[ VAR_MATCHING	].ival)
#define max_recur	(special_var[ VAR_MAX_RECUR	].ival)
#define mecho		(special_var[ VAR_MECHO		].ival)
#define more		(special_var[ VAR_MORE		].ival)
#define mprefix		(special_var[ VAR_MPREFIX	].value)
#define oldslash	(special_var[ VAR_OLDSLASH	].ival)
#define prompt_sec	(special_var[ VAR_PROMPT_SEC	].ival)
#define prompt_usec	(special_var[ VAR_PROMPT_USEC	].ival)
#define process_time	(special_var[ VAR_PROCESS_TIME	].ival)
#define qecho		(special_var[ VAR_QECHO		].ival)
#define qprefix		(special_var[ VAR_QPREFIX	].value)
#define quiet		(special_var[ VAR_QUIET		].ival)
#define quitdone	(special_var[ VAR_QUITDONE	].ival)
#define quoted_args	(special_var[ VAR_QUOTED_ARGS	].ival)
#define redef		(special_var[ VAR_REDEF		].ival)
#define refreshtime	(special_var[ VAR_REFRESHTIME	].ival)
#define scroll		(special_var[ VAR_SCROLL	].ival)
#define shpause		(special_var[ VAR_SHPAUSE	].ival)
#define snarf		(special_var[ VAR_SNARF		].ival)
#define sockmload	(special_var[ VAR_SOCKMLOAD	].ival)
#define sub		(special_var[ VAR_SUB		].ival)
#define time_format	(special_var[ VAR_TIME_FORMAT	].value)
#define visual		(special_var[ VAR_VISUAL	].ival)
#define watchdog	(special_var[ VAR_WATCHDOG	].ival)
#define watchname	(special_var[ VAR_WATCHNAME	].ival)
#define wrapflag	(special_var[ VAR_WRAPFLAG	].ival)
#define wraplog		(special_var[ VAR_WRAPLOG	].ival)
#define wrapsize	(special_var[ VAR_WRAPSIZE	].ival)
#define wrapspace	(special_var[ VAR_WRAPSPACE	].ival)

extern Var special_var[];

#endif /* VARIABLE_H */
