;;; Aliases.
; Create command aliases.  Like simple macros, but no leading '/' is required.
; syntax:  /alias <name> <command...>
; syntax:  /unalias <name>
; Argument substitution works differently than you might expect:
; numbers refer to words on the command line, not words in the arguments.
; Therefore %1 is the name of the alias, %2 is the first arg, %3 the second,
; etc., and %-1 refers to all arguments.

/~loaded alias.tf

/def -i alias = \
    /if /test %# < 2%; /then \
        /list alias_%{1-*}%; \
    /else \
        /def -i -ag -mglob -h"send {%1}*" alias_%1 = %-1%; \
    /endif

/def -i unalias = /undef alias_%1

