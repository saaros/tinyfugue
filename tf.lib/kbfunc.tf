;;; Extended keyboard functions.
;;; This file contains many functions found in modern shells (bash, tcsh,
;;; zsh, etc).  To use them, you must either /load kbbind.tf, or
;;; define your own bindings.

;;; /kb_backward_kill_line	delete from cursor to beginning of line
;;; /kb_kill_word		delete from cursor to end of punctuated word
;;; /kb_backward_kill_word	delete from cursor to start of punctuated word
;;; /kb_capitalize_word		capitialize current word
;;; /kb_downcase_word		convert current word to lowercase
;;; /kb_upcase_word		convert current word to uppercase
;;; /kb_transpose_chars		swap current character with previous character
;;; /kb_last_argument		insert last word of previous line
;;; /kb_expand_line		/eval and replace current line

/~loaded kbfunc.tf

/def -i kb_backward_kill_line = /test kbdel(0)

/def -i kb_kill_word = /test kbdel(kbwordright())
/def -i kb_backward_kill_word  = /test kbdel(kbwordleft())

/def -i kb_capitalize_word = \
    /let old_insert=%{insert}%;\
    /set insert=0%;\
    /test kbgoto(kbwordright()), kbgoto(kbwordleft()) %;\
    /test input(toupper(substr(kbtail(), 0, 1))) %;\
    /test kbgoto(kbwordright())%;\
    /set insert=%{old_insert}

/def -i kb_downcase_word = \
    /let old_insert=%{insert}%;\
    /set insert=0%;\
    /test input(tolower(substr(kbtail(), 0, kbwordright() - kbpoint()))) %;\
    /set insert=%{old_insert}

/def -i kb_upcase_word = \
    /let old_insert=%{insert}%;\
    /set insert=0%;\
    /test input(toupper(substr(kbtail(), 0, kbwordright() - kbpoint()))) %;\
    /set insert=%{old_insert}

/def -i kb_transpose_chars = \
    /if /test kbpoint() > 0 %; /then \
        /let old_insert=%{insert}%;\
        /set insert=0%;\
        /if /test kbpoint() == kblen()%; /then /dokey_left%; /endif%;\
        /dokey left%;\
        /test input(strcat(substr(kbtail(),1,1), substr(kbtail(),0,1))) %;\
        /set insert=%{old_insert}%;\
    /else \
        /beep 1%;\
    /endif

/def -i kb_last_argument = \
    /def -i ~kb_last_argument = /input %%L%;\
    /~kb_last_argument $(/recall -i -- -2)%;\
    /undef ~kb_last_argument

/def -i kb_expand_line = \
    /eval /grab $(/recall -i 1)

