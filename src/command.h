/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.h,v 35004.19 2003/05/27 01:09:21 hawkeye Exp $ */

#ifndef COMMAND_H
#define COMMAND_H

typedef struct Value *(Handler)(String *args, int offset);

typedef struct BuiltinCmd {
    const char *const name;
    Handler *const func;	/* the implementation of the command */
    int reserved;		/* is name reserved? */
    struct Macro *macro;	/* macro with same name, if any */
} BuiltinCmd;

extern int exiting;

extern int      handle_command(String *cmd_line);
extern BuiltinCmd *find_builtin_cmd(const char *cmd);
extern int      do_file_load(const char *args, int tinytalk);
extern int      handle_echo_func(String *string, const char *attrstr,
                     int inline_flag, const char *dest);
extern int      handle_substitute_func(String *string,
                     const char *attrstr, int inline_flag);

#endif /* COMMAND_H */
