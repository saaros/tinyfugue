;;; /watch
; Watch for people to connect to a mud.
; Requires that the mud have a WHO command that lists one player per line,
; and OUTPUTPREFIX and OUTPUTSUFFIX commands.
;
; Usage:
; /watch <player>	Tells you when <player> logs on to the mud.
; /watch		Tells you who you are still watching for.
; /unwatch <player>	Stops looking for <player>.
; /unwatch -a		Stops looking for everyone.
;
; This version polls for all names with a single WHO, unlike previous
; versions which did a separate WHO for each name being watched.
;
; Written by David Moore ("OliverJones").


/loaded __TFLIB__/watch.tf
/require pcmd.tf

;;; Global variables:
;/set watch_pid
;/set watch_list
;/set watch_glob

/def -i watch = \
    /let who=$[tolower(%1)]%;\
    /if (who =~ "") \
	/echo \% You are watching for: %{watch_list}%;\
	/break%;\
    /endif%;\
    /if (who =/ watch_glob) \
	/echo \% You are already watching for that person!%;\
	/break%;\
    /endif%;\
    /if (watch_pid =~ "") \
	/repeat -60 1 /_watch%;\
	/set watch_pid=%?%;\
    /endif%;\
    /set watch_list=%{who}|%{watch_list}%;\
    /set watch_list=$(/replace || | %{watch_list})%;\
    /set watch_glob={%{watch_list}}

/def -i unwatch =\
    /let who=$[tolower(%1)]%;\
    /if (who =~ "") \
	/echo \% Use /unwatch <name> or /unwatch -a for all.%;\
	/break%;\
    /endif%;\
    /if ((who !~ "-a") & (who !/ watch_glob)) \
	/echo \% You already weren't watching for that person!%;\
	/break%;\
    /endif%;\
    /if (who =~ "-a") \
	/set watch_list=|%;\
    /else \
	/set watch_list=$(/replace %{who}| | %{watch_list})%;\
	/set watch_list=$(/replace || | %{watch_list})%;\
    /endif%;\
    /set watch_glob={%{watch_list}}%;\
    /if ((watch_list =~ "|") & (watch_pid !~ "")) \
	/kill %{watch_pid}%;\
	/unset watch_pid%;\
    /endif

/def -i _watch =\
    /unset watch_pid%;\
    /def -i -p100 -1 -aGg -msimple -t"%{outputprefix}" _watch_start =\
	/def -i -p100 -aGg -mglob -t"*" _watch_ignore =%%;\
	/def -i -p101 -aGg -mglob -t"%{watch_glob}*" _watch_match =\
	    /echo # %%%1 has connected.%%%;\
	    /unwatch %%%1%%;\
	/def -i -p101 -1 -aGg -msimple -t"%{outputsuffix}" _watch_end =\
	    /undef _watch_ignore%%%;\
	    /undef _watch_match%%%;\
	    /if (watch_list !~ "|") \
		/repeat -60 1 /_watch%%%;\
		/set watch_pid=%%%?%%%;\
	    /endif%;\
    /pcmd WHO


