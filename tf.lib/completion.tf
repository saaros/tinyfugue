;;;; Completion.
;; This allows you to type part of a word, hit a special key, and have
;; the rest of the word filled in for you automagically.  ESC TAB attempts
;; to complete based on context, or from a user defined list of words;
;; a few other bindings will do more explicit completions.
;;
;; To use:  /load this file, and store a list of words in %{completion_list}.
;; For example, add this to your ~/.tfrc file:
;;
;;    /load completion.tf
;;    /set completion_list=Hawkeye TinyFugue glia.biostr.washington.edu
;;
;; Completion keys:
;;
;; ESC TAB	from context, or %{completion_list}.
;; ESC ;	always uses %{completion_list}.
;; ESC /	filename completion.
;; ESC @	hostname completion.
;; ESC %	variable name completion.
;; ESC ^W	world name completion.

;; By "from context", I mean it will look for patterns and decide which
;; type of completion to use.  For example, if the line begins with "/load",
;; it will use filename completion; if the word begins with "%" or "%{", it
;; will use variable name completion; etc.

;; Optional user extensions.
;; To add your own completion, write a macro with a name like complete_foo
;; which takes the partial word as an argument, and calls /_complete_from_list
;; with the partial word and a list of possible matches.  Then bind a key
;; sequence that calls "/complete foo", and/or add a context
;; sensitive call in "/complete_context".

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/~loaded completion.tf

/require lisp.tf

/def -ib'^[^I'	= /complete
/def -ib'^[;'	= /complete user_defined
/def -ib'^[/'	= /complete filename
/def -ib'^[@'	= /complete hostname
/def -ib'^[%'	= /complete variable
/def -ib'^[^W'	= /complete worldname

; /def -ib'^[$'	= /complete macroname   (not implemented)
; /def -ib'^[~'	= /complete playername  (not implemented)
; /def -ib'^[`'	= /complete command     (not implemented)

;; /complete_playername is difficult to do correctly because of mud delays,
;; and is not portable to different muds, so it is not implemented here.


/def -i complete = \
    /unset _completion_suffix%;\
    /eval /complete_%{1-context} $$(/last $$[kbhead()])


;; /_complete_from_list <word> <list...>
;; <word> is the partial word to be completed.
;; <list> is a list of possible matches.
;; If <word> matches exactly one member of <list>, that member will be
;; inserted in the input buffer.  If multiple matches are found, the
;; longest common prefix will be inserted, and a list of matches will
;; be displayed.  If no matches are found, it simply beeps.
;; If exactly one match was found, %{_completion_suffix} or a space
;; will be appended to the completed word.

/def -i _complete_from_list = \
    /let _completion_pattern=%1%;\
    /shift%;\
    /let len=$[strlen(_completion_pattern)]%;\
    /let match=%;\
;   scan list for words which match _completion_pattern.
    /while /test %#%; /do \
        /if /test $[substr(%1, 0, len) =~ _completion_pattern]%; /then \
            /let match=%{match} %{1}%;\
        /endif%;\
        /shift%;\
    /done%;\
;   strip leading space
    /let match=$(/echo -- %{match})%;\
    /if /test match =~ ""%; /then \
;       No match was found.
        /beep 1%;\
    /elseif /test match !/ "{*}"%; /then \
;       Multiple matches were found.  Use longest common prefix.
        /beep 1%;\
        /let _prefix=$(/common_prefix %{len} %{match})%;\
        /test input(substr(_prefix, len, 999999))%;\
        /echo - %{match}%;\
    /else \
;       Exactly one match was found.
        /test match := strcat(match, _completion_suffix)%;\
        /if /test match =/ "*[A-Za-z0-9_]"%; /then \
            /test match := strcat(match, " ")%;\
        /endif%;\
        /test input(substr(match, len, 999999))%;\
    /endif%;\
    /unset _completion_suffix


/def -i complete_user_defined = \
    /_complete_from_list %1 %completion_list

/def -i complete_filename = \
    /quote -0 /_complete_from_list %1 !\
        echo `for f in \\\\`/bin/ls -d %1* 2>/dev/null\\\\`; do \
            test -d $$f && echo $$f/ || echo $$f; done`


/def -i complete_hostname = \
    /let target=$[substr(%1, strrchr(%1, "@") + 1, 999999)]%;\
    /quote -0 /_complete_from_list %{target} !\
        result=`cat /etc/hosts %{HOME}/etc/hosts 2>/dev/null | \
            awk '($$NF > 0 && $$1 != "#") {print $$2;}' | \
            egrep '^%{target}' | uniq`;\
        echo $$result


/def -i complete_variable = \
    /set _complete_variable_list=%;\
    /let part=$[substr(%1, strrchr(%1, '%') + 1, 999999)]%;\
    /if /test substr(part, 0, 1) =~ '{'%; /then \
        /let part=$[substr(part, 1, 999999)]%;\
        /set _completion_suffix=}%;\
    /endif%;\
    /quote -0 /_complete_variable %{part} `/set%%;/setenv%%;/echo <done>

/def -i _complete_variable = \
    /let args=%-1%;\
    /if /test args !~ "<done>"%; /then \
        /let word=$[substr(%{-2}, 0, strchr(%{-2}, '='))]%;\
        /set _complete_variable_list=%{_complete_variable_list} %{word}%;\
    /else \
        /_complete_from_list %1 %{_complete_variable_list}%;\
    /endif


/def -i complete_worldname = \
    /set _complete_worldname_list=%;\
    /quote -0 /_complete_worldname %{1} `/listworlds%%;/echo <done>

/def -i _complete_worldname = \
    /let args=%-1%;\
    /if /test args =~ "<done>"%; /then \
        /_complete_from_list %1 %{_complete_worldname_list}%;\
    /elseif /test "%{3}" !/ "-T*"%; /then \
        /set _complete_worldname_list=%{_complete_worldname_list} %{3}%;\
    /else \
        /set _complete_worldname_list=%{_complete_worldname_list} %{4}%;\
    /endif

;; /complete_context <word>
;; Uses context to determine which completion macro to use.

/def -i complete_context = \
    /let head=$[kbhead()]%;\
    /if /test strrchr(head, "@") > strrchr(head, " ")%; /then \
        /complete_hostname %1%;\
    /elseif /test $[strrchr(head, "%") > strrchr(head, " ")]%; /then \
        /complete_variable %1%;\
;   /elseif /test $[strrchr(head, "$") > strrchr(head, " ")]%; /then \
;       /complete_macroname %1%;\
;   /elseif /test head =/ "{/*}"%; /then \
;       /complete_command %1%;\
    /elseif /test head =/ "{/[sl]et|/setenv} *"%; /then \
        /complete_variable %1%;\
    /elseif /test head =/ "{/load*|/save*} *"%; /then \
        /complete_filename %1%;\
;   /elseif /test head =/ "{whisper|page|tel*|kill} *"%; /then \
;       /complete_playername %1%;\
    /elseif /test regmatch("^/quote .*'(.*)", head)%; /then \
        /complete_filename %P1%;\
    /elseif /test head =/ "*{*/*|.*}"%; /then \
        /complete_filename %1%;\
    /elseif /test head =/ "{/world} *"%; /then \
        /complete_worldname %1%;\
    /elseif /test head =/ "{/telnet|/addworld}*"%; /then \
        /complete_hostname %1%;\
    /else \
        /complete_user_defined %1%;\
    /endif


;;; /common_prefix <min> <list>...
;; prints the common prefix shared by each word in <list>, assumes at least
;; first <min> chars are shared.

/def -i common_prefix = \
    /let min=%1%;\
    /let result=%2%;\
    /shift 2%;\
    /while /test %#%; /do \
        /let i=%min%;\
        /let len=$[strlen(result)]%;\
        /while /test i < len%; /do \
            /if /test $[substr(result, i, 1) !~ substr(%1, i, 1)]%; /then \
                /break%;\
            /endif%;\
            /let i=$[i + 1]%;\
        /done%;\
        /let result=$[substr(result, 0, i)]%;\
        /shift%;\
    /done%;\
    /echo -- %result

