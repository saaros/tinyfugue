;;; Lisp-like macros
; These macros return values via /echo.  Use $() command subs to catch output.

/~loaded lisp.tf

/def -i car = /echo -- %1
/def -i cdr = /echo -- %-1
/def -i cadr = /echo -- %2
/def -i cddr = /echo -- %-2
/def -i caddr = /echo -- %3
/def -i cdddr = /echo -- %-3

/def -i length = /echo %#

/def -i reverse = \
    /if /test %# > 0%; \
    /then \
        /echo -- $(/reverse %-1) %1%;\
    /endif

/def -i mapcar = \
    /if /test %# > 1%; \
    /then \
        /eval %1 %2%;\
        /mapcar %1 %-2%;\
    /endif

/def -i maplist = \
    /if /test %# > 1%; \
    /then \
        /eval %1 %-1%;\
        /maplist %1 %-2%;\
    /endif
