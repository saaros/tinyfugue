;;;; Active worlds
;;; If you like to connect to a lot of worlds at the same time, you may find
;;; these macros useful.

;;; Whenever activity occurs in a background world, these macros will add
;;; the name of that world to a queue.  Then, when you type ``ESC w'', they
;;; will switch to to the first world on the queue.  So by typing ``ESC w''
;;; repeatedly, you can visit all your active worlds.  If the queue is
;;; empty, ``ESC w'' will switch you to the last world you visited.

/~loaded worldqueue.tf

/require stack_queue.tf

/def -ib'^[w' = /to_active_or_prev_world

/def -i -h"ACTIVITY" activity_queue_hook = \
    /enqueue %1 active_worlds

/def -i -h"WORLD" prev_world_hook =\
    /set prev_world=%{current_world}%;\
    /set current_world=${world_name}

/def -i to_active_world = \
    /let world=$(/dequeue active_worlds)%; \
    /if /test world =~ ""%; /then \
        /echo %% No active worlds.%;\
    /else \
        /world %{world}%;\
    /endif

/def -i to_active_or_prev_world = \
    /let old=%{more}%;\
    /set more=0%;\
    /set more=%{old}%;\
    /if /test "%{active_worlds}" !~ ""%; /then \
        /to_active_world%;\
    /else \
        /world %{prev_world}%;\
    /endif

/def -i list_active_worlds = \
    /if /test "%{active_worlds}" =~ ""%; /then \
        /echo %% No active worlds.%;\
    /else \
        /echo %% Active worlds:  %{active_worlds}%;\
    /endif

