;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; TinyFugue - programmable mud client
;;;; Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2002 Ken Keys
;;;;
;;;; TinyFugue (aka "tf") is protected under the terms of the GNU
;;;; General Public License.  See the file "COPYING" for details.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/set tf_stdlib_id=$Id: stdlib.tf,v 35000.71 2003/05/27 02:08:23 hawkeye Exp $

;;; TF macro library

;;; DO NOT EDIT THIS FILE.
;;;
;;; Personal commands should be performed in %HOME/.tfrc; public commands
;;; for all users should be performed in %TFLIBDIR/local.tf.  This file will
;;; be replaced when tf is upgraded; %HOME/.tfrc and %TFLIBDIR/local.tf will
;;; not, so changes you make to them will not be lost.

;;; This library is essential to the correct operation of TF.  It is as
;;; much a part of TF as the executable file.  It is designed so that
;;; it can be used by one or many users.

;;; Many internal macros here are named starting with "~" to minimize
;;; conflicts with the user's namespace; you should not give your own
;;; macros names beginning with "~".  Also, you probably don't want to
;;; use the -i flag in defining your personal macros, although you can.


;;; library loading
; Note: users should not rely on %_loaded_libs or any other undocumented
; feature of /loaded and /require.

/set _loaded_libs=

/def -i loaded = \
    /if /@test _loaded_libs !/ "*{%{1}}*"%; /then \
        /set _loaded_libs=%{_loaded_libs} %{1}%;\
;       in case the file this tries to /load another file that uses /loaded
        /let _required=0%; \
    /elseif (_required) \
        /exit%; \
    /endif

/def -i require = \
    /let _required=1%; \
    /load %{-L} %{L}


;;; visual status bar
/eval /load -q %TFLIBDIR/tfstatus.tf


;;; file compression

/if ( systype() =~ "unix" ) \
    /def -i COMPRESS_SUFFIX = .Z%;\
    /def -i COMPRESS_READ = zcat%;\
/elseif ( systype() =~ "os/2" ) \
    /def -i COMPRESS_SUFFIX = .zip%;\
    /def -i COMPRESS_READ = unzip -p%;\
/endif

;;; High priority for library hooks/triggers.  This is a hack.
/set maxpri=2147483647


;;; Commands
;; Some of these use helper macros starting with ~ to reduce conflicts with
;; the user's namespace.  Users should not rely on them or any other
;; undocumented implementation details.


;;; /echo [-a<attr>] [-p] [-oeAr] [-w[<world>]] <text>
/def -i echo = \
    /let opt_a=%; \
    /let opt_w=()%; \
    /let opt_p=0%; /let opt_o=0%; /let opt_e=0%; /let opt_r=0%; /let opt_A=0%; \
    /if (!getopts("a:poerAw:")) /return 0%; /endif%; \
    /return echo({*}, opt_a, !!opt_p, \
        (opt_w !~ "()") ? strcat("w",opt_w) : opt_e ? "e" : opt_A ? "a" : opt_r ? "r" : "o")

/def -i _echo = /test echo({*})

;;; /substitute [-a<attr>] [-p] <text>
/def -i substitute = \
    /if (!getopts("a:p", "")) /return 0%; /endif%; \
    /return substitute({*}, opt_a, !!opt_p)

;;; /sys <command>
; Executes an "inline" shell command.
; Only works for commands that do not require input or redraw the screen.

/def -i sys = /quote -S -decho \\!!%{*-:}


;;; /send [-nW] [-T<type>] [-w<world>] text
/def -i send = \
    /if (!getopts("hnWT:w:", "")) /return 0%; /endif%; \
    /let _text=%{*}%; \
    /let _flags=$[opt_h ? "h" : ""]$[opt_n ? "u" : ""]%; \
    /if (opt_W) \
        /~send $(/@listsockets -s)%; \
    /elseif (opt_T !~ "") \
        /~send $(/@listsockets -s -T%{opt_T})%; \
    /else \
        /test send(_text, {opt_w}, _flags)%; \
    /endif

/def -i ~send = \
    /while ({#}) \
        /@test send(_text, {1}, _flags)%; \
        /shift%; \
    /done


;; null commands
/def -i :	= /@test 1
/def -i true	= /@test 1
/def -i false	= /@test 0

;;

/def -i bg = /fg -n


;;  /ADDWORLD [-p] [-T<type>] [-s<srchost>] <name> [[<char> <pass>] <host> <port> [<file>]]
;;  /ADDWORLD [-T<type>] DEFAULT <char> <pass> [<file>]

/def -i addworld = \
    /if (!getopts("pxT:s:", "")) /return 0%; /endif%; \
    /let flags=$[strcat(opt_p ? "p" : ""), strcat(opt_x ? "x" : "")]%; \
    /if ({1} =/ "default") \
        /test addworld({1}, opt_T, "", "", {2}, {3}, {4}, flags, opt_s)%;\
    /elseif ({#} <= 4) \
        /test addworld({1}, opt_T, {2}, {3}, "", "", {4}, flags, opt_s)%;\
    /else \
        /test addworld({1}, opt_T, {4}, {5}, {2}, {3}, {6}, flags, opt_s)%;\
    /endif


;; /world [-nlqx] [<name>]
;; /world [-nlqx] <host> <port>

/def -i world = \
    /if (!getopts("lqnx", 0)) /return 0%; \
    /endif%; \
    /let _args=%*%; \
    /if (_args =~ "") \
	/let _args=$(/nth 1 $(/@listworlds -s))%; \
	/if (_args =/ "default") \
	    /let _args=$(/nth 2 $(/@listworlds -s))%; \
	/endif%; \
    /endif%; \
    /let _opts=%; \
    /if (opt_l) /let _opts=%opts -l%; /endif%; \
    /if (opt_q) /let _opts=%opts -q%; /endif%; \
    /if (opt_n) /let _opts=%opts -n%; /endif%; \
    /if (is_open(_args)) \
	/@fg %_opts %_args%; \
    /else \
	/@connect $[opt_x ? "-x" : ""] %_opts %_args%; \
    /endif


;; /purgeworld <name>...
/def -i purgeworld = /unworld $(/@listworlds -s %*)


;; for loop.
; syntax:  /for <var> <min> <max> <command>

/def -i for	= \
    /@eval \
        /let %1=%2%%; \
        /while ( %1 <= %3 ) \
            %-3%%; \
            /@test ++%1%%; \
        /done

;; flag toggler.
/def -i toggle	= /@test %1 := !%1

;; result negation.
/def -i not	= /@eval %*%; /@test !%?

;; expression evaluator.
/def -i expr	= /result %*

;; replace text in input buffer.
/def -i grab	= /@test kblen() & dokey("dline")%; /@test input({*})

;; partial hilites.
/def -i partial = /def -F -p%{hpri-0} -Ph -t"$(/escape " %*)"

;; triggers.
/def -i trig	= /trigpc 0 100 %*
/def -i trigc	= /trigpc 0 %{1-x} %-1
/def -i trigp	= /trigpc %{1-x} 100 %-1

/def -i undeft	= /untrig -anGgurfdhbBC0 - %*

/def -i nogag = \
    /if ({#}) \
        /untrig -ag - %*%;\
    /else \
        /echo %% Gags disabled.%;\
        /set gag=0%;\
    /endif

/def -i nohilite = \
    /if ({#}) \
        /untrig -aurfdhbBC0 - %*%;\
    /else \
        /echo %% Hilites disabled.%;\
        /set hilite=0%;\
    /endif

;; macro existance test.
/def -i ismacro = /test tfclose("o")%; /@list -s -i %{*-@}


;; cut-and-paste tool

; paste [-w<world>] [-spxtqn] [-e<end>] [-a<abort>] [prefix]
/def -i paste = \
    /if (!getopts("spnxtqw:e:a:", "")) /return 0%; /endif%; \
    /if (opt_p & opt_t) \
        /echo -e %% %0: Options -p and -t are mutually exclusive.%; \
	/return 0%; \
    /endif%; \
    /if (opt_x & opt_w !~ "") \
        /echo -e %% %0: Options -x and -w are mutually exclusive.%; \
	/return 0%; \
    /endif%; \
    /let _prefix=$[opt_n ? "" : {*-%{paste_prefix-:|}}]%; \
    /if (!opt_n) /shift%; /endif%; \
    /let _end=%{opt_e-/endpaste}%; \
    /let _abort=%{opt_a-/abort}%; \
    /let _line=%; \
    /let _text=%; \
    /let _world=%{opt_w-${world_name}}%; \
    /let _oldlen=0%; \
    /let _lead=0%; \
    /let _read=0%; \
    /if (!opt_q) \
	/echo -ep %% Entering paste mode.  Type "@{B}%{_end}@{n}" or "@{B}.@{n}" to end, or @{B}%{_abort}@{n} to abort.%; \
    /endif%; \
    /while (1) \
	/if ((_read := tfread(_line)) < 0 | _line =/ _abort) \
	    /return 0%; \
	/endif%; \
        /if (_line =/ _end | _line =/ ".") \
	    /break%; \
	/endif%; \
        /if (_line =/ "/quit" | _line =/ "/help*") \
            /echo -ep %% Type "@{B}%{_end}@{n}" or "@{B}.@{n}" to end /paste, or @{B}%{_abort}@{n} to abort.%; \
        /endif%; \
	/if (opt_t) \
	    /test regmatch("^ +", _line), _lead := strlen({P0})%; \
	    /if (!_oldlen) \
		/test _text := _line%; \
	    /elseif (_oldlen <= _lead) \
		/test _text := strcat(_text, substr(_line, _oldlen))%; \
	    /else \
		/_paste %{_prefix} %{_text}%; \
		/test _text := _line%; \
	    /endif%; \
	    /test _oldlen := strlen(_text)%; \
        /elseif (!opt_p) \
	    /if (!opt_s) \
		/test regmatch(" *$$", _line)%; \
		/let _line=%PL%; \
	    /endif%; \
            /test _paste(_prefix=~"" ? _line : strcat(_prefix, " ", _line))%; \
        /elseif (regmatch("^ *$", _line)) \
            /if (_text !~ "") \
		/_paste %{_prefix}%{_text}%; \
		/_paste %{_prefix}%; \
		/let _text=%; \
	    /endif%; \
        /else \
            /let _text=%{_text} $(/echo - %{_line})%; \
        /endif%; \
    /done%; \
    /if ((opt_p | opt_t) & _text !~ "") \
        /_paste %{_prefix}%{_text}%; \
    /endif%; \
    /return 1

/def -i _paste = \
    /if (opt_x) \
;	execute
	/eval -s0 - %*%; \
    /else \
;	send (preserving leading spaces)
	/test send({*}, _world)%; \
    /endif
;   /recordline -i - %*
; A /recordline here would allow history browsing during the paste, but do we
; really want pasted lines being stored permanently in history?  Anyway,
; there's currently no way to preserve leading spaces in /recordline.


;; other useful stuff.

/def -i first	= /result {1}
/def -i rest	= /result {-1}
/def -i last	= /result {L}
/def -i nth	= /result {1} > 0 ? shift({1}), {1} : ""

/def -i cd	= /lcd %{*-%HOME}
/def -i pwd	= /last $(/@lcd)

/def -i man	= /help %*

/def -i signal	= /quote -S -decho !kill -%{1-l} $[{1}!~"" ? getpid() : ""]

/def -i split	= /@test regmatch("^([^=]*[^ =])? *=? *(.*)", {*})

/def -i ver	= \
    /result regmatch('version (.*). %% Copyright', $$(/version)), {P1}

/def -i vercmp = \
    /let pat=^([0-9]+)\\.([0-9]+) (alpha|beta|gamma|stable) ([0-9]*)$$%; \
    /if (!regmatch(pat, {1})) \
        /echo -e %% %0: Bad version format "%1"%; \
        /return -2%; \
    /endif%; \
    /let maj1=%P1%; \
    /let min1=%P2%; \
    /let lev1=%P3%; \
    /let rev1=%P4%; \
    /if (!regmatch(pat, {2})) \
        /echo -e %% %0: Bad version format "%2"%; \
        /return -2%; \
    /endif%; \
    /let maj2=%P1%; \
    /let min2=%P2%; \
    /let lev2=%P3%; \
    /let rev2=%P4%; \
;   lev comparison works because (alpha, beta, gamma, stable) happen to be
;   alphabetically sorted.
    /return (maj1-maj2 ?: min1-min2 ?: strcmp(lev1,lev2) ?: rev1-rev2)


/def -i runtime = \
    /let real=$[time()]%; \
    /let cpu=$[cputime()]%; \
    /eval -s0 %{*}%; \
    /let result=%?%; \
    /_echo real=$[time() - real] cpu=$[cputime() - cpu]%; \
    /return %result


;;; Extended world definition macros

/def -i addtiny		= /addworld -T"tiny"	%*
/def -i addlp		= /addworld -T"lp"	%*
/def -i addlpp		= /addworld -T"lpp"	%*
/def -i adddiku		= /addworld -T"diku"	%*
/def -i addtelnet	= /addworld -T"telnet"	%*


;; Auto-switch connect hook
/def -iFp0 -agG -hCONNECT ~connect_switch_hook = /@fg %1

;; Proxy server connect hook
/eval /def -iFp%{maxpri} -agG -hPROXY proxy_hook = /proxy_command

/def -i proxy_command = \
    /proxy_connect%; \
;   Many proxy servers turn localecho off.  We don't want that.
    /localecho on%; \
    /trigger -hCONNECT ${world_name}%; \
    /if (login & ${world_character} !~ "" & ${world_login}) \
        /trigger -hLOGIN ${world_name}%; \
    /endif

/def -i proxy_connect = telnet ${world_host} ${world_port}

;; Heuristics to detect worlds that use prompts, but have not been classified
;; as such by the user's /addworld definition.
/def -iFp1 -mglob -T'{}' -hCONNECT ~detect_worldtype_hook = \
; telnet prompt
    /def -ip1 -n1 -w -mregexp -h'PROMPT [Ll]ogin: *$$' \
    ~detect_worldtype_telnet_${world_name} = \
        /echo -e %%% This looks like a telnet world, so I'm redefining it as \
            one.  You should explicitly set the type with the -T option of \
            /addworld.%%;\
        /addworld -Ttelnet ${world_name}%%;\
        /set lp=1%%;\
        /localecho on%%; \
        /@test prompt(strcat({PL}, {P0}))%%;\
        /purge -i ~detect_worldtype_*_${world_name}%; \
    /let cleanup=/purge -i #%?%; \
; generic prompt
    /def -ip0 -n1 -w -mregexp -h'PROMPT ...[?:] *$$' \
    ~detect_worldtype_prompt_${world_name} = \
        /echo -e %%% This looks like an unterminated-prompt world, so I'm \
            redefining it as one.  You should explicitly set the type with the \
            -T option of /addworld.%%;\
        /addworld -Tprompt ${world_name}%%; \
        /set lp=1%%; \
        /@test prompt(strcat({PL}, {P0}))%%; \
        /purge -i ~detect_worldtype_*_${world_name}%; \
    /let cleanup=%cleanup%%; /purge -i #%?%; \
; If there's no prompt in the first 60s, assume this is not a prompting world,
; and undefine the hooks to avoid false positives later.  We must also create
; a disconnect hook to undefine the prompt hooks if we disconnect before the
; timeout, and have the timeout process undefine the disconnect hook.
    /def -ip1 -n1 -w -hDISCONNECT = %cleanup%; \
    /let cleanup=%cleanup%%; /purge -i #%?%; \
    /repeat -60 1 %cleanup


;; Default worldtype hook: tiny login format (for backward compatibility),
;; but do not change any flags.
/eval \
    /def -mglob -T{} -hLOGIN -iFp%{maxpri} ~default_login_hook = \
        /~login_hook_tiny

;; Tiny hooks: login format, lp=off.
/eval \
    /def -mglob -T{tiny|tiny.*} -hWORLD -iFp%{maxpri} ~world_hook_tiny = \
        /set lp=0%; \
    /def -mglob -T{tiny|tiny.*} -hLOGIN -iFp%{maxpri} ~login_hook_tiny = \
        /let _char=$${world_character}%%;\
        /if (strchr(_char, ' ') >= 0) /let _char="%%_char"%%; /endif%%; \
        /let _pass=$${world_password}%%;\
        /if (strchr(_pass, ' ') >= 0) /let _pass="%%_pass"%%; /endif%%; \
        /send connect %%_char %%_pass

;; Generic prompt-world hooks: lp=on.
/eval \
    /def -mglob -Tprompt -hWORLD -iFp%{maxpri} ~world_hook_prompt = \
        /set lp=1

;; LP/Diku/Aber/etc. hooks: login format, lp=on.
/eval \
    /def -mglob -T{lp|lp.*|diku|diku.*|aber|aber.*} -hWORLD -iFp%{maxpri} \
    ~world_hook_lp = \
        /set lp=1%; \
    /def -mglob -T{lp|lp.*|diku|diku.*|aber|aber.*} -hLOGIN -iFp%{maxpri} \
    ~login_hook_lp = \
        /send -- $${world_character}%%; \
        /send -- $${world_password}

;; Hooks for LP-worlds with telnet end-of-prompt markers:
;; login format, lp=off.
/eval \
    /def -mglob -T{lpp|lpp.*} -hWORLD -iFp%{maxpri} ~world_hook_lpp = \
        /set lp=0%; \
    /def -mglob -T{lpp|lpp.*} -hLOGIN -iFp%{maxpri} ~login_hook_lpp = \
        /send -- $${world_character}%%; \
        /send -- $${world_password}


;; Telnet hooks: login format, lp=on, and localecho=on (except at
;; password prompt).
/eval \
    /def -mglob -T{telnet|telnet.*} -hCONNECT -iFp%{maxpri} ~con_hook_telnet =\
        /def -w -qhPROMPT -n1 -iFp$[maxpri-1] = /localecho on%;\
    /def -mglob -T{telnet|telnet.*} -hWORLD -iFp%{maxpri} ~world_hook_telnet =\
        /set lp=1%; \
    /def -mglob -T{telnet|telnet.*} -hLOGIN -iFp%{maxpri} ~login_hook_telnet =\
        /def -n1 -ip%{maxpri} -mregexp -w -h'PROMPT [Ll]ogin: *$$' \
        ~telnet_login_$${world_name} = \
            /send -- $$${world_character}%%; \
        /def -n1 -ip%{maxpri} -mregexp -w -h'PROMPT [Pp]assword: *$$' \
        ~telnet_pass_$${world_name} = \
            /send -- $$${world_password}%; \
    /def -mregexp -T'^telnet(\\\\..*)?$$' -h'PROMPT [Pp]assword: *$$' \
    -iFp$[maxpri-1] ~telnet_passwd = \
        /@test prompt(strcat({PL}, {P0}))%%;\
        /def -w -q -hSEND -i -n1 -Fp%{maxpri} ~echo_$${world_name} =\
            /localecho on%%;\
        /localecho off


;; /telnet <host> [<port>]
;; Defines a telnet-world and connects to it.
/def -i telnet = \
	/addtelnet %{1},%{2-23} %1 %{2-23}%; \
	/connect %{1},%{2-23}


;;; default filenames
; This is ugly, mainly to keep backward compatibility with the lame old
; "~/tiny.*" filenames and *FILE macros.  The new style, "~/*.tf", has
; a sensible suffix, and works on 8.3 FAT filesystems.  (A subdirectory
; would be nice, but then /save* would fail if the user hasn't created
; the subdirectory).

/if ( TINYPREFIX =~ "" & TINYSUFFIX =~ "" ) \
;   New-style names make more sense.
    /set TINYPREFIX=~/%; \
    /set TINYSUFFIX=.tf%; \
;   Old-style names on unix systems, for backward compatibility.
    /if ( systype() =~ "unix" ) \
        /set TINYPREFIX=~/tiny.%; \
        /set TINYSUFFIX=%; \
    /endif%; \
/endif

/eval /def -i MACROFILE		= %{TINYPREFIX}macros%{TINYSUFFIX}
/eval /def -i HILITEFILE	= %{TINYPREFIX}hilite%{TINYSUFFIX}
/eval /def -i GAGFILE		= %{TINYPREFIX}gag%{TINYSUFFIX}
/eval /def -i TRIGFILE		= %{TINYPREFIX}trig%{TINYSUFFIX}
/eval /def -i BINDFILE		= %{TINYPREFIX}bind%{TINYSUFFIX}
/eval /def -i HOOKFILE		= %{TINYPREFIX}hook%{TINYSUFFIX}
/eval /def -i WORLDFILE		= %{TINYPREFIX}world%{TINYSUFFIX}
/eval /def -i LOGFILE		= tiny.log


;;; define load* and save* macros with default filenames.

/def -i ~def_file_command = \
    /def -i %1%2	= \
        /%1 %%{1-$${%{3}FILE}} %{-3}

/~def_file_command  load  def     MACRO
/~def_file_command  load  hilite  HILITE
/~def_file_command  load  gag     GAG
/~def_file_command  load  trig    TRIG
/~def_file_command  load  bind    BIND
/~def_file_command  load  hook    HOOK
/~def_file_command  load  world   WORLD

/~def_file_command  save  def	MACRO   -mglob -h0 -b{} -t{} ?*
/~def_file_command  save  gag	GAG     -mglob -h0 -b{} -t -ag
/~def_file_command  save  trig	TRIG    -mglob -h0 -b{} -t -an
/~def_file_command  save  bind	BIND    -mglob -h0 -b
/~def_file_command  save  hook	HOOK    -mglob -h

/def -i savehilite = \
    /save %{1-${HILITEFILE}} -mglob -h0 -b{} -t -aurfdhbBC0%;\
    /save -a %{1-${HILITEFILE}} -mglob -h0 -t -P


;;; list macros

/def -i listdef		= /list %*
/def -i listfullhilite	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -aurfdhbBC0
/def -i listpartial	= /list -mglob -h0 -t'$(/escape ' %*)' -P
/def -i listhilite	= /listfullhilite%; /listpartial
/def -i listgag		= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -ag
/def -i listtrig	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -an
/def -i listbind	= /list -mglob -h0 -b'$(/escape ' %*)'
/def -i listhook	= /list -mglob -h'$(/escape ' %*)'


;;; purge macros

/def -i purgedef	= /purge -mglob -h0 -b{} - %{1-?*}
/def -i purgehilite	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -aurfdhbBC0
/def -i purgegag	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -ag
/def -i purgetrig	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -an
/def -i purgedeft	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' ?*
/def -i purgebind	= /purge -mglob -h0 -b'$(/escape ' %*)'
/def -i purgehook	= /purge -mglob -h'$(/escape ' %*)'


;; meta-character quoter
;; /escape <metachars> <string>
/def -i escape = \
    /let _meta=%; /let _dest=%; /let _tail=%; /let _i=%;\
    /test _meta:=strcat({1}, "\\\\")%;\
    /test _tail:={-1}%;\
    /while ((_i := strchr(_tail, _meta)) >= 0) \
        /test _dest:=strcat(_dest, substr(_tail,0,_i), "\\\\", substr(_tail,_i,1)), \
              _tail:=substr(_tail, _i+1)%;\
    /done%;\
    /result strcat(_dest, _tail)


;;;; Replace
;;; syntax:  /replace <old> <new> <string>

/def -i replace = /result replace({1}, {2}, {3})


;;; /loadhist [-lig] [-w<world>] file

/def -i loadhist = \
    /let _file=%L%; \
    /quote -S /recordline %-L '%%{_file-${LOGFILE}}

;;; /keys simulation
;; For backward compatibilty only.
;; Supports '/keys <mnem> = <key>' and '/keys' syntax.

/def -i keys =\
    /if ( {*} =/ "" ) \
        /@list -Ib%;\
    /elseif ( {*} =/ "*,*" ) \
        /echo -e %% The /keys comma syntax is no longer supported.%;\
        /echo -e %% See /help bind, /help dokey.%;\
    /elseif ( {*} =/ "{*} = ?*" ) \
        /def -ib'%{-2}' = /dokey %1%;\
    /elseif ( {*} =/ "*=*" ) \
        /echo -e %% '=' must be surrounded by spaces.%;\
        /echo -e %% See /help bind, /help dokey.%;\
    /else \
        /echo -e %% Bad /keys syntax.%;\
    /endif


;;; Retry connections

;; /retry <world> [<delay>]
;; Try to connect to <world>.  Repeat every <delay> seconds (default 60)
;; until successful.

/def -i retry = \
    /def -mglob -p%{maxpri} -F -h'CONFAIL $(/escape ' %1) *' ~retry_fail_%1 =\
        /repeat -%{2-60} 1 /connect %1%;\
    /def -mglob -1 -p%{maxpri} -F -h'CONNECT $(/escape ' %1)' ~retry_succ_%1=\
        /undef ~retry_fail_%1%;\
    /connect %1

;; /retry_off [<world>]
;; Cancels "/retry <world>" (default: all worlds)

/def -i retry_off = /purge -mglob {~retry_fail_%{1-*}|~retry_succ_%{1-*}}


;;; Hilites for pages and whispers
;; Simulates "/hilite page" and "/hilite whisper" in old versions.

/def -i hilite_whisper	= \
  /def -ip2 -ah -mregexp -t'^[^ ]* whispers,? ".*" (to [^ ]*)?$$' ~hilite_whisper1

/def -i hilite_page	= \
  /def -ip2 -ah -mglob -t'{*} pages from *[,:] *' ~hilite_page1%;\
  /def -ip2 -ah -mglob -t'You sense that {*} is looking for you in *' ~hilite_page2%;\
  /def -ip2 -ah -mglob -t'The message was: *' ~hilite_page3%;\
  /def -ip2 -ah -mglob -t'{*} pages[,:] *' ~hilite_page4%;\
  /def -ip2 -ah -mglob -t'In a page-pose*' ~hilite_page5

/def -i nohilite_whisper	= /purge -mglob -I ~hilite_whisper[1-9]
/def -i nohilite_page		= /purge -mglob -I ~hilite_page[1-9]


;;; backward compatible commands

/def -i cat = \
    /echo -e %% Entering cat mode.  Type "." to end.%; \
    /let _line=%; \
    /let _all=%; \
    /while ((tfread(_line) >= 0) & (_line !~ ".")) \
        /if (_line =/ "/quit") \
            /echo -e %% Type "." to end /cat.%; \
        /endif%; \
        /@test _all := \
            strcat(_all, (({1} =~ "%%" & _all !~ "") ? "%%;" : ""), _line)%; \
    /done%; \
    /recordline -i %_all%; \
    /@test eval(_all)

/def -i time = /@test echo(ftime({*-%%{time_format}})), time()

/def -i rand = \
    /if ( {#} == 0 ) /echo $[rand()]%;\
    /elseif ( {#} == 1 ) /echo $[rand({1})]%;\
    /elseif ( {#} == 2 ) /echo $[rand({1}, {2})]%;\
    /else /echo -e %% %0: too many arguments%;\
    /endif

; Since the default page key (TAB) is not obvious to a new user, we display
; instructions when he executes "/more on" if he hasn't re-bound the key.
/def -i more = \
    /if ( {*} =/ "{on|1}" & ismacro("-ib'^I' = /dokey page") ) \
        /echo -e %% "More" paging enabled.  Use TAB to scroll.%;\
    /endif%; \
    /set more %*

/def -i nolog		= /log off
/def -i nowrap		= /set wrap off
/def -i nologin		= /set login off
/def -i noquiet		= /set quiet off

/def -i act		= /trig %*
/def -i reply		= /set borg %*

/def -i background	= /set background %*
/def -i bamf		= /set bamf %*
/def -i borg		= /set borg %*
/def -i clearfull	= /set clearfull %*
/def -i cleardone	= /set cleardone %*
/def -i insert		= /set insert %*
/def -i login		= /set login %*
/def -i lp		= /set lp %*
/def -i lpquote		= /set lpquote %*
/def -i quiet		= /set quiet %*
/def -i quitdone	= /set quitdone %*
/def -i redef		= /set redef %*
/def -i shpause		= /set shpause %*
/def -i sockmload	= /set sockmload %*
/def -i sub		= /set sub %*
/def -i visual		= /set visual %*

/def -i gpri		= /set gpri %*
/def -i hpri		= /set hpri %*
/def -i isize		= /set isize %*
/def -i ptime		= /set ptime %*
/def -i wrapspace	= /set wrapspace %*

/def -i wrap = \
    /if ({*} =/ '[0-9]*') \
        /set wrapsize=%*%; \
        /set wrap=1%; \
    /else \
        /set wrap %*%;\
    /endif

/def -i ~do_prefix = \
    /if ( {-1} =/ "{|off|0|on|1}" ) \
        /set %{1}echo %{-1}%; \
    /elseif ( {-1} =/ "{all|2}" & {1} =~ "m" ) \
        /set %{1}echo %{-1}%; \
    /else \
        /set %{1}prefix=%{-1}%; \
        /set %{1}prefix%; \
        /set %{1}echo=1%; \
    /endif

/def -i kecho = /~do_prefix k %*
/def -i mecho = /~do_prefix m %*
/def -i qecho = /~do_prefix q %*


;;; Other standard libraries

/eval /load -q %TFLIBDIR/kbbind.tf
/eval /if (systype() =~ "os/2") /load -q %TFLIBDIR/kb-os2.tf%; /endif
/eval /load -q %TFLIBDIR/color.tf
/eval /load -q %TFLIBDIR/changes.tf


;;; constants

/set pi=
/test pi:=2 * acos(0)
/set e=
/test e:=exp(1)


;;; Copy shell's MAILPATH to tf's TFMAILPATH
; MAILPATH is a colon-separated list of fields; each field is a filename and
; an optional '?' or '%' followed by a message.

/eval /if (MAILPATH !~ "") \
    /let _head=%; \
    /let _tail=%{MAILPATH}%; \
    /while (regmatch("^([^?%%:]+)([?%%][^:]+)?:?", {_tail}))%; \
        /let _head=%{_head} %{P1}%; \
        /let _tail=%{PR}%; \
    /done%; \
    /set TFMAILPATH=%{_head}%; \
/endif


;;; Help for newbies
/def -i -h'SEND help' -Fq send_help = \
    /if (${world_name} =~ "") \
        /echo -e %% You are not connected to a world.%; \
        /echo -e %% Use "/help" to get help on TinyFugue.%; \
    /endif

;;; Load local public config file

/def -hloadfail -ag ~gagloadfail
/eval /load %{TFLIBDIR}/local.tf
/undef ~gagloadfail

