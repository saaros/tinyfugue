;;;; Miscellaneous useful macros.

/~loaded tools.tf
/require lisp.tf

;;; /sys <command>
; Executes a shell command, without the annoying redraw or messages of /sh.
; Only works for commands that do not require input or redraw the screen.

/def -i sys = \
    /if /test %#%; /then \
        /quote -0 /echo -- !%{*}%;\
    /else \
        /sh%;\
    /endif

; for compatibility
/def -i shl = /sys %*


;;; Re-edit
; Stick an existing macro definition in the input window for editing.
; syntax:  /reedit <macroname>

/def -i reedit = /grab $(/cddr $(/list - %{L-@}))


;;; name - change your name (on a TinyMUD style mud)
; syntax:  /name [<name>]

/def -i name =\
    /let name=%1%;\
    /eval @name me=%%{name-${world_character}} $${world_password}


;;; getline - grab the nth line from history and stick it in the input buffer
; syntax:  /getline <n>

/def -i getline = /quote /grab #%{1}-%{1}


;;; xtitle - change the titlebar on an xterm.
; syntax:  /xtitle <text>

/def -i xtitle = /echo \033]2;%*\07
