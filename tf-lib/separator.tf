;; this doesn't work right: activity hook occurs after the text is printed,
;; so separator appears AFTER first new line.
; /eval /def -Fp%maxpri -hactivity draw_separator = /echo -w -aG - ---

;; more complex, but works better... but incorrectly prints dashes even
;; if triggering line ends up being gagged.
;/eval /def -q -Fp%maxpri -t* draw_separator = \
;    /if (world_info("name") !~ fg_world() & nactive(world_info("name")) == 0) \
;	/echo -w -aG - ---%%; \
;    /endif

; New PREACTIVITY hook solves the problem with ACTIVITY hook
/eval /def -Fp%maxpri -hPREACTIVITY draw_separator = /echo -w -aG - ---
