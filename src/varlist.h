/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1996 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: varlist.h,v 35000.25 1999/01/31 00:27:58 hawkeye Exp $ */

/* It may not be easy to read in 80 columns, but it keeps the constants and
 * the array in the same place, so they can't get out of sync.
 */

/* These MUST be sorted by name! */

/*      ID             , NAME           , VAL    , TYPE   , ENUMS     , IVAL  , FUNC */

#ifdef HAVE_setlocale
varcode(VAR_LANG       , "LANG"         , NULL   , VARSTRX, NULL      , 0     , ch_locale),
varcode(VAR_LC_ALL     , "LC_ALL"       , NULL   , VARSTRX, NULL      , 0     , ch_locale),
varcode(VAR_LC_CTYPE   , "LC_CTYPE"     , NULL   , VARSTRX, NULL      , 0     , ch_locale),
varcode(VAR_LC_TIME    , "LC_TIME"      , NULL   , VARSTRX, NULL      , 0     , ch_locale),
#endif /* HAVE_setlocale */
varcode(VAR_MAIL       , "MAIL"         , NULL   , VARSTR , NULL      , 0     , ch_mailfile),
varcode(VAR_TERM       , "TERM"         , NULL   , VARSTR , NULL      , 0     , change_term),
varcode(VAR_TFLIBDIR   , "TFLIBDIR"     , LIBDIR , VARSTR , NULL      , 0     , NULL),
varcode(VAR_TFMAILPATH , "TFMAILPATH"   , NULL   , VARSTR , NULL      , 0     , ch_mailfile),
varcode(VAR_TFPATH     , "TFPATH"       , NULL   , VARSTR , NULL      , 0     , NULL),
varcode(VAR_TZ         , "TZ"           , NULL   , VARSTRX, NULL      , 0     , ch_timezone),
varcode(VAR_auto_fg    , "auto_fg"      , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_background , "background"   , NULL   , VARENUM, enum_flag , TRUE  , tog_bg),
varcode(VAR_backslash  , "backslash"    , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_bamf       , "bamf"         , NULL   , VARENUM, enum_bamf , FALSE , NULL),
varcode(VAR_beep       , "beep"         , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_bg_output  , "bg_output"    , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_binary_eol , "binary_eol"   , NULL   , VARENUM, enum_eol ,  EOL_LF, NULL),
varcode(VAR_borg       , "borg"         , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_cleardone  , "cleardone"    , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_clearfull  , "clearfull"    , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_async_conn , "connect"	, NULL   , VARENUM, enum_block, TRUE  , NULL),
varcode(VAR_emulation  , "emulation"    , NULL   , VARENUM, enum_emul , EMUL_ANSI_ATTR, NULL),
varcode(VAR_gag        , "gag"          , NULL   , VARENUM, enum_flag , TRUE  , NULL),
#ifdef PLATFORM_OS2
varcode(VAR_async_name , "gethostbyname", NULL   , VARENUM, enum_block, TRUE  , NULL),
#else
varcode(VAR_async_name , "gethostbyname", NULL   , VARENUM, enum_block, FALSE , NULL),
#endif
varcode(VAR_gpri       , "gpri"         , NULL   , VARINT , NULL      , 0     , NULL),
varcode(VAR_hilite     , "hilite"       , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_hiliteattr , "hiliteattr"   , "B"    , VARSTR , NULL      , 0     , ch_hiliteattr),
varcode(VAR_histsize   , "histsize"     , NULL   , VARPOS , NULL      , 1000  , NULL),
varcode(VAR_hook       , "hook"         , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_hpri       , "hpri"         , NULL   , VARINT , NULL      , 0     , NULL),
varcode(VAR_insert     , "insert"       , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_isize      , "isize"        , NULL   , VARPOS , NULL      , 3     , ch_visual),
varcode(VAR_istrip     , "istrip"       , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_kecho      , "kecho"        , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_kprefix    , "kprefix"      , NULL   , VARSTR , NULL      , 0     , NULL),
varcode(VAR_login      , "login"        , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_lp         , "lp"           , NULL   , VARENUM, enum_flag , FALSE , tog_lp),
varcode(VAR_lpquote    , "lpquote"      , NULL   , VARENUM, enum_flag , FALSE , runall),
varcode(VAR_maildelay  , "maildelay"    , NULL   , VARINT , NULL      , 60    , ch_maildelay),
varcode(VAR_matching   , "matching"     , NULL   , VARENUM, enum_match, 1     , NULL),
varcode(VAR_max_iter   , "max_iter"     , NULL   , VARINT , NULL      , 1000  , NULL),
varcode(VAR_max_recur  , "max_recur"    , NULL   , VARINT , NULL      , 100   , NULL),
varcode(VAR_mecho      , "mecho"        , NULL   , VARENUM, enum_mecho, 0     , NULL),
varcode(VAR_meta_esc   , "meta_esc"     , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_more       , "more"         , NULL   , VARENUM, enum_flag , FALSE , tog_more),
varcode(VAR_mprefix    , "mprefix"      , "+"    , VARSTR , NULL      , 0     , NULL),
varcode(VAR_oldslash   , "oldslash"     , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_pedantic   , "pedantic"     , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_prompt_sec , "prompt_sec"   , NULL   , VARINT , NULL      , 0     , NULL),
varcode(VAR_prompt_usec, "prompt_usec"  , NULL   , VARINT , NULL      , 250000, NULL),
varcode(VAR_proxy_host , "proxy_host"   , NULL   , VARSTR , NULL      , 0     , NULL),
varcode(VAR_proxy_port , "proxy_port"   , "23"   , VARSTR , NULL      , 0     , NULL),
varcode(VAR_ptime      , "ptime"        , NULL   , VARINT , NULL      , 1     , NULL),
varcode(VAR_qecho      , "qecho"        , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_qprefix    , "qprefix"      , NULL   , VARSTR , NULL      , 0     , NULL),
varcode(VAR_quiet      , "quiet"        , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_quitdone   , "quitdone"     , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_redef      , "redef"        , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_refreshtime, "refreshtime"  , NULL   , VARINT , NULL      , 250000, NULL),
varcode(VAR_scroll     , "scroll"       , NULL   , VARENUM, enum_flag , FALSE , ch_visual),
varcode(VAR_shpause    , "shpause"      , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_snarf      , "snarf"        , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_sockmload  , "sockmload"    , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_status_attr, "status_attr"  , NULL   , VARSTR , NULL      , 0     , ch_status_attr),
varcode(VAR_status_fields, "status_fields", NULL , VARSTR , NULL      , 0     , ch_status_fields),
varcode(VAR_status_pad , "status_pad"   , "_"    , VARSTR , NULL      , 0     , update_status_line),
varcode(VAR_sub        , "sub"          , NULL   , VARENUM, enum_sub  , SUB_KEYWORD , NULL),
varcode(VAR_tabsize    , "tabsize"      , NULL   , VARPOS , NULL      , 8     , NULL),
varcode(VAR_telopt     , "telopt"       , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_time_format, "time_format"  , "%H:%M", VARSTR , NULL      , 0     , NULL),
varcode(VAR_visual     , "visual"       , NULL   , VARENUM, enum_flag , -1    , ch_visual),
varcode(VAR_watchdog   , "watchdog"     , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_watchname  , "watchname"    , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_wordpunct  , "wordpunct"    , "_"    , VARSTR , NULL      , FALSE , NULL),
varcode(VAR_wrap       , "wrap"         , NULL   , VARENUM, enum_flag , TRUE  , NULL),
varcode(VAR_wraplog    , "wraplog"      , NULL   , VARENUM, enum_flag , FALSE , NULL),
varcode(VAR_wrapsize   , "wrapsize"     , NULL   , VARINT , NULL      , 0     , NULL),
varcode(VAR_wrapspace  , "wrapspace"    , NULL   , VARINT , NULL      , 0     , NULL),
varcode(NUM_VARS       , NULL           , NULL   , 0      , NULL      , 0     , NULL)

