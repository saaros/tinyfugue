/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: macro.h,v 35004.47 2003/12/10 09:12:02 hawkeye Exp $ */

#ifndef MACRO_H
#define MACRO_H

enum { USED_NAME, USED_TRIG, USED_HOOK, USED_KEY, USED_N }; /* for Macro.used */

typedef struct {
    cattr_t attr;
    short subexp;
} subattr_t;

typedef struct Macro {
    const char *name;
    struct ListEntry *numnode;		/* node in maclist */
    struct ListEntry *hashnode;		/* node in macro_table hash bucket */
    struct ListEntry *trignode;		/* node in one of the triglists */
    struct Macro *tnext;		/* temp list ptr for collision/death */
    String *body, *expr;
    Program *prog, *exprprog;		/* compiled body, expr */
    const char *bind, *keyname;
    Pattern trig, hargs, wtype;		/* trigger/hook/worldtype patterns */
    hookvec_t hook;			/* bit vector */
    struct World *world;		/* only trig on text from world */
    int pri, num;
    attr_t attr;
    int nsubattr;
    subattr_t *subattr;
    short prob, shots, invis;
    short flags;
    signed char fallthru, quiet;
    struct BuiltinCmd *builtin;		/* builtin cmd with same name, if any */
    int used[USED_N];			/* number of calls by each method */
} Macro;

extern int invis_flag;

extern void   init_macros(void);
extern char  *parse_attrs(const char *str, attr_t *attrp, int delimiter);
extern int    hookname2int(const char *name);
extern Macro *find_macro(const char *name);
extern Macro *find_num_macro(int num); 
extern Macro *new_macro(const char *trig, const char *binding,
                 const hookvec_t *hook, const char *hargs, const char *body,
                 int pri, int prob, attr_t attr, int invis, int mflag);
extern int    add_macro(struct Macro *macro);
extern int    add_hook(char *name, const char *body);
extern int    remove_macro_by_name(const char *name);
extern void   nuke_dead_macros(void);
extern void   kill_macro(struct Macro *macro);
extern void   rebind_key_macros(void);
extern void   remove_world_macros(struct World *w);
extern int    save_macros(String *args, int offset);
extern int    do_macro(Macro *macro, String *args, int offset,
		int used_type, String *kbnumlocal);
extern const char *macro_body(const char *name);
extern int    find_and_run_matches(String *text, int hooknum, String **linep,
		struct World *world, int globalflag, int exec_list_long);

#if USE_DMALLOC
extern void   free_macros(void);
#endif

#define add_ibind(key, cmd) \
    add_macro(new_macro(NULL, key, NULL, NULL, cmd, 0, 100, 0, TRUE, 0))

#endif /* MACRO_H */
