;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; TinyFugue - programmable mud client
;;;; Copyright (C) 1994 Ken Keys
;;;;
;;;; TinyFugue (aka "tf") is protected under the terms of the GNU
;;;; General Public License.  See the file "COPYING" for details.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; $Id: stdlib.tf,v 32101.1 1994/03/30 22:55:51 hawkeye Exp $

;;; TF 3.0 macro library

;;; This library is essential to the correct operation of TF.  It is as
;;; much a part of TF as the executable file.  It is designed so that
;;; it can be used by one or many users.  If you wish to modify any of
;;; the defaults defined here, you should do so in your %HOME/.tfrc
;;; file (for personal commands) or %TFLIBDIR/local.tf (for public
;;; commands).

;;; Many "hidden" macros here are named starting with "~" to minimize
;;; conflicts with the user's namespace.  You should not give your own
;;; macros names beginning with "~".  Also, you probably don't want to
;;; use the -i flag in defining your own macros, although you can.


;;; file compression

; "normal" BSD compression
/def -i COMPRESS_SUFFIX=.Z
/def -i COMPRESS_READ=zcat

;;; High priority for library hooks/triggers.  This is a hack.
/set maxpri=2147483647

;;; Commands
;; Some of these use helper macros starting with ~ to reduce conflicts with
;; the user's namespace.

;; /world [-nlq] [<name>]
;; /world [-nlq] <host> <port>

/def -i world = /if /not /@fg -s %*%; /then /@connect %*%; /endif

;; for loop.
; syntax:  /for <var> <start> <end> <command>

/def -i for	= \
    /@eval \
        /let %1=%2%%; \
        /while /test %1 <= %3 %%; /do \
            %-3%%; \
            /let %1=$$[%1 + 1]%%; \
        /done

;; flag toggler.
/def -i toggle	= /test %1 := !%1

;; result negation.
/def -i not	= /@eval %*%; /test !%?

;; expression evaluator.
/def -i expr	= /@eval /echo -- $$[%*]

;; replace text in input buffer.
/def -i grab = /dokey dline%; /input %*

;; partial hilites.
/def -i partial = /def -F -mregexp -p%{hpri-0} -Ph -t"$(/escape " %*)"

;; triggers.
/def -i trig	= /trigpc 0 100 %*
/def -i trigc	= /trigpc 0 %{1-x} %-1
/def -i trigp	= /trigpc %{1-x} 100 %-1

;; null command. expands its arguments and returns 0.
/def -i :

;; macro existance test.
/def -i ismacro = /test $(/last $(/eval /list -s -i %{*-@}%%; /echo %%?))

;; other useful stuff.

/def -i first	= /echo - %1
/def -i rest	= /echo - %-1
/def -i last	= /echo - %L
/def -i nth	= /shift %1%; /echo - %1

/def -i cd	= /let dir=%L%; /@eval /lcd %%{dir-%HOME}
/def -i pwd	= /last $(/lcd)

/def -i man	= /help %*


;;; Extended world definition macros

/def -i addtiny		= /addworld -T"tiny"	%*
/def -i addlp		= /addworld -T"lp"	%*
/def -i addlpp		= /addworld -T"lpp"	%*
/def -i adddiku		= /addworld -T"diku"	%*
/def -i addtelnet	= /addworld -T"telnet"	%*


;; Auto-switch connect hook

/def -iFp0 -agG -hCONNECT ~connect_switch_hook = /fg %1


;; Default worldtype hook: tiny login format (for backward compatibility),
;; but do not change any flags.
/eval \
	/def -mglob -T{} -hLOGIN -iFp%{maxpri} ~default_login_hook = \
		/send connect $${world_character} $${world_password}

;; Tiny hooks: login format, lp=off, always_echo=off.
/eval \
	/def -mglob -T{tiny|tiny.*} -hWORLD -iFp%{maxpri} ~world_hook_tiny = \
		/set lp=0%%; \
		/set always_echo=0%; \
	/def -mglob -T{tiny|tiny.*} -hLOGIN -iFp%{maxpri} ~login_hook_tiny = \
		/send connect $${world_character} $${world_password}

;; LP/Diku/Aber/etc. hooks: login format, lp=on, always_echo=off.
/eval \
    /def -mglob -T{lp|lp.*|diku|diku.*|aber|aber.*} -hWORLD -iFp%{maxpri} \
    ~world_hook_lp = \
        /set lp=1%%; \
        /set always_echo=0%; \
    /def -mglob -T{lp|lp.*} -hLOGIN -iFp%{maxpri} ~login_hook_lp = \
        /send -- $${world_character}%%; \
        /send -- $${world_password}

;; Hooks for LP-worlds with telnet end-of-prompt markers:
;; login format, lp=off, always_echo=off.
/eval \
    /def -mglob -T{lpp|lpp.*} -hWORLD -iFp%{maxpri} ~world_hook_lpp = \
        /set lp=0%%; \
        /set always_echo=0%; \
    /def -mglob -T{lpp|lpp.*} -hLOGIN -iFp%{maxpri} ~login_hook_lpp = \
        /send -- $${world_character}%%; \
        /send -- $${world_password}

;; Telnet hooks: login format, lp=on, and always_echo=on (except at
;; password prompt).
/eval \
    /def -mglob -Ttelnet -hWORLD -iFp%{maxpri} ~world_hook_telnet = \
	/set lp=1%%; \
	/set always_echo=1%; \
    /def -mglob -Ttelnet -hLOGIN -iFp%{maxpri} ~login_hook_telnet = \
	/send -- $${world_character}%%; \
	/send -- $${world_password}%; \
    /def -mregexp -Ttelnet -h'PROMPT ^Password: *$$' -iFp%{maxpri} \
    ~telnet_passwd = \
	/prompt %%*%%;\
	/def -w$${world_name} -t* -1 -iFp%{maxpri} ~echo_$${world_name} = \
	    /set always_echo=1%%;\
	/set always_echo=0

;; /telnet <host> [<port>]
;; Defines a telnet-world and connects to it.
/def -i telnet = \
	/addtelnet %{1},%{2-telnet} %1 %{2-telnet}%; \
	/connect %{1},%{2-telnet}


;;; /dokey functions.

/def -i dokey_bspc	= /test kbdel(kbpoint() - 1)
/def -i dokey_bword	= /test regmatch("[^ ]* *$$", kbhead()), \
			        kbdel(kbpoint() - strlen(P0))
/def -i dokey_dch	= /test kbdel(kbpoint() + 1)
/def -i dokey_deol	= /test kbdel(kblen())
/def -i dokey_dline	= /dokey dline
/def -i dokey_down	= /test kbgoto(kbpoint() + wrapsize)
/def -i dokey_dword	= /test kbdel(kbwordright())
/def -i dokey_end	= /test kbgoto(kblen())
/def -i dokey_home	= /test kbgoto(0)
/def -i dokey_left	= /test kbgoto(kbpoint() - 1)
/def -i dokey_lnext	= /dokey lnext
/def -i dokey_newline	= /dokey newline
/def -i dokey_recallb	= /dokey recallb
/def -i dokey_recallf	= /dokey recallf
/def -i dokey_right	= /test kbgoto(kbpoint() + 1)
/def -i dokey_searchb	= /dokey searchb
/def -i dokey_searchf	= /dokey searchf
/def -i dokey_socketb	= /dokey socketb
/def -i dokey_socketf	= /dokey socketf
/def -i dokey_up	= /test kbgoto(kbpoint() - wrapsize)
/def -i dokey_wleft	= /test kbgoto(kbwordleft())
/def -i dokey_wright	= /test kbgoto(kbwordright())
/def -i dokey_page	= /dokey page
/def -i dokey_hpage	= /dokey hpage
/def -i dokey_line	= /dokey line
/def -i dokey_flush	= /dokey flush


;;; Default Key Bindings

/def -ib'^M'	= /DOKEY NEWLINE

/def defaultbind = \
    /if /not /ismacro -msimple -ib'%1'%; /then \
        /def -ib'%1' = /dokey %2%;\
    /endif

/defaultbind ^J NEWLINE

;; default bindings for keys normally defined by stty.

/defaultbind ^H BSPC
/defaultbind ^? BSPC
/defaultbind ^W BWORD
/defaultbind ^U DLINE
/defaultbind ^R REFRESH
/defaultbind ^V LNEXT

;; default bindings for keys normally defined by termcap.

/defaultbind ^[[A UP
/defaultbind ^[[B DOWN
/defaultbind ^[[C RIGHT
/defaultbind ^[[D LEFT

/defaultbind ^[OA UP
/defaultbind ^[OB DOWN
/defaultbind ^[OC RIGHT
/defaultbind ^[OD LEFT

;; other useful bindings
;; Any operation "foo" can be performed with "/dokey foo" or "/dokey_foo".
;; The only difference between the two invocations is efficiency.

/def -ib'^P'	= /dokey recallb
/def -ib'^N'	= /dokey recallf
/def -ib'^[p'	= /dokey searchb
/def -ib'^[n'	= /dokey searchf
/def -ib'^[b'	= /dokey socketb
/def -ib'^[f'	= /dokey socketf
/def -ib'^D'	= /dokey_dch
/def -ib'^[d'	= /dokey_dword
/def -ib'^L'	= /dokey redraw
/def -ib'^A'	= /dokey_home
/def -ib'^E'	= /dokey_end
/def -ib'^B'	= /dokey_wleft
/def -ib'^F'	= /dokey_wright
/def -ib'^K'	= /dokey_deol
/def -ib'^[v'	= /test insert := !insert
/def -ib'^I'	= /dokey page
/def -ib'^[h'	= /dokey hpage
/def -ib'^[l'	= /dokey line
/def -ib'^[j'	= /dokey flush

/undef defaultbind

;;; default filenames
; Should be variables, but these are backward compatible.

/eval /def -i MACROFILE		= %{TINYPREFIX-~/tiny.}macros
/eval /def -i HILITEFILE	= %{TINYPREFIX-~/tiny.}hilite
/eval /def -i GAGFILE		= %{TINYPREFIX-~/tiny.}gag
/eval /def -i TRIGFILE		= %{TINYPREFIX-~/tiny.}trig
/eval /def -i BINDFILE		= %{TINYPREFIX-~/tiny.}bind
/eval /def -i HOOKFILE		= %{TINYPREFIX-~/tiny.}hook
/eval /def -i WORLDFILE		= %{TINYPREFIX-~/tiny.}world
/eval /def -i LOGFILE		= %{TINYPREFIX-~/tiny.}log


;;; list macros

/def -i listdef		= /list %*
/def -i listhilite	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -aurfdhbBC0
/def -i listgag		= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -ag
/def -i listtrig	= /list -mglob -h0 -b{} -t'$(/escape ' %*)' -an
/def -i listbind	= /list -mglob -h0 -b'$(/escape ' %*)'
/def -i listhook	= /list -mglob -h'$(/escape ' %*)'


;;; purge macros

/def -i purgedef	= /purge -mglob -h0 -b{} %{1-?*}
/def -i purgehilite	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -aurfdhbBC0
/def -i purgegag	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -ag
/def -i purgetrig	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' -an
/def -i purgedeft	= /purge -mglob -h0 -b{} -t'$(/escape ' %*)' ?*
/def -i purgebind	= /purge -mglob -h0 -b'$(/escape ' %*)'
/def -i purgehook	= /purge -mglob -h'$(/escape ' %*)'


;;; define load* and save* macros with default filenames.

/def -i ~def_file_command = \
    /def -i %1%2	= \
        /let file=%%1%%;\
        /@eval /%1 %%%{file-$${%{3}FILE}} %{-3}

/~def_file_command  load  def     MACRO
/~def_file_command  load  hilite  HILITE
/~def_file_command  load  gag     GAG
/~def_file_command  load  trig    TRIG
/~def_file_command  load  bind    BIND
/~def_file_command  load  hook    HOOK
/~def_file_command  load  world   WORLD

/~def_file_command  save  def     MACRO   -mglob -h0 -b{} -t{} ?*
/~def_file_command  save  hilite  HILITE  -mglob -h0 -b{} -t -aurfdhbBC0
/~def_file_command  save  gag     GAG     -mglob -h0 -b{} -t -ag
/~def_file_command  save  trig    TRIG    -mglob -h0 -b{} -t -an
/~def_file_command  save  bind    BIND    -mglob -h0 -b
/~def_file_command  save  hook    HOOK    -mglob -h


;; library loading

/set _loaded_libs=

/def -i ~loaded = \
    /if /test _loaded_libs !/ "*{%{1}}*"%; /then \
        /set _loaded_libs=%{_loaded_libs} %{1}%;\
    /endif

/def -i require = \
    /if /test _loaded_libs !/ "*{%{1}}*"%; /then \
        /load %{TFLIBDIR}/%{1}%;\
    /endif

;; meta-character quoter
;; /escape <metachars> <string>
/def -i escape = \
    /let meta=$[strcat(%1, "\\")]%;\
    /let dest=%;\
    /let tail=%-1%;\
    /let i=garbage%;\
    /while /test (i := strchr(tail, meta)) >= 0 %; /do \
        /let dest=$[strcat(dest, substr(tail,0,i), "\\", substr(tail,i,1))]%;\
        /let tail=$[substr(tail, i+1, 99999999)]%;\
    /done%;\
    /echo -- %{dest}%{tail}


;;; /loadhist [-lig] [-w<world>] file

/def -i loadhist = \
    /let file=%L%; \
    /quote -0 /recordline %-L '%%{file-${LOGFILE}}

;;; /keys simulation
;; For backward compatibilty only.
;; Supports '/keys <mnem> = <key>' and '/keys' syntax.

/def -i keys =\
    /if /test $[%* =/ ""]%; /then \
        /list -Ib%;\
    /elseif /test $[%* =/ "*,*"]%; /then \
        /echo %% The /keys comma syntax is no longer supported.%;\
        /echo %% See /help bind, /help dokey.%;\
    /elseif /test $[%* =/ "{*} = ?*"]%; /then \
        /def -ib'%{-2}' = /dokey %1%;\
    /elseif /test $[%* =/ "*=*"]%; /then \
        /echo %% '=' must be surrounded by spaces.%;\
        /echo %% See /help bind, /help dokey.%;\
    /else \
        /echo %% Bad /keys syntax.%;\
    /endif


;;; popular color definitions
;; I provided extended ANSI codes because they're the most common.
;; The user can of course redefine them.

/set end_color			\033[37;0m

/set start_color_black		\033[30m
/set start_color_red		\033[31m
/set start_color_green		\033[32m
/set start_color_yellow		\033[33m
/set start_color_blue		\033[34m
/set start_color_magenta	\033[35m
/set start_color_cyan		\033[36m
/set start_color_white		\033[37m

; This group is set up for 16 colors on xterms.
; Colors 0-7 correspond to the 8 named colors above.  The named color
; variables override the numbered variables below, so to use numbered
; variables 0-7 you must unset the named variables (or reset them to
; the codes below).

/set start_color_0		\033[200m
/set start_color_1		\033[201m
/set start_color_2		\033[202m
/set start_color_3		\033[203m
/set start_color_4		\033[204m
/set start_color_5		\033[205m
/set start_color_6		\033[206m
/set start_color_7		\033[207m
/set start_color_8		\033[208m
/set start_color_9		\033[209m
/set start_color_10		\033[210m
/set start_color_11		\033[211m
/set start_color_12		\033[212m
/set start_color_13		\033[213m
/set start_color_14		\033[214m
/set start_color_15		\033[215m


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
  /def -ip2ah -mglob -t'{*} whispers, "*"' ~hilite_whisper1%;\
  /def -ip2ah -mglob -t'{*} whispers "*"' ~hilite_whisper2

/def -i hilite_page	= \
  /def -ip2ah -mglob -t'{*} pages from *[,:] *' ~hilite_page1%;\
  /def -ip2ah -mglob -t'You sense that {*} is looking for you in *' ~hilite_page2%;\
  /def -ip2ah -mglob -t'The message was: *' ~hilite_page3%;\
  /def -ip2ah -mglob -t'{*} pages[,:] *' ~hilite_page4%;\
  /def -ip2ah -mglob -t'In a page-pose*' ~hilite_page5

/def -i nohilite_whisper	= /purge -mglob -I ~hilite_whisper[1-9]
/def -i nohilite_page		= /purge -mglob -I ~hilite_page[1-9]


;;; backward compatible commands

/def -i rand = \
    /if /test %# < 2%; /then /echo $[rand(%1)]%;\
    /elseif /test %# == 2%; /then /echo $[rand(%1, %2)]%;\
    /elseif /test %# > 2%; /then /echo %% rand: too many arguments%;\
    /endif%;\

; Since the default page key (TAB) is not obvious to a new user, we display
; instructions when he executes "/more on" if he hasn't re-bound the key.
/def -i more = \
    /let args=%*%;\
    /if /test args =/ "{on|1}"%; /then \
        /if /ismacro -ib'^I' = /dokey page%; /then \
            /echo %% "More" paging enabled.  Use TAB to scroll.%;\
        /endif%; \
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
    /if /test $[%* > 1]%; /then \
        /set wrapsize=%*%; \
        /set wrap=1%; \
    /else \
        /set wrap %*%;\
    /endif

/def -i ~do_prefix = \
    /let args=%-1%;\
    /if /test args =/ "{|off|0|on|1}"%; /then \
        /set %{1}echo %args%; \
    /elseif /test args =/ "{all|2}" & "%{1}" =~ "m"%; /then \
        /set %{1}echo %args%; \
    /else \
        /set %{1}prefix=%args%; \
        /set %{1}prefix%; \
        /set %{1}echo=1%; \
    /endif

/def -i kecho = /~do_prefix k %*
/def -i mecho = /~do_prefix m %*
/def -i qecho = /~do_prefix q %*


;;; Load local public config file

/def -hloadfail -ag ~loadfail

/eval /load %{TFLIBDIR}/local.tf

/undef ~loadfail

