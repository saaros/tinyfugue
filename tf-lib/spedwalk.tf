;;;; Speedwalk
;;;; "/speedwalk" toggles speedwalking.  With speedwalking enabled, you can
;;;; type multiple directions on a single line, similar to tintin.  Any line
;;;; containing only numbers and the letters "n", "s", "e", "w", "u", and
;;;; "d" are interpreted by speedwalk; other lines are left alone (of course,
;;;; to guarantee that lines don't get interpreted, you should turn speedwalk
;;;; off).  Each letter is sent individually; if it is preceeded by a number,
;;;; it will be repeated that many times.  For example, with speedwalk
;;;; enabled, typing "ne3ses" will send "n", "e", "s", "s", "s", "e", "s".


/loaded __TFLIB__/spedwalk.tf

/eval \
    /def -i speedwalk = \
        /if /ismacro ~speedwalk%%; /then \
            /echo -e %%% Speedwalk disabled.%%;\
            /undef ~speedwalk%%;\
        /else \
            /echo -e %%% Speedwalk enabled.%%;\
;           NOT fallthru, so _map_send in map.tf won't catch it too.
            /def -ip%{maxpri} -mregexp -h'send ^[nsewud0-9]+$$$' ~speedwalk = \
                /~do_speedwalk %%%*%%;\
        /endif

/def -i ~do_speedwalk = \
    /let args=%*%;\
    /let string=%;\
    /let count=%;\
    /let c=%;\
    /let i=-1%;\
    /while ( (c:=substr(args, ++i, 1)) !~ "" ) \
        /if ( c =/ "[0-9]" ) \
            /@test count:= strcat(count, c)%;\
        /elseif ( regmatch("[nsewud]", c) ) \
            /if ( string !~ "" ) /send - %{string}%; /endif%;\
            /let string=%;\
            /for j 1 %{count-1} /~do_speedwalk_aux %{c}%;\
            /let count=%;\
        /else \
            /@test string:= strcat(string, count, c)%;\
            /let count=%;\
        /endif%;\
    /done%;\
    /let string=%{string}%{count}%;\
    /if ( string !~ "" ) /send - %{string}%; /endif

/def -i ~do_speedwalk_aux = \
;   _map_hook may be defined if map.tf was loaded.
    /if /ismacro _map_hook%; /then \
        /_map_hook %*%;\
    /endif%;\
    /send %*
