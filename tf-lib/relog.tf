;;; relog - recall into a log
; syntax:  /relog <file> <recall_arguments>
; Starts logging, and silently performs a /recall into the log file.

/loaded __TFLIB__/relog.tf

/def -i relog = \
    /def -i -hlog -1 -ag = /echo %%% Recalling to log file %1%;\
    /log %1%;\
    /quote -S /_relog %1 #%-1%; \
    /log off

/def -i _relog = \
    /test fwrite({1}, {-1})

