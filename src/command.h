/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: command.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef COMMAND_H
#define COMMAND_H

extern int      FDECL(handle_command,(char *cmdline));
extern Handler *FDECL(find_command,(char *cmd));
extern int      FDECL(do_file_load,(char *args, int tinytalk));

#endif /* COMMAND_H */
