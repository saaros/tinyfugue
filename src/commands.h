/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: commands.h,v 35004.19 1999/01/31 00:27:39 hawkeye Exp $ */

#ifndef COMMANDS_H
#define COMMANDS_H

extern struct Value *FDECL(newint_fl,(long i, CONST char *file, int line));
extern struct Value *FDECL(newstrid,(CONST char *str, int len, int type,
              CONST char *file, int line));

#define newint(i)         newint_fl(i, __FILE__, __LINE__)
#define newid(s,l)        newstrid(s, l, TYPE_ID, __FILE__, __LINE__)
#define newstr(s,l)       newstrid(s, l, TYPE_STR, __FILE__, __LINE__)
#define newstrliteral(s)  newstr(s, sizeof(s))


#define HANDLER(name) struct Value *FDECL(name,(char *args))

extern HANDLER (handle_dc_command);
extern HANDLER (handle_def_command);
extern HANDLER (handle_dokey_command);
extern HANDLER (handle_edit_command);
extern HANDLER (handle_eval_command);
extern HANDLER (handle_exit_command);
extern HANDLER (handle_export_command);
extern HANDLER (handle_fg_command);
extern HANDLER (handle_help_command);
extern HANDLER (handle_input_command);
extern HANDLER (handle_list_command);
extern HANDLER (handle_listsockets_command);
extern HANDLER (handle_liststreams_command);
extern HANDLER (handle_listvar_command);
extern HANDLER (handle_listworlds_command);
extern HANDLER (handle_prompt_command);
extern HANDLER (handle_purge_command);
extern HANDLER (handle_result_command);
extern HANDLER (handle_return_command);
extern HANDLER (handle_saveworld_command);
extern HANDLER (handle_shift_command);
extern HANDLER (handle_test_command);
extern HANDLER (handle_undefn_command);
extern HANDLER (handle_unset_command);
extern HANDLER (handle_unworld_command);
#ifndef NO_PROCESS
extern HANDLER (handle_kill_command);
extern HANDLER (handle_ps_command);
extern HANDLER (handle_quote_command);
extern HANDLER (handle_repeat_command);
#else
# define handle_kill_command         NULL
# define handle_ps_command           NULL
# define handle_quote_command        NULL
# define handle_repeat_command       NULL
#endif
#ifndef NO_HISTORY
extern HANDLER (handle_histsize_command);
extern HANDLER (handle_log_command);
extern HANDLER (handle_recall_command);
extern HANDLER (handle_recordline_command);
extern HANDLER (handle_watchdog_command);
extern HANDLER (handle_watchname_command);
#else
# define handle_histsize_command     NULL
# define handle_log_command          NULL
# define handle_recall_command       NULL
# define handle_recordline_command   NULL
# define handle_watchdog_command     NULL
# define handle_watchname_command    NULL
#endif

#endif /* COMMANDS_H */
