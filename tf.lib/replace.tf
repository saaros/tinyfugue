;;;; Replace
;;; syntax:  /replace <old> <new> <string>
;;; Echoes <string>, with each occurance of <old> replaced by <new>.
;;; <old> and <new> must be single words.

;;; Example:
;;; command: /replace foo bar Whee foofle foobar
;;; output:  Whee barfle barbar

/~loaded replace.tf

/def -i replace = \
    /let old=%1%;\
    /let new=%2%;\
    /let left=%;\
    /let right=%-2%;\
    /while /test (i := strstr(right, old)) >= 0%; /do \
         /test left := strcat(left, substr(right, 0, i), new)%;\
         /test right := substr(right, i + strlen(old), 999999)%;\
    /done%;\
    /echo %{left}%{right}

