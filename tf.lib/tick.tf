;;;; Tick counting
;;;; This file implements several tick counting commands similar to those
;;;; found in tintin, useful on Diku muds.  To use, just /load this file.

;;;; usage:
;;; /tick		Display the time remaining until next tick.
;;; /tickon		Reset and start the tick counter.
;;; /tickoff		Stop the tick counter.
;;; /tickset		Reset and start the tick counter.
;;; /ticksize <n>	Set the tick length to <n> seconds (default is 75).

/~loaded tick.tf

/set ticksize=75
/set next_tick=0
/set _tick_pid1=0
/set _tick_pid2=0

/def -i tick = \
	/if /test next_tick%; /then \
		/eval /echo %% $$[next_tick - $(/time @)] seconds until tick.%;\
	/else \
		/echo %% Tick counter is not running.%;\
	/endif

/def -i tickon = \
	/tickoff%;\
	/eval /set next_tick=$$[$(/time @) + ticksize]%;\
	/repeat -$[ticksize - 10] 1 \
		/set _tick_pid1=0%%;\
		/echo %%% Next tick in 10 seconds.%;\
        /set _tick_pid1=%%?%;\
	/repeat -%ticksize 1 \
		/set _tick_pid2=0%%;\
		/echo %%% TICK%%;\
		/tickon%;\
        /set _tick_pid2=%%?%;\

/def -i tickoff = \
	/if /test _tick_pid1%; /then /kill %_tick_pid1%; /endif%;\
	/if /test _tick_pid2%; /then /kill %_tick_pid2%; /endif%;\
	/set _tick_pid1=0%; \
	/set _tick_pid2=0%; \
	/set next_tick=0

/def -i tickset	= /tickon %*

/def -i ticksize	= /~setint ticksize %*
