; finger - get user information.  Arguments are like unix finger:
; /finger user@host
; /finger user
; /finger @host
; /finger

; This is more complicated than it needs to be, just to make it act nicely.

; The empty WORLD hooks override normal hooks (like those in world-q.tf)
; that we don't want during finger.

/def -i finger = \
    /@test regmatch("^([^@]*)@?", {*})%; \
    /let user=%{P1}%; \
    /let host=%{PR-localhost}%; \
    /def -i _finger_exit = \
        /purge -i -msimple -h"CONNECT|CONFAIL|DISCONNECT {finger@%{host}}*"%%;\
        /def -1 -i -ag -msimple -h'WORLD ${world_name}' -p%{maxpri}%%; \
        /fg ${world_name}%%; \
        /unworld finger@%{host}%%; \
        /undef _finger_exit%; \
    /addworld finger@%{host} %{host} 79%; \
    /def -1 -i -ag -msimple -h'WORLD finger@%{host}' -p%{maxpri}%; \
    /def -1 -i -ag -mglob -h'CONNECT {finger@%{host}}*' -p%{maxpri} = \
        /fg finger@%{host}%%; \
        /send -- %{user}%; \
    /def -1 -i -ag -mglob -h'DISCONNECT {finger@%{host}}*' -p%{maxpri} = \
        /_finger_exit%; \
    /def -1 -i -mglob -h'CONFAIL {finger@%{host}}*' -p%{maxpri} = \
        /_finger_exit%; \
    /connect finger@%{host}

