/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996, 1997, 1998, 1999, 2002, 2003, 2004 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: varlist.h,v 35000.62 2004/02/17 06:44:44 hawkeye Exp $ */

/* This keeps the constants and the array in the same place, so they can't
 * get out of sync.
 */

#define varstr(id, name, val, func) \
    varcode(id, name, val, TYPE_STR, 0, NULL, 0, 0, func)
#define varstrx(id, name, val, func) \
    varcode(id, name, val, TYPE_STR, VARAUTOX, NULL, 0, 0, func)
#define varint(id, name, ival, func) \
    varcode(id, name, NULL, TYPE_INT, 0, NULL, ival, 0, func)
#define vartime(id, name, ival, uval, func) \
    varcode(id, name, NULL, TYPE_TIME, 0, NULL, ival, uval, func)
#define varpos(id, name, ival, func) \
    varcode(id, name, NULL, TYPE_POS, 0, NULL, ival, 0, func)
#define varenum(id, name, ival, func, enums) \
    varcode(id, name, NULL, TYPE_ENUM, 0, enums, ival, 0, func)
#define varflag(id, name, ival, func) \
    varcode(id, name, NULL, TYPE_ENUM, 0, enum_flag, ival, 0, func)

/* These MUST be sorted by name! */
/*      ID,		NAME,		VALUE,		FUNC,	ENUMS	*/

#if HAVE_SETLOCALE
varstrx(VAR_LANG,	"LANG",		NULL,		ch_locale)
varstrx(VAR_LC_ALL,	"LC_ALL",	NULL,		ch_locale)
varstrx(VAR_LC_CTYPE,	"LC_CTYPE",	NULL,		ch_locale)
varstrx(VAR_LC_TIME,	"LC_TIME",	NULL,		ch_locale)
#endif /* HAVE_SETLOCALE */
varstr (VAR_MAIL,	"MAIL",		NULL,		ch_mailfile)
varstr (VAR_TERM,	"TERM",		NULL,		change_term)
varstr (VAR_TFLIBDIR,	"TFLIBDIR",	LIBDIR,		NULL)
varstr (VAR_TFMAILPATH,	"TFMAILPATH",	NULL,		ch_mailfile)
varstr (VAR_TFPATH,	"TFPATH",	NULL,		NULL)
varstrx(VAR_TZ,		"TZ",		NULL,		ch_timezone)
varstr (VAR_alert_attr,	"alert_attr",	"Br",		ch_alert_attr)
vartime(VAR_alert_time,	"alert_time",	5,0,		NULL)
varflag(VAR_auto_fg,	"auto_fg",	TRUE,		NULL)
varflag(VAR_background,	"background",	TRUE,		tog_bg)
varflag(VAR_backslash,	"backslash",	TRUE,		NULL)
varenum(VAR_bamf,	"bamf",		FALSE,		NULL,	enum_bamf)
varflag(VAR_beep,	"beep",		TRUE,		NULL)
varflag(VAR_bg_output,	"bg_output",	TRUE,		NULL)
varenum(VAR_binary_eol,	"binary_eol",	EOL_LF,		NULL,	enum_eol)
varflag(VAR_borg,	"borg",		TRUE,		NULL)
varenum(VAR_cecho,	"cecho",	0,		NULL,	enum_mecho)
varflag(VAR_cleardone,	"cleardone",	FALSE,		NULL)
varflag(VAR_clearfull,	"clearfull",	FALSE,		NULL)
varenum(VAR_async_conn,	"connect",	TRUE,		NULL,	enum_block)
varflag(VAR_defcompile,	"defcompile",	FALSE,		NULL)
varenum(VAR_emulation,	"emulation",	EMUL_ANSI_ATTR,	NULL,	enum_emul)
varflag(VAR_expand_tabs,"expand_tabs",	TRUE,		NULL)
varflag(VAR_gag,	"gag",		TRUE,		NULL)
varenum(VAR_async_name,	"gethostbyname",TRUE,		NULL,	enum_block)
varint (VAR_gpri,	"gpri",		0,		NULL)
varflag(VAR_hilite,	"hilite",	TRUE,		NULL)
varstr (VAR_hiliteattr,	"hiliteattr",	"B",		ch_hiliteattr)
varpos (VAR_histsize,	"histsize",	1000,		NULL)
varflag(VAR_hook,	"hook",		TRUE,		NULL)
varint (VAR_hpri,	"hpri",		0,		NULL)
varenum(VAR_iecho,	"iecho",	0,		NULL,	enum_mecho)
varflag(VAR_insert,	"insert",	TRUE,		NULL)
varflag(VAR_interactive,"interactive",	-1,		NULL)
varpos (VAR_isize,	"isize",	3,		ch_visual)
varflag(VAR_istrip,	"istrip",	FALSE,		NULL)
varstr (VAR_kbnum,	"kbnum",	NULL,		NULL)
varflag(VAR_kecho,	"kecho",	FALSE,		NULL)
varflag(VAR_keepalive,	"keepalive",	TRUE,		tog_keepalive)
varflag(VAR_keypad,	"keypad",	FALSE,		tog_keypad)
varstr (VAR_kprefix,	"kprefix",	NULL,		NULL)
varflag(VAR_login,	"login",	TRUE,		NULL)
varflag(VAR_lp,		"lp",		FALSE,		tog_lp)
varflag(VAR_lpquote,	"lpquote",	FALSE,		ch_lpquote)
vartime(VAR_maildelay,	"maildelay",	60,0,		ch_maildelay)
varenum(VAR_matching,	"matching",	1,		NULL,	enum_match)
varint (VAR_max_instr,	"max_instr",	1000000,	NULL)
varint (VAR_max_kbnum,	"max_kbnum",	999,		NULL)
varint (VAR_max_recur,	"max_recur",	100,		NULL)
varint (VAR_max_trig,	"max_trig",	1000,		NULL)
#if HAVE_MCCP
varflag(VAR_mccp,	"mccp",		TRUE,		NULL)
#else
varenum(VAR_mccp,	"mccp",		FALSE,		NULL,	enum_off)
#endif
varenum(VAR_mecho,	"mecho",	0,		NULL,	enum_mecho)
varenum(VAR_meta_esc,	"meta_esc",	META_NONPRINT,	NULL,	enum_meta)
varflag(VAR_more,	"more",		FALSE,		tog_more)
varstr (VAR_mprefix,	"mprefix",	"+",		NULL)
varflag(VAR_oldslash,	"oldslash",	TRUE,		NULL)
varflag(VAR_optimize,	"optimize",	TRUE,		NULL)
varflag(VAR_pedantic,	"pedantic",	FALSE,		NULL)
varstr (VAR_prompt_sec,	"prompt_sec",	NULL,		obsolete_prompt)
varstr (VAR_prompt_usec,"prompt_usec",	NULL,		obsolete_prompt)
vartime(VAR_prompt_wait,"prompt_wait",	0,250000,	NULL)
varstr (VAR_proxy_host,	"proxy_host",	NULL,		NULL)
varstr (VAR_proxy_port,	"proxy_port",	"23",		NULL)
vartime(VAR_ptime,	"ptime",	1,0,		NULL)
varflag(VAR_qecho,	"qecho",	FALSE,		NULL)
varstr (VAR_qprefix,	"qprefix",	NULL,		NULL)
varflag(VAR_quiet,	"quiet",	FALSE,		NULL)
varflag(VAR_quitdone,	"quitdone",	FALSE,		NULL)
varflag(VAR_redef,	"redef",	TRUE,		NULL)
varint (VAR_refreshtime,"refreshtime",	100000,		NULL)
varflag(VAR_scroll,	"scroll",	FALSE,		ch_visual)
varflag(VAR_shpause,	"shpause",	TRUE,		NULL)
varint (VAR_sigfigs,	"sigfigs",	15,		NULL)
varflag(VAR_snarf,	"snarf",	FALSE,		NULL)
varflag(VAR_sockmload,	"sockmload",	FALSE,		NULL)
varstr (VAR_stat_attr,	"status_attr",	NULL,		ch_status_attr)
varstr (VAR_stat_fields,"status_fields",NULL,		ch_status_fields)
varstr (VAR_stat_pad,	"status_pad",	"_",		update_status_line)
varstr (VAR_stint_clock,"status_int_clock",NULL,	ch_status_int)
varstr (VAR_stint_more,	"status_int_more",NULL,		ch_status_int)
varstr (VAR_stint_world,"status_int_world",NULL,	ch_status_int)
varenum(VAR_sub,	"sub",		SUB_KEYWORD,	NULL,	enum_sub)
varpos (VAR_tabsize,	"tabsize",	8,		NULL)
varflag(VAR_telopt,	"telopt",	FALSE,		NULL)
varenum(VAR_textdiv,	"textdiv",	TRUE,		NULL,	enum_textdiv)
varstr (VAR_textdiv_str,"textdiv_str",	"=====",	NULL)
varstr (VAR_tfhost,	"tfhost",	NULL,		NULL)
varstr (VAR_time_format,"time_format",	"%H:%M",	NULL)
varflag(VAR_virtscreen,	"virtscreen",	TRUE,		NULL)
varflag(VAR_visual,	"visual",	-1,		ch_visual)
varflag(VAR_warn_5keys, "warn_5keys",	TRUE,		NULL)
varflag(VAR_warn_curly_re,"warn_curly_re",TRUE,		NULL)
varflag(VAR_warn_def_B,	"warn_def_B",	TRUE,		NULL)
varflag(VAR_warn_status,"warn_status",	TRUE,		NULL)
varflag(VAR_watchdog,	"watchdog",	FALSE,		NULL)
varflag(VAR_watchname,	"watchname",	FALSE,		NULL)
varstr (VAR_wordpunct,	"wordpunct",	"_",		NULL)
varflag(VAR_wrap,	"wrap",		TRUE,		ch_wrap)
varflag(VAR_wraplog,	"wraplog",	FALSE,		NULL)
varint (VAR_wrappunct,	"wrappunct",	10,		ch_wrap)
varint (VAR_wrapsize,	"wrapsize",	0,		ch_wrap)
varint (VAR_wrapspace,	"wrapspace",	4,		ch_wrap)

