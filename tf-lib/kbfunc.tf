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

/def -i dokey_bspc	= /@test kbdel(kbpoint() - 1)
/def -i dokey_bword	= /@test regmatch("[^ ]* *$$", kbhead()), \
			        kbdel(kbpoint() - strlen({P0}))
/def -i dokey_dch	= /@test kbdel(kbpoint() + 1)
/def -i dokey_deol	= /@test kbdel(kblen())
/def -i dokey_dline	= /@test kbgoto(0), kbdel(kblen())
/def -i dokey_down	= /@test kbgoto(kbpoint() + wrapsize)
/def -i dokey_dword	= /@test kbdel(kbwordright())
/def -i dokey_end	= /@test kbgoto(kblen())
/def -i dokey_home	= /@test kbgoto(0)
/def -i dokey_left	= /@test kbgoto(kbpoint() - 1)
/def -i dokey_lnext	= /dokey lnext
/def -i dokey_newline	= /dokey newline
/def -i dokey_recallb	= /dokey recallb
/def -i dokey_recallbeg	= /dokey recallbeg
/def -i dokey_recallend	= /dokey recallend
/def -i dokey_recallf	= /dokey recallf
/def -i dokey_redraw	= /dokey redraw
/def -i dokey_right	= /@test kbgoto(kbpoint() + 1)
/def -i dokey_searchb	= /dokey searchb
/def -i dokey_searchf	= /dokey searchf
/def -i dokey_socketb	= /fg -<
/def -i dokey_socketf	= /fg ->
/def -i dokey_up	= /@test kbgoto(kbpoint() - wrapsize)
/def -i dokey_wleft	= /@test kbgoto(kbwordleft())
/def -i dokey_wright	= /@test kbgoto(kbwordright())
/def -i dokey_page	= /test morescroll(lines() - (visual?isize:0) - 1)
/def -i dokey_hpage	= /test morescroll((lines() - (visual?isize:0) - 1) / 2)
/def -i dokey_line	= /test morescroll(1)
/def -i dokey_flush	= /dokey flush
/def -i dokey_selflush	= /dokey selflush


/def -i kb_backward_kill_line = /@test kbdel(0)

/def -i kb_kill_word = /@test kbdel(kbwordright())

/def -i kb_backward_kill_word  = /@test kbdel(kbwordleft())

/def -i kb_capitalize_word = \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /@test kbgoto(kbwordright()), kbgoto(kbwordleft()) %;\
    /let end=$[kbwordright()]%;\
    /@test input(toupper(substr(kbtail(), 0, 1))) %;\
    /@test input(tolower(substr(kbtail(), 0, end - kbpoint()))) %;\
    /set insert=%{_old_insert}

/def -i kb_downcase_word = \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /@test input(tolower(substr(kbtail(), 0, kbwordright() - kbpoint()))) %;\
    /set insert=%{_old_insert}

/def -i kb_upcase_word = \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /@test input(toupper(substr(kbtail(), 0, kbwordright() - kbpoint()))) %;\
    /set insert=%{_old_insert}

/def -i kb_transpose_chars = \
    /if ( kbpoint() <= 0 ) /beep 1%; /return 0%; /endif%; \
    /let _old_insert=$[+insert]%;\
    /set insert=0%;\
    /if (kbpoint()==kblen()) /dokey_left%; /endif%;\
    /dokey_left%;\
    /@test input(strcat(substr(kbtail(),1,1), substr(kbtail(),0,1))) %;\
    /set insert=%{_old_insert}

/def -i kb_last_argument = \
    /input $(/last $(/recall -i - -2))

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
