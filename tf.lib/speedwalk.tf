;;;; Speedwalk
;;; "/speedwalk" toggles speedwalking.  With speedwalking enabled,
;;; you cay type multiple directions on a single line, similar to
;;; tintin.  Lowercase letters are sent individually; if they are
;;; preceeded by a number, they will be repeated that many times.  Non-
;;; lowercase characters are not interpreted.  For example, with
;;; speedwalk enabled, typing "n2e3sNEWSes" will send to the mud "n",
;;; "e", "e", "s", "s", "s", "NEWS", "e", "s".


/~loaded speedwalk.tf

/eval \
    /def -i speedwalk = \
        /if /ismacro speedwalk_hook%%; /then \
            /echo %%% Speedwalk disabled.%%;\
            /undef speedwalk_hook%%;\
        /else \
            /echo %%% Speedwalk enabled.%%;\
            /def -iFp%{maxpri} -hsend speedwalk_hook = \
                /~do_speedwalk %%%*%%;\
        /endif

/def -i ~do_speedwalk = \
    /let args=%*%;\
    /let string=%;\
    /let count=%;\
    /let c=%;\
    /let i=-1%;\
    /while /test (c:=substr(args, (i:=i+1), 1)) !~ ""%; /do \
        /if /test c =/ "[0-9]"%; /then \
            /test count:= strcat(count, c)%;\
        /elseif /test regmatch("[nsewud]", c)%; /then \
            /if /test string !~ ""%; /then /send - %{string}%; /endif%;\
            /let string=%;\
            /for j 1 %{count-1} /~do_speedwalk_aux %{c}%;\
            /let count=%;\
        /else \
            /test string:= strcat(string, count, c)%;\
            /let count=%;\
        /endif%;\
    /done%;\
    /let string=%{string}%{count}%;\
    /if /test string !~ ""%; /then /send - %{string}%; /endif

/def -i ~do_speedwalk_aux = \
;   _map_hook may be defined if map.tf was loaded.
    /if /ismacro _map_hook%; /then \
        /_map_hook %*%;\
    /endif%;\
    /send - %*
