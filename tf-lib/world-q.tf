;;;; Active worlds
;;; If you like to connect to a lot of worlds at the same time, you may find
;;; these macros useful.

;;; Whenever activity occurs in a background world, these macros will add
;;; the name of that world to a queue.  Then, when you type ``ESC w'', they
;;; will switch to to the first world on the queue.  So by typing ``ESC w''
;;; repeatedly, you can visit all your active worlds.  If the queue is
;;; empty, ``ESC w'' will switch you to the last world you visited.

/~loaded world-q.tf

/require stack-q.tf
/require lisp.tf

/def -ib'^[w' = /to_active_or_prev_world

/def -iFp1 -h"ACTIVITY" activity_queue_hook = \
    /enqueue %1 active_worlds

; don't queue world "rwho".
/def -ip2 -msimple -h"ACTIVITY rwho" activity_rwho_hook

/def -iFp1 -h"WORLD" prev_world_hook =\
    /set prev_world=%{fg_world}%;\
    /set fg_world=${world_name}%;\
    /if (fg_world !~ "") \
        /set active_worlds=$(/remove %{fg_world} %{active_worlds})%; \
    /endif

; don't remember world "rwho".
/def -ip2 -msimple -h"WORLD rwho" prev_rwho_hook

/def -i to_active_world = \
    /if ( active_worlds =~ "" ) \
        /echo -e %% No active worlds.%;\
    /else \
        /fg $(/dequeue active_worlds)%;\
    /endif

/def -i to_active_or_prev_world = \
    /if ( active_worlds !~ "" ) \
        /fg $(/dequeue active_worlds)%;\
    /elseif ( prev_world !~ "" ) \
        /fg -s %{prev_world}%;\
    /endif

/def -i list_active_worlds = \
    /if ( active_worlds =~ "" ) \
        /echo -e %% No active worlds.%;\
    /else \
        /echo -e %% Active worlds:  %{active_worlds}%;\
    /endif

