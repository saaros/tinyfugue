;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; TinyFugue - programmable mud client
;;;; Copyright (C) 1998-2002 Ken Keys
;;;;
;;;; TinyFugue (aka "tf") is protected under the terms of the GNU
;;;; General Public License.  See the file "COPYING" for details.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;; status bar definitions and utilities

/loaded tfstatus.tf


/set clock_format=%H:%M

; disable warnings
/set warn_status=0

/set status_int_more \
     (!limit() & moresize() == 0) ? "" : status_int_more()
/def -i status_int_more = \
    /let new=$[moresize("ln")]%; \
    /let old=$[moresize("l") - new]%; \
    /if (old >= 1000) /let old=$[old/1000]k%; /endif%; \
    /if (old) \
	/if (new >= 1000) /let new=$[new/1000]k%; /endif%; \
	/result pad(limit() ? "L" : " ", 1,  old, 3,  "+", 1,  new, 3)%; \
    /else \
	/if (new >= 10000) /let new=$[new/1000]k%; /endif%; \
	/result pad(limit() ? "LIM " : "More", 4,  new, 4)%; \
    /endif

/set status_int_world   \
     ${world_name} =~ "" ? "(no world)" : \
     strcat(!is_open() ? "!" : "",  ${world_name})
/set status_int_read    nread() ? "(Read)" : ""
/set status_int_active  nactive() ? pad("(Active:", 0, nactive(), 2, ")") : ""
/set status_int_log     nlog() ? "(Log)" : ""
/set status_int_mail \
    !nmail() ? "" : \
    nmail()==1 ? "(Mail)" : \
    pad("Mail", 0, nmail(), 2)
/set status_var_insert  insert ? "" : "(Over)"
/set status_int_clock   ftime(clock_format)

/set status_field_defaults \
    @more:8:Br :1 @world :1 \
    @read:6 :1 @active:11 :1 @log:5 :1 @mail:6 :1 insert:6 :1 \
    kbnum:4 :1 @clock:5
/eval /set status_fields=%status_field_defaults

; re-enable warnings
/set warn_status=1


;;; /status_add [options] field[:width[:attrs]]
;; add argument to %status_fields.
;; -A[<name>]   add after field <name>, or at end
;; -B[<name>]   add before field <name>, or at beginning
;; -s<N>        insert spacer <N> (default 1)
;; -x           don't add if already present
;; If neither -A nor -B is given, -A is assumed.
/def -i status_add = \
    /let opt_A=:%; \
    /let opt_B=:%; \
    /let opt_s=1%; \
    /let opt_x=0%; \
    /if (!getopts("A:B:s#x")) /return 0%; /endif%; \
    /let new=%*%; \
    /let space=$[opt_s > 0 ? strcat(" :", +opt_s) : ""]%; \
    /if (opt_x) \
	/let regexp=(^| +)%new(:\\d*(:[\\w,]*)?)?( +|$$)%; \
	/if (regmatch(regexp, status_fields)) \
	    /return 0%; \
	/endif%; \
    /endif%; \
    /let old_warn_status=%warn_status%; \
    /set warn_status=0%; \
    /if (opt_B =~ "") \
	/set status_fields=%1%space %status_fields%; \
    /elseif (opt_B !~ ":") \
	/let regexp=(^| +)(%opt_B(:\\d*(:[\\w,]*)?)?)( +|$$)%; \
	/if (regmatch(regexp, status_fields)) \
	    /set status_fields=%PL%space %new %P2 %PR%; \
	/endif%; \
    /elseif (opt_A =~ "" | opt_A =~ ":") \
	/set status_fields=%status_fields%space %1%; \
    /else \
	/let regexp=(^| +)(%opt_A(:\\d*(:[\\w,]*)?)?)( +|$$)%; \
	/if (regmatch(regexp, status_fields)) \
	    /set status_fields=%PL %P2%space %new %PR%; \
	/endif%; \
    /endif%; \
    /set warn_status=%old_warn_status

;;; /status_rm field
;; remove field from status_fields
/def -i status_rm = \
    /let regexp=(^| +)(:(\\d*) +)?%1(:\\d*(:[\\w,]*)?)?( +:(\\d*))?($$| +)%; \
    /if (regmatch(regexp, status_fields)) \
	/let old_warn_status=%warn_status%; \
	/set warn_status=0%; \
	/if ({P1} !~ "" & {P8} !~ "" & ({P3} | {P7})) \
;	    keep the larger of the two neighboring spacers
	    /set status_fields=%PL :$[({P3} > {P7}) ? {P3} : {P7}] %PR%; \
	/else \
	    /set status_fields=%PL %PR%; \
	/endif%; \
	/set warn_status=%old_warn_status%; \
    /endif

;;; /status_edit field[:width[:attrs]]
;; replace existing field with argument
/def -i status_edit = \
    /if (regmatch("^([^: ]+)(:\\d*(:[\\w,]*)?)?$", {*})) \
	/let label=%P1%; \
	/let regexp=(^| )%label(:\\d*(:[\\w,]*)?)?($$| )%; \
	/if (regmatch(regexp, status_fields)) \
	    /let old_warn_status=%warn_status%; \
	    /set warn_status=0%; \
	    /set status_fields=%PL%P1%*%P4%PR%; \
	    /set warn_status=%old_warn_status%; \
	/else \
	    /echo -e %% No field matches "%label".%; \
	/endif%; \
    /else \
	/echo -e %% Invalid field "%*".%; \
    /endif

/def -i clock = \
    /if ({*} =/ "{0|off|no}") \
	/status_rm @clock%; \
    /else \
	/if ({*} !/ "{1|on|yes}") \
	    /set clock_format=%*%; \
	/endif%; \
	/let width=$[strlen(ftime(clock_format))]%; \
	/if (regmatch("(^|\\s+)@clock(:[^: ]+(:[^ ]+)?)?\\b", status_fields)) \
	    /status_edit @clock:%width%P3%; \
	/else \
	    /status_add -A -x @clock:%width%; \
	/endif%; \
    /endif

