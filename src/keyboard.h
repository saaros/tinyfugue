/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: keyboard.h,v 33000.2 1994/04/26 08:56:29 hawkeye Exp $ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

extern void          NDECL(init_keyboard);
extern struct Macro *FDECL(bind_key,(struct Macro *macro));
extern void          FDECL(unbind_key,(struct Macro *macro));
extern struct Macro *FDECL(find_key,(char *key));
extern int           FDECL(do_kbdel,(int place));
extern int           NDECL(dokey_dline);
extern int           NDECL(do_kbwordleft);
extern int           NDECL(do_kbwordright);
extern void          NDECL(handle_keyboard_input);
extern void          FDECL(handle_input_string,(char *input, unsigned int len));
extern int           NDECL(handle_input_line);

#endif /* KEYBOARD_H */
