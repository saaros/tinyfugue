/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.h,v 35004.12 1997/08/26 07:16:34 hawkeye Exp $ */

#ifndef VARIABLE_H
#define VARIABLE_H

/*********************************************
 * Internal, user, and environment variables *
 *********************************************/

extern void NDECL(init_variables);
extern CONST char *FDECL(getnearestvar,(CONST char *name, long *ival));
extern CONST char *FDECL(getvar,(CONST char *name));
extern Var *FDECL(ffindglobalvar,(CONST char *name));
extern Var *FDECL(setnearestvar,(CONST char *name, CONST char *value));
extern Var *FDECL(setvar,(CONST char *name, CONST char *value, int exportflag));
extern void FDECL(setivar,(CONST char *name, long value, int exportflag));
extern int  FDECL(do_set,(char *args, int exportflag, int localflag));
extern Var *FDECL(newlocalvar,(CONST char *name, CONST char *value));
extern void FDECL(newvarscope,(struct List *level));
extern void NDECL(nukevarscope);

#endif /* VARIABLE_H */
