;;;; Default keybindings
;;;; This file is loaded by stdlib.tf.

/loaded __TFLIB__/kbbind.tf

/require kbfunc.tf
/require complete.tf

; ^J, ^M, ^H, and ^? are handled internally.


;; ~keybind avoids warnings for undefined keys
/def -i ~keybind = /if (keycode({1}) !~ "") /def -iB'%{1}' = %-1%; /endif

/def -i ~defaultbind = \
    /if /!ismacro -msimple -ib'%1'%; /then \
        /def -ib'%1' = %-1%;\
    /endif

/def -i kb_updown = \
    /kbwarn updown Up/down now scroll through input history; if you prefer \
	the old behavior of moving within a line, "/load updown.tf".%; \
    /dokey %*

;;; Keys defined by name.

;/~keybind Up	/dokey_up
/~keybind Up	/kb_updown recallb
;/~keybind Down	/dokey_down
/~keybind Down	/kb_updown recallf
/~keybind Right	/dokey_right
/~keybind Left	/dokey_left
/~keybind Delete	/dokey_dch
/~keybind F1	/help
/~keybind Home	/dokey_home
/~keybind Insert	/@test insert := !insert
/~keybind PgDn	/dokey_pgdn
/~keybind PgUp	/dokey_pgup


;;; Defaults for keys normally defined by name.

/~defaultbind ^[[A /kb_updown recallb
/~defaultbind ^[[B /kb_updown recallf
/~defaultbind ^[[C /dokey_right
/~defaultbind ^[[D /dokey_left

/~defaultbind ^[OA /kb_updown recallb
/~defaultbind ^[OB /kb_updown recallf
/~defaultbind ^[OC /dokey_right
/~defaultbind ^[OD /dokey_left


;;; Bindings to cycle through connected sockets

;; 'Safe' bindings don't depend on potentially broken arrow keys
/def -ib'^[{'	= /dokey_socketb
/def -ib'^[}'	= /dokey_socketf

;; Termcap arrow keys
/eval \
    /if (keycode("left") !~ "") \
        /def -ib'^[$[keycode("left")]' = /dokey_socketb%; \
    /endif

/eval \
    /if (keycode("right") !~ "") \
        /def -ib'^[$[keycode("right")]' = /dokey_socketf%; \
    /endif

;; In case termcap arrow key entries are missing or wrong
/~defaultbind ^[^[OD	/dokey_socketb
/~defaultbind ^[^[OC	/dokey_socketf
/~defaultbind ^[^[[D	/dokey_socketb
/~defaultbind ^[^[[C	/dokey_socketf

/def -ib'^[-'	= /set kbnum=-
/def -ib'^[0'	= /set kbnum=+
/def -ib'^[1'	= /set kbnum=+1
/def -ib'^[2'	= /set kbnum=+2
/def -ib'^[3'	= /set kbnum=+3
/def -ib'^[4'	= /set kbnum=+4
/def -ib'^[5'	= /set kbnum=+5
/def -ib'^[6'	= /set kbnum=+6
/def -ib'^[7'	= /set kbnum=+7
/def -ib'^[8'	= /set kbnum=+8
/def -ib'^[9'	= /set kbnum=+9

;;; Other useful bindings
;;; Any dokey operation "X" can be performed with "/dokey X" or "/dokey_X".
;;; The only difference between the two invocations is efficiency.
;;; /~defaultbind is used for keys that may already be defined internally by
;;; copying the terminal driver.

/def -i kbwarn = \
    /if /test warn_5keys & !kbwarned_%1%; /then \
	/echo %% Note: keys have changed in version 5.0 to be more like emacs \
	    and many other unix apps.  %-1  This warning can be disabled with \
	    "/set warn_5keys=off".%; \
	/set kbwarned_%1=1%; \
    /endif

/def -ib'^A'	= /dokey_home
/def -ib"^B"	= /kbwarn char ^B/^F now move the cursor by a letter; \
		    use ESC b/ESC f to move by a word.%; \
		    /dokey_left
/def -ib'^D'	= /dokey_dch
/def -ib'^E'	= /dokey_end
/def -ib"^F"	= /kbwarn char ^B/^F now move the cursor by a letter; \
		    use ESC b/ESC f to move by a word.%; \
		    /dokey_right
; note ^G does NOT honor kbnum, so it can be used to cancel kbnum entry.
/def -ib'^G'	= /beep
;def -ib"^H"	= /dokey_bspc			; internal
;def -ib"^I"	= /complete			; complete.tf
/def -ib'^I'	= /dokey page
;def -ib"^J"	= /dokey newline		; internal
/def -ib'^K'	= /dokey_deol
/def -ib'^L'	= /dokey redraw
;def -ib"^M"	= /dokey newline		; internal
/def -ib'^N'	= /dokey recallf
;def -ib"^O"	= /operate-and-get-next		; not implemented
/def -ib'^P'	= /dokey recallb
/def -ib"^Q"	= /dokey lnext
/~defaultbind ^R /dokey refresh
;def -ib"^R"	= /dokey searchb		; conflict
;def -ib"^S"	= /dokey searchf		; conflict
/def -ib"^S"	= /dokey pause
/def -ib'^T'	= /kb_transpose_chars
/def -ib'^U'	= /kb_backward_kill_line
/~defaultbind ^V /dokey lnext
/~defaultbind ^W	/dokey_bword
/def -ib"^X^R"	= /load ~/.tfrc
/def -ib"^X^V"	= /version
/def -ib"^X^?"	= /kb_backward_kill_line

/def -ib'^X['	= /dokey_hpageback
/def -ib'^X]'	= /dokey_hpage
/def -ib'^X{'	= /dokey_pageback
/def -ib'^X}'	= /dokey_page

;def -ib"^?"	= /dokey_bspc			; internal

/def -ib'^[^E'	= /kb_expand_line
/def -ib'^[^H'	= /kb_backward_kill_word
/def -ib'^[^L'	= /dokey clear
/def -ib'^[^N'	= /dokey line
/def -ib'^[^P'	= /dokey lineback
;def -ib"^[!"	= /complete command		; complete.tf
;def -ib"^[%"	= /complete variable		; complete.tf
/def -ib'^[ '	= /kb_collapse_space
/def -ib'^[='	= /kb_goto_match
/def -ib'^[.'	= /kb_last_argument
;def -ib"^[/"	= /complete filename		; complete.tf
/def -ib'^[<'	= /dokey recallbeg
/def -ib'^[>'	= /dokey recallend
;def -ib"^[?"	= /possible-completions		; complete.tf
;def -ib"^[@"	= /complete hostname		; complete.tf
/def -ib'^[J'	= /dokey selflush
/def -ib'^[L'	= /kb_toggle_limit
/def -ib'^[_'	= /kb_last_argument
/def -ib"^[b"	= /kbwarn word ESC b/ESC f now move the cursor by a word; \
		    use ESC LEFT/ESC RIGHT to switch worlds.%; \
		    /dokey_wleft
/def -ib"^[c"	= /kb_capitalize_word
/def -ib"^[d"	= /kb_kill_word
/def -ib"^[f"	= /kbwarn word ESC b/ESC f now move the cursor by a word; \
		    use ESC LEFT/ESC RIGHT to switch worlds.%; \
		    /dokey_wright
/def -ib'^[h'	= /dokey_hpage
/def -ib'^[j'	= /dokey flush
/def -ib'^[l'	= /kb_downcase_word
/def -ib'^[n'	= /dokey searchf
/def -ib'^[p'	= /dokey searchb
/def -ib'^[u'	= /kb_upcase_word
/def -ib'^[v'	= /@test insert := !insert
;def -ib"^[~"	= /complete username		; complete.tf
/def -ib'^[^?'	= /kb_backward_kill_word

/def -ib'^]'	= /bg


;;; Other common keyboard-specific mappings which may or may not work
;F1
/~defaultbind ^[[11~	/help
/~defaultbind ^[OP	/help

; Some broken terminal emulators (TeraTerm, NiftyTelnet) send incorrect
; char sequences for the editor keypad (the 6 keys above the arrow keys).
; We can't cater to them without breaking keys for users with correct
; terminal emulators.
;
;                      niftytelnet
;            vt220     & teraterm
;           standard    (broken)
; insert     ^[[2~       ^[[1~
; home       ^[[1~       ^[[2~
; pgup       ^[[5~       ^[[3~
; delete     ^[[3~       ^[[4~
; end        ^[[4~       ^[[5~
; pgdn       ^[[6~       ^[[6~
;
; TeraTerm users should fix their emulators by copying
; %TFLIBDIR/teraterm.keyboard.cnf to keyboard.cnf in their TeraTerm
; directory.
;
; I do not have a fix for NiftyTelnet.

;Insert
/~defaultbind ^[[2~	/@test insert := !insert
;Delete
/~defaultbind ^[[3~	/dokey_dch
;PgDn
/~defaultbind ^[[6~	/dokey_pgdn
/~defaultbind ^[Os	/dokey_pgdn
;PgUp
/~defaultbind ^[[5~	/dokey_pgup
/~defaultbind ^[Oy	/dokey_pgup
;Home
/~defaultbind ^[[1~	/dokey_home
/~defaultbind ^[OH	/dokey_home
/~defaultbind ^[[H	/dokey_home
;End
/~defaultbind ^[[4~	/dokey_end
/~defaultbind ^[OF	/dokey_end
/~defaultbind ^[[F	/dokey_end

