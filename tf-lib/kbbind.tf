;;;; Default keybindings
;;; This file is loaded by stdlib.tf.
;;; ^J, ^M, and ^H are builtin, and do not need to be bound.

/loaded __TFLIB__/kbbind.tf

/require kbfunc.tf

; ^J, ^M, ^H, and ^? are handled internally.


; keybind avoids warnings for undefined keys
/def keybind = /if (keycode({1}) !~ "") /def -iB'%{1}' = %-1%; /endif

/def defaultbind = \
    /if /!ismacro -msimple -ib'%1'%; /then \
        /def -ib'%1' = %-1%;\
    /endif


;; Keys defined by name.

/keybind Up	/dokey_up
/keybind Down	/dokey_down
/keybind Right	/dokey_right
/keybind Left	/dokey_left
/keybind F1	/help
/keybind Home	/dokey_home
/keybind Insert	/@test insert := !insert
/keybind PgDn	/dokey page


;; Defaults for keys normally defined by name.

/defaultbind ^[[A /dokey_up
/defaultbind ^[[B /dokey_down
/defaultbind ^[[C /dokey_right
/defaultbind ^[[D /dokey_left

/defaultbind ^[OA /dokey_up
/defaultbind ^[OB /dokey_down
/defaultbind ^[OC /dokey_right
/defaultbind ^[OD /dokey_left


;; Defaults for keys normally defined by terminal driver.

/defaultbind ^W /dokey_bword
/defaultbind ^U /dokey dline
/defaultbind ^R /dokey refresh
/defaultbind ^V /dokey lnext


;; Other useful bindings
;; Any dokey operation "foo" can be performed with "/dokey foo" or "/dokey_foo".
;; The only difference between the two invocations is efficiency.

/def -ib'^A'	= /dokey_home
/def -ib'^B'	= /dokey_wleft
/def -ib'^D'	= /dokey_dch
/def -ib'^E'	= /dokey_end
/def -ib'^F'	= /dokey_wright
/def -ib'^G'	= /beep 1
/def -ib'^I'	= /dokey page
/def -ib'^K'	= /dokey_deol
/def -ib'^L'	= /dokey redraw
/def -ib'^N'	= /dokey recallf
/def -ib'^P'	= /dokey recallb
/def -ib'^T'	= /kb_transpose_chars
/def -ib'^[^E'	= /kb_expand_line
/def -ib'^[ '	= /kb_collapse_space
/def -ib'^[-'	= /kb_goto_match
/def -ib'^[.'	= /kb_last_argument
/def -ib'^[<'	= /dokey recallbeg
/def -ib'^[>'	= /dokey recallend
/def -ib'^[J'	= /dokey selflush
/def -ib'^[L'	= /dokey line
/def -ib'^[_'	= /kb_last_argument
/def -ib'^[b'	= /fg -<
/def -ib'^[c'	= /kb_capitalize_word
/def -ib'^[d'	= /dokey_dword
/def -ib'^[f'	= /fg ->
/def -ib'^[h'	= /dokey hpage
/def -ib'^[j'	= /dokey flush
/def -ib'^[l'	= /kb_downcase_word
/def -ib'^[n'	= /dokey searchf
/def -ib'^[p'	= /dokey searchb
/def -ib'^[u'	= /kb_upcase_word
/def -ib'^[v'	= /@test insert := !insert
/def -ib'^[^h'	= /kb_backward_kill_word
/def -ib'^[^?'	= /kb_backward_kill_word


;; Other common keyboard-specific mappings
;F1
/defaultbind ^[[11~	/help
/defaultbind ^[OP	/help
;Insert
/defaultbind ^[[2~	/@test insert := !insert
;Delete
/defaultbind ^[[3~	/dokey_dch
;PgDn
/defaultbind ^[[6~	/dokey page
/defaultbind ^[Os	/dokey page
;Home
/defaultbind ^[[1~	/dokey_home
;End
/defaultbind ^[[4~	/dokey_end


;; clean up

/undef keybind
/undef defaultbind

