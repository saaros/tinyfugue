;;; prefix/suffix for mud commands

;; usage:
;; /pfxrand			- randomize *fixes
;; /pfxon [-w<world>]		- enable *fixes	on <world>
;; /pfxoff [-w<world>]		- disable *fixes on <world>
;; /pcmd [-w<world>] <cmd>	- execute *fixed <cmd> on <world>

;; It is okay to issue multiple /pcmd commands without worrying that their
;; triggers will interfere with each other, because a unique prefix and
;; suffix is generated each time.

;; Example: /silent_foobar executes the command "foobar" on the mud, and gags
;; all output of the command if the command works, but lets the "foobar failed"
;; message through if if fails.  Either way, when the command is done,
;; the triggers are cleaned up.
;;
;; /def silent_foobar =\
;;   /def -1 -ag -p5009 -t"%{outputprefix}" =\
;;     /def -p5001 -t"foobar failed" foobar_fail%%;\
;;     /def -ag -p5000 -t"*" foobar_gag%;\
;;   /def -1 -ag -p5009 -t"%{outputsuffix}" =\
;;     /undef foobar_gag%%;\
;;     /undef foobar_fail%;\
;;   /pcmd foobar %1

;; Programmer's note: the /send commands here deliberately do not have a
;; leading "-", because we want the -w<world> option to be interpreted.


/loaded __TFLIB__/pcmd.tf

/def -i pfxrand = \
    /set outputprefix=<pre:%{_pfx_counter}:$[rand()]>%;\
    /set outputsuffix=<suf:%{_pfx_counter}:$[rand()]>

/pfxrand
/set _pfx_counter=1

/def -i pfxon = \
    /send %* - OUTPUTPREFIX %{outputprefix}%;\
    /send %* - OUTPUTSUFFIX %{outputsuffix}

/def -i pfxoff = \
    /send %* - OUTPUTPREFIX%;\
    /send %* - OUTPUTSUFFIX

/def -i pcmd = \
    /let _opts=%; \
    /while ( {1} =/ "-[^- ]*" ) \
        /let _opts=%_opts %1%; \
        /shift%; \
    /done%; \
    /pfxon %{_opts}%; \
    /send %{_opts} %*%; \
    /pfxoff %{_opts}%; \
    /@test _pfx_counter := _pfx_counter + 1%; \
    /pfxrand

