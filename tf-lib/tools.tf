;;;; Miscellaneous useful macros.

/~loaded tools.tf
/require lisp.tf


; for compatibility
/def -i shl = /sys %*


;;; Re-edit
; Stick an existing macro definition in the input window for editing.
; syntax:  /reedit <macroname>

/def -i reedit = /grab $(/cddr $(/list -i - %{L-@}))


;;; name - change your name (on a TinyMUD style mud)
; syntax:  /name [<name>]

/def -i name =\
    @name me=%{1-${world_character}} ${world_password}


;;; getline - grab the nth line from history and stick it in the input buffer
; syntax:  /getline <n>

/def -i getline = /quote /grab #%{1}-%{1}


;;; xtitle - change the titlebar on an xterm.
; syntax:  /xtitle <text>

/def -i xtitle = /echo -r \033]2;%*\07
