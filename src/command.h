/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1998 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.h,v 35004.8 1998/05/03 19:33:50 hawkeye Exp $ */

#ifndef COMMAND_H
#define COMMAND_H

typedef struct Value *FDECL((Handler),(char *args));

extern int exiting;

extern int      FDECL(handle_command,(String *cmd_line));
extern Handler *FDECL(find_command,(CONST char *cmd));
extern int      FDECL(do_file_load,(CONST char *args, int tinytalk));
extern int      FDECL(handle_echo_func,(CONST char *string, CONST char *attrstr,
                     int inline_flag, CONST char *dest));
extern int      FDECL(handle_substitute_func,(CONST char *string,
                     CONST char *attrstr, int inline_flag));

#endif /* COMMAND_H */
