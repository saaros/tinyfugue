/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: macro.h,v 35004.21 1999/01/31 00:27:47 hawkeye Exp $ */

#ifndef MACRO_H
#define MACRO_H

typedef struct Macro {
    CONST char *name;
    struct ListEntry *numnode;		/* node in list by number */
    struct ListEntry *trignode;		/* node in list by priority */
    struct ListEntry *hooknode;		/* node in list by hook */
    struct ListEntry *hashnode;		/* node in list in hash bucket */
    struct Macro *tnext;		/* temp list ptr for collision/death */
    CONST char *bind, *keyname, *body, *expr;
    Pattern trig, hargs, wtype;		/* trigger/hook/worldtype patterns */
    long hook;				/* bit vector, at least 32 bits */
    struct World *world;		/* only trig on text from world */
    int pri, num;
    attr_t attr, subattr;
    short subexp;
    short prob, shots, invis;
    short flags, fallthru, quiet;
} Macro;

extern int invis_flag;

extern void   NDECL(init_macros);
extern attr_t FDECL(parse_attrs,(char **argp));
extern long   FDECL(parse_hook,(char **args));
extern Macro *FDECL(find_macro,(CONST char *name));
extern Macro *FDECL(find_num_macro,(int num)); 
extern Macro *FDECL(new_macro,(CONST char *trig, CONST char *binding,
                 long hook, CONST char *hargs, CONST char *body,
                 int pri, int prob, attr_t attr, int invis, int mflag));
extern int    FDECL(add_macro,(struct Macro *macro));
extern int    FDECL(add_hook,(char *name, CONST char *body));
extern int    FDECL(remove_macro,(char *args, attr_t attr, int byhook));
extern void   NDECL(nuke_dead_macros);
extern void   FDECL(kill_macro,(struct Macro *macro));
extern void   NDECL(rebind_key_macros);
extern void   FDECL(remove_world_macros,(struct World *w));
extern int    FDECL(save_macros,(char *args));
extern int    FDECL(do_macro,(Macro *macro, CONST char *args));
extern CONST char *FDECL(macro_body,(CONST char *name));
extern int    FDECL(find_and_run_matches,(CONST char *text, long hook,
                    Aline **alinep, struct World *world, int globalflag));

#ifdef DMALLOC
extern void   NDECL(free_macros);
#endif

#define add_ibind(key, cmd) \
    add_macro(new_macro(NULL, key, 0, NULL, cmd, 0, 0, 0, TRUE, 0))

#endif /* MACRO_H */
