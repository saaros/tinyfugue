;;;; Tintin emulation macros.
;;; If you're converting to tf from tintin, you might find this useful.
;;; This file emulates most of the commands in tintin++, although some
;;; commands will behave somewhat differently.
;;; These commands have not been fully tested.


/~loaded tintin.tf

;;; Some useful stuff is stored in other files.
/require tick.tf
/require alias.tf
/require speedwalk.tf
/require map.tf

/def -i action	= /trig %*
;/alias		(see alias.tf)
/def -i antisubstitute = /def -p9999 -t'$(/escape ' %*)'
/def -i all	= /send -W -- %*
/def -i bell	= /beep %*
/def -i boss	= /@echo Not implemented.
/def -i char	= /@echo Not implemented.
;/def echo	= /toggle mecho%; /: The name "/echo" conflicts with tf builtin.
/def -i end	= /quit
;/gag		builtin
;/help		builtin
/def -i highlight	= /hilite %*
/def -i history	= /recall -i %{1-15}
;/if		builtin
/def -i ignore	= /toggle borg%; /set borg
;/log		builtin
;/loop		(see /for)
;/map		(see map.tf)
;/mark		(see map.tf)
/def -i math	= /test %1 := %-1
/message	= /@echo Not implemented; use hooks with gags.
/def -i -mregexp -p2 -h'send ^#([0-9]+) (.*)$' #rep_hook = /repeat %P1 %P2
/def -i nop	= /:
;path		(see map.tf)
/def -i presub	/@echo Not implemented; use gags with or without -F flag.
;/redraw	not needed (always on)
;return		(see map.tf)
/def -i read	= /load %*
;savepath	(see map.tf)

/def -i session	= \
	 	/if /test %#%; /then \
			/if /addworld %*%; /then /world %1%; /endif%;\
		/else \
			/listsockets%;\
		/endif%\
		/def %1 = \
			/if /test %%#%; /then \
				/send -w%1 %%*%;\
			/else \
				/world %1%;\
			/endif

/def -i showme	= /@echo %*
/def -i snoop	= \
		/if /ismacro _snoop_%1%; /then \
			/@echo %% Snooping %1 disabled.%;\
			/undef _snoop_%1%;\
			/undef _snoopbg%1%;\
		/else \
			/@echo %% Snooping %1 enabled.%;\
			/def -i -w%1 -hbackground -ag _snoopbg_%1%;\
			/def -i -t* -p9999 -w%1 -ag -F _snoop_%1 = \
				/@echo $${world_name}: %%*%;\
		/endif

;/speedwalk	(see speedwalk.tf)
/def -i split	= /isize %{1-3}%; /visual on
/substitute	/@echo Not implemented.  Use gag and /echo.
/def -i system	= /sh %*
;/togglesubs	(no equiv)
/def -i unaction	= /untrig %*
;/unalias	(see alias.tf)
;/unantisub	(no equiv)
/def -i ungag	= /nogag %*
/def -i unhighlight	= /nohilite %*
;unpath		(see map.tf)
/def -i unsplit	= /visual off
;/unsubs	(no equiv)
/def -i unvariable	= /unset %*
/def -i variable	= /set %*
/def -i verbatim	= /toggle sub
;/version	builtin
/def -i wizlist	= /help author
/def -i write	= /if /test %# == 1%; /then \
			/save %1%;\
		/else \
			/@echo %% usage: /write <filename>%;\
		/endif

/def -i zap	= /dc %*

