;;;; Replace
;;; syntax:  /replace <old> <new> <string>
;;; Echoes <string>, with each occurance of <old> replaced by <new>.
;;; <old> and <new> must be single words.

;;; Example:
;;; command: /replace foo bar Whee foofle foobar
;;; output:  Whee barfle barbar

/~loaded replace.tf

/def -i replace = \
    /let old=%;\
    /let new=%;\
    /let left=%;\
    /let right=%;\
    /test old:={1}%;\
    /test new:={2}%;\
    /test right:={-2}%;\
    /while /let i=$[strstr(right, old)]%; /@test i >= 0%; /do \
         /@test left := strcat(left, substr(right, 0, i), new)%;\
         /@test right := substr(right, i + strlen(old))%;\
    /done%;\
    /result strcat(left, right)

