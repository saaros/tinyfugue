/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003, 2004, 2005 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.h,v 35004.38 2005/04/18 03:15:36 kkeys Exp $ */

#ifndef VARIABLE_H
#define VARIABLE_H

/*********************************************
 * Internal, user, and environment variables *
 *********************************************/

#define set_var_by_name(name, sval) \
    setvar(NULL, name, hash_string(name), TYPE_STR, sval, 0)
#define set_var_by_id(id, i) \
    do { \
        long ival = i; \
        setexistingvar(&special_var[id], TYPE_INT, &ival, FALSE); \
    } while (0)

extern Pattern looks_like_special_sub_ic;

extern void init_variables(void);
extern Var   *hfindnearestvar(const Value *idval);
extern Value *hgetnearestvarval(const Value *idval);
extern Value *getvarval(Var *var);
extern const char *getvar(const char *name);
extern Var *ffindglobalvar(const char *name);
extern void set_var_direct(Var *var, int type, void *value);
extern Var *hsetnearestvar(const Value *idval, int type, void *value);
extern Var *setvar(Var *var, const char *name, unsigned int hash, int type,
    void *value, int exportflag);
extern Var *setnewvar(const char *name, unsigned int hash, int type,
    void *value, int exportflag);
extern Var *setexistingvar(Var *var, int type, void *value, int exportflag);
extern int  unsetvar(Var *var);
extern void freevar(Var *var);
extern char *spanvar(const char *start);
extern int  setdelim(const char **pp);
extern int  do_set(const char *name, unsigned int hash, conString *value,
	    int offset, int exportflag, int localflag);
extern int  command_set(String *args, int offset, int exportflag,
	    int localflag);
extern Var *setlocalvar(const char *name, int type, void *value);
extern void pushvarscope(struct List *level);
extern void popvarscope(void);

#if USE_DMALLOC
extern void   free_vars(void);
#endif

#endif /* VARIABLE_H */
