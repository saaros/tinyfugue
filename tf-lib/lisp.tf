;;; Lisp-like macros
; These macros return values via /result, so they can be used in $() command
; subs, or as functions in expressions.

/~loaded lisp.tf

/def -i car = /result {1}
/def -i cdr = /result {-1}
/def -i cadr = /result {2}
/def -i cddr = /result {-2}
/def -i caddr = /result {3}
/def -i cdddr = /result {-3}

/def -i length = /result {#}

/def -i reverse = \
    /let result=%1%;\
    /while (shift(), {#}) \
        /let result=%1 %result%;\
    /done%;\
    /result result

/def -i mapcar = \
    /let cmd=%1%; \
    /while (shift(), {#}) \
        /eval %cmd %%1%; \
    /done

/def -i maplist = \
    /let cmd=%1%;\
    /while (shift(), {#}) \
        /eval %cmd %%*%;\
    /done

/def -i remove = \
    /let word=%1%;\
    /let result=%;\
    /while (shift(), {#}) \
        /if (word !~ {1}) \
            /let result=%{result} %{1}%;\
        /endif%;\
    /done%;\
    /result result


;; Remove all duplicate items from list.
;; Note that the speed of this macro is O(N^2), so it is very slow on
;; long lists.

/def -i unique = \
    /let result=%1 $[{#}>1 ? $(/unique $(/remove %1 %-1)) : ""]%; \
    /result result

