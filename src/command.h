/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.h,v 35004.3 1997/03/27 01:04:21 hawkeye Exp $ */

#ifndef COMMAND_H
#define COMMAND_H

typedef int     FDECL((Handler),(char *args));

extern int      FDECL(handle_command,(String *cmd_line));
extern Handler *FDECL(find_command,(CONST char *cmd));
extern int      FDECL(do_file_load,(CONST char *args, int tinytalk));

#endif /* COMMAND_H */
