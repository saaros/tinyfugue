;;;; Mapping.
;;;; This file implements mapping and movement ("speedwalking") commands
;;;; similar to those found in tintin.  Once mapping is enabled with /mark,
;;;; all movement commands (n,s,e,w,ne,sw,nw,se,u,d) will be remembered in
;;;; your "path".

;;;; usage:
;;; /map <dir>		Add <dir> to remembered path.
;;; /mark		Reset path and enable mapping.
;;; /path		Display remembered path.
;;; /return		Move in the opposite direction of the last remembered
;;;			  movement, and remove that last movement from the path.
;;; /savepath <name>	Create a macro <name> to execute the current path.
;;;			  Note: macro is not written to a file.
;;; /unpath		Remove the last movement from the path.
;;; /unmark		Disable maping.
;;; /dopath <path>	Execute <path>, where <path> is a space-separated list
;;;			  of commands with optional repeat counts.  E.g.,
;;;			  "/dopath 10 n 3 e d 2 w" will execute "n" 10
;;;			  times, "e" 3 times, "d" once, and "w" twice.

/~loaded map.tf

/set path=

/def -i mark = \
	/echo %% Will start mapping here.%;\
	/set path=%;\
	/def -i -mglob -h'send {n|s|e|w|ne|sw|ne|sw|u|d}' _map_hook = /map %%*

/def -i map	= /set path=%path %1

/def -i unmark	= /set path=%; /undef _map_hook%; /echo %% Mapping disabled.

/def -i path	= /echo %% Path: %path

/def -i savepath= /def -i %1 = /dopath %path

/def -i dopath	= \
    /if /test $[%1 != 0 && %# >= 2]%; /then \
        /for i 1 %1 %2%;\
        /dopath %-2%;\
    /elseif /test %#%; /then \
        %1%;\
        /dopath %-1%;\
    /endif

/def -i unpath	= /set path=$(/all_but_last %path)

/def -i return = \
	/let dir=$(/last %path)%;\
	/unpath%;\
	/_return_aux n s e w ne sw nw se u d%;

/def -i _return_aux = \
	/if /test %# == 0%; /then \
		/echo %% Don't know how to return from "%dir".%;\
		/set path=%path %dir%;\
	/elseif /test dir =~ "%1"%; /then %2%;\
	/elseif /test dir =~ "%2"%; /then %1%;\
	/else   /_return_aux %-2%;\
	/endif


/def -i all_but_last = /echo %-L
