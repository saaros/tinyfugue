/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: commands.h,v 33000.1 1994/04/26 08:56:29 hawkeye Exp $ */

#ifndef COMMANDS_H
#define COMMANDS_H

extern HANDLER (handle_addworld_command);
extern HANDLER (handle_dc_command);
extern HANDLER (handle_def_command);
extern HANDLER (handle_dokey_command);
extern HANDLER (handle_edit_command);
extern HANDLER (handle_export_command);
extern HANDLER (handle_fg_command);
extern HANDLER (handle_help_command);
extern HANDLER (handle_list_command);
extern HANDLER (handle_listsockets_command);
extern HANDLER (handle_prompt_command);
extern HANDLER (handle_purge_command);
extern HANDLER (handle_purgeworld_command);
extern HANDLER (handle_saveworld_command);
extern HANDLER (handle_send_command);
extern HANDLER (handle_shift_command);
extern HANDLER (handle_test_command);
extern HANDLER (handle_undefn_command);
extern HANDLER (handle_unset_command);
extern HANDLER (handle_unworld_command);
#ifndef NO_PROCESS
extern HANDLER (handle_kill_command);
extern HANDLER (handle_log_command);
extern HANDLER (handle_ps_command);
extern HANDLER (handle_quote_command);
extern HANDLER (handle_repeat_command);
#else
# define handle_kill_command NULL
# define handle_log_command NULL
# define handle_ps_command NULL
# define handle_quote_command NULL
# define handle_repeat_command NULL
#endif
#ifndef NO_HISTORY
extern HANDLER (handle_recordline_command);
#else
# define handle_recordline_command NULL
#endif

#endif /* COMMANDS_H */
