;;; Commands that are useful when bound to keys.

;;; /kb_backward_kill_line	delete from cursor to beginning of line
;;; /kb_kill_word		delete from cursor to end of punctuated word
;;; /kb_backward_kill_word	delete from cursor to start of punctuated word
;;; /kb_capitalize_word		capitialize current word
;;; /kb_downcase_word		convert current word to lowercase
;;; /kb_upcase_word		convert current word to uppercase
;;; /kb_transpose_chars		swap current character with previous character
;;; /kb_last_argument		insert last word of previous line
;;; /kb_expand_line		/eval and replace current line
;;; /kb_goto_match		move cursor to matching parenthesis or bracket

/loaded __TFLIB__/kbfunc.tf

;;; /dokey functions.

/def -i dokey_bspc	= /@test kbdel(kbpoint() - (kbnum?:1))
/def -i dokey_bword	= /@test regmatch("[^ ]* *$$", kbhead()), \
			        kbdel(kbpoint() - strlen({P0}))
/def -i dokey_dch	= /@test kbdel(kbpoint() + (kbnum?:1))
/def -i dokey_deol	= /@test kbdel(kblen())
/def -i dokey_dline	= /@test kbgoto(0), kbdel(kblen())
/def -i dokey_down	= /@test kbgoto(kbpoint() + wrapsize * (kbnum?:1))
/def -i dokey_dword	= /kb_kill_word
/def -i dokey_end	= /@test kbgoto(kblen())
/def -i dokey_home	= /@test kbgoto(0)
/def -i dokey_left	= /@test kbgoto(kbpoint() - (kbnum?:1))
/def -i dokey_lnext	= /dokey lnext
/def -i dokey_newline	= /dokey newline
/def -i dokey_pause	= /dokey pause
/def -i dokey_recallb	= /dokey recallb
/def -i dokey_recallbeg	= /dokey recallbeg
/def -i dokey_recallend	= /dokey recallend
/def -i dokey_recallf	= /dokey recallf
/def -i dokey_redraw	= /dokey redraw
/def -i dokey_right	= /@test kbgoto(kbpoint() + (kbnum?:1))
/def -i dokey_searchb	= /dokey searchb
/def -i dokey_searchf	= /dokey searchf
/def -i dokey_socketb	= /fg -c$[-kbnum?:-1]
/def -i dokey_socketf	= /fg -c$[+kbnum?:1]
/def -i dokey_up	= /@test kbgoto(kbpoint() - wrapsize * (kbnum?:1))
/def -i dokey_wleft	= /test kbgoto(kb_nth_word(-(kbnum?:1)))
/def -i dokey_wright	= /test kbgoto(kb_nth_word(kbnum?:1))
/def -i dokey_page	= /test morescroll(winlines() * (kbnum?:1))
/def -i dokey_pageback	= /test morescroll(-winlines() * (kbnum?:1))
/def -i dokey_hpage	= /test morescroll(winlines() * (kbnum?:1) / 2)
/def -i dokey_hpageback	= /test morescroll(-winlines() * (kbnum?:1) / 2)
/def -i dokey_line	= /test morescroll(kbnum?:1)
/def -i dokey_lineback	= /test morescroll(-(kbnum?:1))
/def -i dokey_flush	= /dokey flush
/def -i dokey_selflush	= /dokey selflush

; These two are intended to be redefined as the user wants
/def -i dokey_pgup	= /dokey_pageback
/def -i dokey_pgdn	= /dokey_page


/def -i kb_backward_kill_line = /@test kbdel(0)

/def -i kb_nth_word = \
    /let i=%{1-1}%; \
    /let point=$[kbpoint()]%; \
    /while (i<0) /@test point:=kbwordleft(point), ++i%; /done%; \
    /while (i>0) /@test point:=kbwordright(point), --i%; /done%; \
    /return point

/def -i kb_kill_word = /@test kbdel(kb_nth_word(kbnum?:1))
/def -i kb_backward_kill_word = /@test kbdel(kb_nth_word(-(kbnum?:1)))

/def -i kb_capitalize_word = \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /repeat -S $[kbnum>0?+kbnum:1] \
	/@test kbgoto(kbwordright()), kbgoto(kbwordleft()) %%;\
	/let end=$$[kbwordright()]%%;\
	/@test input(toupper(substr(kbtail(), 0, 1))) %%;\
	/@test input(tolower(substr(kbtail(), 0, end - kbpoint()))) %;\
    /set insert=%{_old_insert}

/def -i kb_downcase_word = \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /repeat -S $[kbnum>0?+kbnum:1] \
	/@test input(tolower(substr(kbtail(), 0, kbwordright() - kbpoint()))) %;\
    /set insert=%{_old_insert}

/def -i kb_upcase_word = \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /repeat -S $[kbnum>0?+kbnum:1] \
	/@test input(toupper(substr(kbtail(), 0, kbwordright() - kbpoint()))) %;\
    /set insert=%{_old_insert}

/def -i kb_transpose_chars = \
    /if ( kbpoint() <= 0 ) /beep 1%; /return 0%; /endif%; \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
;   Can't use /dokey_left because it would use %kbnum.
    /@test kbgoto(kbpoint() - (kbpoint()==kblen()) - 1)%; \
    /@test input(strcat(substr(kbtail(),1,kbnum>0?kbnum:1), \
	substr(kbtail(),0,1))) %;\
    /set insert=%{_old_insert}

/def -i kb_last_argument = \
    /input $(/last $(/recall -i - -$[1 + (kbnum>0?kbnum:1)]))

/def -i kb_expand_line = \
    /eval /grab $(/recall -i 1)

/def -i kb_goto_match = \
    /let _match=$[kbmatch()]%; \
    /@test (_match < 0) ? beep() : kbgoto(_match)

/def -i kb_collapse_space = \
    /while (substr(kbtail(), 0, 2) =~ "  ") \
        /@test kbdel(kbpoint() + 1)%; \
    /done%; \
    /while (substr(strcat(kbhead(), kbtail()), kbpoint()-1, 2) =~ "  ") \
        /@test kbdel(kbpoint() - 1)%; \
    /done

/def -i kb_toggle_limit = \
    /if /limit%; /then /unlimit%; /else /relimit%; /endif
