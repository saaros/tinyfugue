;;;; Speedwalk
;;; "/speedwalk" toggles speedwalking.  With speedwalking enabled,
;;; you cay type multiple directions on a single line, similar to
;;; tintin.  Lowercase letters are sent individually; if they are
;;; preceeded by a number, they will be repeated that many times.  Non-
;;; lowercase characters are not interpreted.  For example, with
;;; speedwalk enabled, typing "n2e3sNEWSes" will send to the mud "n",
;;; "e", "e", "s", "s", "s", "NEWS", "e", "s".


/~loaded speedwalk.tf

/def -i speedwalk = \
    /if /ismacro speedwalk_hook%; /then \
        /echo %% Speedwalk disabled.%;\
        /undef speedwalk_hook%;\
    /else \
        /echo %% Speedwalk enabled.%;\
        /def -i -ag -hsend speedwalk_hook = \
            /~do_speedwalk %%*%;\
    /endif

/def -i ~do_speedwalk = \
    /let args=%*%;\
    /while /test args !~ ""%; /do \
        /test regmatch("([0-9]*)([a-z]?)(([0-9]*[^0-9a-z])*)(.*)", args)%;\
        /if /test $[%P2 !~ ""]%; /then \
            /if /test $[%P1 =~ ""]%; /then \
                /send -- %P2%;\
            /else \
                /for i 1 %P1 /send -- %P2%;\
            /endif%;\
            /if /test $[%P3 !~ ""]%; /then \
                /send -- %P3%;\
            /endif%;\
        /else \
            /send -- %{P1}%{P3}%;\
        /endif %;\
        /let args=%P5%;\
    /done
