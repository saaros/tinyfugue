;; simple grep

/loaded __TFLIB__/grep.tf

; Note: users should not rely on %_loaded_libs like this.  I can get away
; with this here only because this and /loaded are internal to TF.
/if (_loaded_libs =/ "*{__TFLIB__/textutil.tf}*") \
    /echo -e %% Warning: textutil.tf and grep.tf have conflicting defintions.%;\
/endif


/def -i _fgrep = \
    /@test (strstr({*}, pattern) >= 0) & echo({*})

/def -i fgrep = \
    /let pattern=%1%; \
    /quote -S /_fgrep `%-1


;; glob grep

/def -i _grep = \
    /@test ({*} =/ pattern) & echo({*})

/def -i grep = \
    /let pattern=%1%; \
    /quote -S /_grep `%-1


;; regexp grep

/def -i _egrep = \
    /@test regmatch(pattern, {*}) & echo({*})

/def -i egrep = \
    /let pattern=%1%; \
    /quote -S /_egrep `%-1

