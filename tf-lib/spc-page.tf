;;;; space-page
;; This set of commands allows the use of the SPACE key to scroll at a
;; --More-- prompt, like tf versions prior to 2.0.  The TAB key also works.
;; I personally don't like it, but you might if you can't get the hang of
;; pressing TAB.  To enable space-page, just load this file.

/loaded __TFLIB__/spc-page.tf

/def -i pager = \
    /purge -ib" "%; \
    /dokey page

/def -i -arh -hMORE = \
    /def -i -b" " = /pager

; This part is so TAB still works.
/if /ismacro -mglob -ib"^I" = /dokey page%; /then \
    /purge -ib"^I"%;\
    /def -i -b"^I" = /pager%;\
/endif
