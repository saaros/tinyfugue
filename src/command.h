/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.h,v 35004.5 1997/11/23 23:03:56 hawkeye Exp $ */

#ifndef COMMAND_H
#define COMMAND_H

typedef struct Value *FDECL((Handler),(char *args));

extern int exiting;

extern int      FDECL(handle_command,(String *cmd_line));
extern Handler *FDECL(find_command,(CONST char *cmd));
extern int      FDECL(do_file_load,(CONST char *args, int tinytalk));

#endif /* COMMAND_H */
