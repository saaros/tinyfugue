/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1998 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: expand.h,v 35004.16 1998/03/11 23:12:19 hawkeye Exp $ */

#ifndef EXPAND_H
#define EXPAND_H

/* note: these numbers must agree with enum_subs[] in variable.c. */
#define SUB_LITERAL -1  /* send literally (no /command interpretation, even) */
#define SUB_KEYWORD  0  /* SUB_NEWLINE if initial keyword, else no subs      */
#define SUB_NEWLINE  1  /* %; subs and command execution                     */
#define SUB_FULL     2  /* all subs and command execution                    */
#define SUB_MACRO    3  /* all subs and command execution, from macro        */


extern void NDECL(init_expand);
extern int  FDECL(process_macro,(CONST char *body, CONST char *args, int subs));
extern String *NDECL(do_mprefix);
extern CONST char **FDECL(keyword,(CONST char *id));


#ifdef DMALLOC
extern void   NDECL(free_expand);
#endif

extern struct Value *user_result;
extern CONST char *current_command;
extern int recur_count, breaking;

#endif /* EXPAND_H */
