/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: keyboard.h,v 35004.5 1997/03/27 01:04:31 hawkeye Exp $ */

#ifndef KEYBOARD_H
#define KEYBOARD_H

extern void          NDECL(init_keyboard);
extern int           FDECL(bind_key,(struct Macro *macro));
extern void          FDECL(unbind_key,(struct Macro *macro));
extern struct Macro *FDECL(find_key,(CONST char *key));
extern int           FDECL(do_kbdel,(int place));
extern int           FDECL(do_kbword,(int dir));
extern int           NDECL(do_kbmatch);
extern int           NDECL(handle_keyboard_input);
extern int           NDECL(handle_input_line);

#endif /* KEYBOARD_H */
