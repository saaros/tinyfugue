/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: macro.h,v 33000.3 1994/04/26 08:56:29 hawkeye Exp $ */

#ifndef MACRO_H
#define MACRO_H

#include "world.h"   /* struct World */

typedef struct Macro {
    char *name;
    struct ListEntry *numnode;		/* node in list by number */
    struct ListEntry *trignode;		/* node in list by priority */
    struct ListEntry *hooknode;		/* node in list by hook */
    struct ListEntry *hashnode;		/* node in list in hash bucket */
    struct Macro *tnext;		/* temp list ptr for collision/death */
    char *bind, *body;
    Pattern trig, hargs, wtype;		/* trigger/hook/worldtype patterns */
    int hook;				/* bit vector, at least 32 bits */
    struct World *world;		/* only trig on text from world */
    int pri, num;
    short attr, subexp, subattr;
    short prob, shots, invis, temp, dead, fallthru, mflag;
} Macro;

enum Hooks {
  H_ACTIVITY,
  H_BACKGROUND,
  H_BAMF,
  H_CONFAIL,
  H_CONFLICT,
  H_CONNECT,
  H_DISCONNECT,
  H_HISTORY,
  H_KILL,
  H_LOAD,
  H_LOADFAIL,
  H_LOG,
  H_LOGIN,
  H_MAIL,
  H_MORE,
  H_PENDING,
  H_PROCESS,
  H_PROMPT,
  H_REDEF,
  H_RESIZE,
  H_RESUME,
  H_SEND,
  H_SHADOW,
  H_SHELL,
  H_WORLD,
  NUM_HOOKS                              /* not a hook, but a count */
};

#define ALL_HOOKS  (~(~0L << NUM_HOOKS))

extern void    NDECL(init_macros);
extern short   FDECL(parse_attrs,(char **argp));
extern Macro  *FDECL(find_macro,(char *name));
extern Macro  *FDECL(new_macro,(char *name, char *trig, char *binding,
    int hook, char *hargs, char *body, int pri, int prob, int attr, int invis));
extern int     FDECL(add_macro,(struct Macro *macro));
extern int     FDECL(add_hook,(char *name, char *body));
extern int     FDECL(remove_macro,(char *args, int attr, int byhook));
extern void    NDECL(nuke_dead_macros);
extern void    FDECL(kill_macro,(struct Macro *macro));
extern void    FDECL(remove_world_macros,(struct World *w));
extern int     FDECL(save_macros,(char *args));
extern int     FDECL(do_macro,(Macro *macro, char *args));
extern short   VDECL(do_hook,(int indx, char *fmt, char *argfmt, ...));
extern char   *FDECL(macro_body,(char *name));
extern int     FDECL(find_and_run_matches,(char *text, int hook, Aline *aline));

#define add_ibind(key, cmd) \
    add_macro(new_macro("", NULL, key, 0, NULL, cmd, 0, 0, 0, TRUE))

#endif /* MACRO_H */
