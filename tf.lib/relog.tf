;;; relog - recall into a log
; syntax:  /relog <file> <recall_arguments>
; Starts logging, and silently performs a /recall into the log file.

/~loaded relog.tf

/def -i relog = \
    /def -i -hprocess -1 -agG =\
        /def -i -hlog -1 -ag = \
            /echo %%%% Recalling to log file %1%%;\
        /log %1%;\
    /quote -0 /_relog %1 #%-1%;\

/def -i _relog = \
    /def -i -hprocess -1 = \
        /unset _ARGS%;\
    /setenv _ARGS=%-1%;\
    /quote -0 !echo "$$_ARGS" >> %1

