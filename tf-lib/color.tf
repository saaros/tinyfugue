;;;; popular color definitions
;;; I provided extended ANSI codes because they're the most common.
;;; The user can of course redefine them (in another file).

;; Code 37;40 sets white on black, and 30;47 sets black on white; we have
;; no way to know which are the terminal's normal colors.  Code 0 resets
;; to default colors on some terminals, but not all.  Code 0 turns off
;; simple attributes on all terminals.  For terminals where 0 resets
;; colors, if 0 is the last code, it doesn't matter what codes are used
;; before the 0.  For terminals where 0 doesn't reset color, we have to
;; pick a color and set it; a 0 anywhere in the string will have no
;; effect.  So, 37;40;0 will work for 0-reset terminals and white on black
;; non-0-reset terminals.

;; For terminals where 0 resets color, and/or normal is white on black.
/set end_color  		\033[37;40;0m
;; For terminals where 0 resets color, and/or normal is black on white.
; /set end_color  		\033[30;47;0m


/set start_color_black		\033[30m
/set start_color_red		\033[31m
/set start_color_green		\033[32m
/set start_color_yellow		\033[33m
/set start_color_blue		\033[34m
/set start_color_magenta	\033[35m
/set start_color_cyan		\033[36m
/set start_color_white		\033[37m

/set start_color_bgblack	\033[40m
/set start_color_bgred		\033[41m
/set start_color_bggreen	\033[42m
/set start_color_bgyellow	\033[43m
/set start_color_bgblue		\033[44m
/set start_color_bgmagenta	\033[45m
/set start_color_bgcyan		\033[46m
/set start_color_bgwhite	\033[47m


;; For some reason, aixterm "white" appears grey; adding 60 gives true white.
/if ( TERM =~ "aixterm" ) \
    /set start_color_white	\\033[97m%; \
    /set start_color_bgwhite	\\033[107m%; \
;;  white on black
    /set end_color  		\\033[97;40;0m%; \
;;  black on white
;   /set end_color  		\\033[30;107;0m%; \
/endif


; This group is set up for 16 colors on xterms.
; Colors 0-7 correspond to the 8 named foreground colors above.  The named
; color variables override the numbered variables below, so to use numbered
; variables 0-7 you must unset the named variables (or reset them to
; the codes below).

/set start_color_0		\033[200m
/set start_color_1		\033[201m
/set start_color_2		\033[202m
/set start_color_3		\033[203m
/set start_color_4		\033[204m
/set start_color_5		\033[205m
/set start_color_6		\033[206m
/set start_color_7		\033[207m
/set start_color_8		\033[208m
/set start_color_9		\033[209m
/set start_color_10		\033[210m
/set start_color_11		\033[211m
/set start_color_12		\033[212m
/set start_color_13		\033[213m
/set start_color_14		\033[214m
/set start_color_15		\033[215m


; Simple commands to disable/enable color.  Resetting status_fields forces
; a redraw of the status line (with the new colors).

/purge -i -mregexp ^color_(on|off)$

/def -i color_on = \
    /load -q %TFLIBDIR/color.tf%; \
    /set status_fields=%status_fields

/def -i color_off = \
    /quote -S /unset `/listvar -s start_color_*%; \
    /unset end_color%; \
    /set status_fields=%status_fields

