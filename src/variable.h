/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.h,v 35004.16 1999/01/31 00:27:58 hawkeye Exp $ */

#ifndef VARIABLE_H
#define VARIABLE_H

/*********************************************
 * Internal, user, and environment variables *
 *********************************************/

#define set_var_by_name(name, value, export) \
    setvar(NULL, name, 0, value, export)
#define set_var_by_id(id, ivalue, value) \
    setvar(&special_var[id], NULL, ivalue, value, FALSE)


extern void NDECL(init_variables);
extern CONST char *FDECL(getnearestvar,(CONST char *name, long *ival));
extern CONST char *FDECL(getvar,(CONST char *name));
extern Var *FDECL(ffindglobalvar,(CONST char *name));
extern Var *FDECL(setnearestvar,(CONST char *name, CONST char *value));
extern Var *FDECL(setvar,(Var *var, CONST char *name, long ivalue,
                          CONST char *value, int exportflag));
extern void FDECL(setivar,(CONST char *name, long value, int exportflag));
extern int  FDECL(do_set,(char *args, int exportflag, int localflag));
extern Var *FDECL(newlocalvar,(CONST char *name, CONST char *value));
extern void FDECL(newvarscope,(struct List *level));
extern void NDECL(nukevarscope);

#ifdef DMALLOC
extern void   NDECL(free_vars);
#endif

#endif /* VARIABLE_H */
