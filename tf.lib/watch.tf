;;; /watch <player>
; Tells you when <player> logs on to the mud.
; Requires that the mud have a "WHO" command that lists one player per line.

/~loaded watch.tf
/require pcmd.tf

/def -i watch = \
    /def -i _watch_%1 = \
        /pcmd WHO %1%%;\
        /def -i -p100 -1 -ag -msimple -t"%%{outputprefix}" = \
            /def -i -p100 -ag -mglob -t"*" _watch_not_%1%%%;\
            /def -i -p101 -ag -mglob -t"{%1}*" _watch_match_%1 = \
                /echo # %1 has connected.%%%%;\
                /kill $$$${_watch_pid_%1}%%%%;\
                /undef _watch_%1%%%%;\
                /undef _watch_pid_%1%%%;\
            /def -i -p101 -1 -ag -msimple -t"%%{outputsuffix}" = \
                /undef _watch_not_%1%%%%;\
                /undef _watch_match_%1%;\
    /repeat -60 99999 /_watch_%1%;\
    /def -i _watch_pid_%1 = %?

