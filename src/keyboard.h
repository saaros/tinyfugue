/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: keyboard.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

extern void          NDECL(init_keyboard);
extern EditFunc     *FDECL(find_efunc,(char *name));
extern void          FDECL(set_ekey,(char *key, char *cmd));
extern struct Macro *FDECL(bind_key,(struct Macro *macro));
extern void          FDECL(unbind_key,(struct Macro *macro));
extern struct Macro *FDECL(find_key,(char *key));
extern int           FDECL(do_kbdel,(int place));
extern void          FDECL(do_grab,(Aline *aline));
extern void          NDECL(handle_keyboard_input);
extern void          FDECL(handle_input_string,(char *input, unsigned int len));
extern int           NDECL(handle_input_line);
extern void          NDECL(free_keyboard);

#endif /* KEYBOARD_H */
