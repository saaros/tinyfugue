/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003, 2004 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: variable.h,v 35004.31 2004/02/17 06:44:44 hawkeye Exp $ */

#ifndef VARIABLE_H
#define VARIABLE_H

/*********************************************
 * Internal, user, and environment variables *
 *********************************************/

#define set_var_by_name(name, sval, export) \
    setvar(NULL, name, hash_string(name), TYPE_STR, sval, export)
#define set_var_by_id(id, i) \
    do { \
        long ival = i; \
        setvar(&special_var[id], NULL, 0, TYPE_INT, &ival, FALSE); \
    } while (0)


extern void init_variables(void);
extern Var   *hfindnearestvar(const char *name, unsigned int hash);
#define findnearestvar(name)	hfindnearestvar(name, hash_string(name))
extern Value *hgetnearestvarval(const char *name, unsigned int hash);
#define getnearestvarval(name)	hgetnearestvarval(name, hash_string(name))
#if 0 /* not used */
extern Value *getglobalvarval(const char *name);
extern const char *getnearestvarchar(const char *name);
#endif
extern Value *getvarval(Var *var);
extern const char *getvar(const char *name);
extern Var *ffindglobalvar(const char *name);
extern Var *hsetnearestvar(const char *name, unsigned int hash, int type,
	void *value);
#define setnearestvar(name, type, value) \
	hsetnearestvar(name, hash_string(name), type, value)
extern Var *setvar(Var *var, const char *name, unsigned int hash, int type,
                          void *value, int exportflag);
extern int  unsetvar(Var *var);
extern void freevar(Var *var);
extern char *spanvar(const char *start);
extern char *spansetspace(const char *p);
extern int  do_set(const char *name, unsigned int hash, String *value,
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
