;;; spelling checker
;;; This lets you type ESC-s to check the spelling of the current line.
;;; If any misspellings are found, you will be told.
;;; This requires the "spell" utility on your local system.

/~loaded spell.tf

/def -i spell_line = \
    /setenv ARGS=$(/recall -i 1)%; \
    /let errs=$(/quote -S -decho !echo $$ARGS | spell)%; \
    /if ( errs !~ "" ) \
        /echo MISSPELLINGS: %errs%; \
    /else \
        /echo No misspellings found.%; \
    /endif%; \
    /@test errs =~ ""

/def -ib^[s = /spell_line

