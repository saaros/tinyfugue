;;;; factorial macros
;;; Not very useful, but they do demonstrate some macro programming techniques.

;; recursive factorial

/def rfact = \
    /if /test $[%1 < 0]%; /then \
        /echo %% factorial: negative argument%; \
    /elseif /test $[%1 == 0]%; /then \
        /echo 1%; \
    /else \
        /eval /echo -- $$[%1 * $(/rfact $[%1 - 1])]%; \
    /endif

;; iterative factorial - much more efficient

/def ifact = \
    /if /test $[%1 < 0]%; /then \
        /echo %% factorial: negative argument%; \
    /else \
        /let n=%1%; \
        /let result=1%; \
        /while /test n%; /do \
            /let result=$[result * n]%; \
            /let n=$[n - 1]%; \
        /done%; \
        /echo -- %result%; \
    /endif%; \
