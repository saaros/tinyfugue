;;;; Bash-like keyboard bindings

/loaded __TFLIB__/kb-bash.tf

/require kbfunc.tf
/require complete.tf

;; The commented-out lines are either unimplemented, already defined,
;; or conflict with pre-existing bindings.

;def -ib"^?"	= /dokey_bspc
;def -ib"^[!"	= /complete command
;def -ib"^[%"	= /complete variable
/def -ib"^[."	= /kb_last_argument
;def -ib"^[/"	= /complete filename
;def -ib"^[<"	= /beginning-of-history
;def -ib"^[>"	= /end-of-history
;def -ib"^[?"	= /possible-completions
/def -iB"up"	= /dokey recallb
/def -iB"down"	= /dokey recallf
;def -ib"^[@"	= /complete hostname
/def -ib"^[^?"	= /kb_backward_kill_word
/def -ib"^[^e"	= /kb_expand_line
/def -ib"^[_"	= /kb_last_argument
/def -ib"^[b"	= /dokey_wleft
/def -ib"^[c"	= /kb_capitalize_word
/def -ib"^[d"	= /kb_kill_word
/def -ib"^[f"	= /dokey_wright
/def -ib"^[l"	= /kb_downcase_word
;def -ib"^[t"	= /transpose-words
/def -ib"^[u"	= /kb_upcase_word
;def -ib"^[~"	= /complete username
/def -ib"^a"	= /dokey_home
/def -ib"^b"	= /dokey_left
/def -ib"^d"	= /dokey_dch
/def -ib"^e"	= /dokey_end
/def -ib"^f"	= /dokey_right
;def -ib"^h"	= /dokey_bspc
;def -ib"^i"	= /complete
;def -ib"^j"	= /dokey newline
/def -ib"^k"	= /dokey_deol
/def -ib"^l"	= /dokey redraw
;def -ib"^m"	= /dokey newline
/def -ib"^n"	= /dokey recallf
;def -ib"^o"	= /operate-and-get-next
/def -ib"^p"	= /dokey recallb
;def -ib"^q"	= /dokey lnext
;def -ib"^r"	= /dokey searchb
;def -ib"^s"	= /dokey searchf
/def -ib"^t"	= /kb_transpose_chars
/def -ib"^u"	= /kb_backward_kill_line
/def -ib"^v"	= /dokey lnext
/def -ib"^w"	= /dokey_bword
/def -ib"^x^?"	= /kb_backward_kill_line
/def -ib"^x^r"	= /load ~/.tfrc
/def -ib"^x^v"	= /version
