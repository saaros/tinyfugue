;;; Lisp-like macros
; These macros return values via /echo.  Use $() command subs to catch output.

/~loaded lisp.tf

/def -i car = /echo - %1
/def -i cdr = /echo - %-1
/def -i cadr = /echo - %2
/def -i cddr = /echo - %-2
/def -i caddr = /echo - %3
/def -i cdddr = /echo - %-3

/def -i length = /echo %#

/def -i reverse = \
    /let result=%1%;\
    /while (shift(), {#}) \
        /let result=%1 %result%;\
    /done%;\
    /echo - %result

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
    /echo - %{result}


;; Remove all duplicate items from list.
;; Note that the speed of this macro is O(N^2), so it is very slow on
;; long lists.

;/def -i unique = \
;    /if ( {#} > 1 ) \
;        /echo - %1 $(/unique $(/remove %1 %-1))%;\
;    /else \
;        /echo - %1%;\
;    /endif

/def -i unique = \
    /echo - %1 $[{#} > 1 ? $(/unique $(/remove %1 %-1)) : ""]

