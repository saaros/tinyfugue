;;;; Completion.
;; This allows you to type part of a word, hit a special key, and have
;; the rest of the word filled in for you automagically.  ESC TAB attempts
;; to complete based on context, or from a user defined list of words;
;; a few other bindings will do more explicit completions.
;;
;; To use:  /load this file, and optionally store a list of words in
;; %{completion_list}.  For example, add this to your ~/.tfrc file:
;;
;;    /load complete.tf
;;    /set completion_list=Hawkeye TinyFugue ftp.tcp.com
;;
;; Completion keys:
;;
;; ESC TAB	complete from context, input history, or %{completion_list}.
;; ESC ;	complete from %{completion_list}.
;; ESC i	complete from input history.
;; ESC /	filename completion (UNIX only).
;; ESC @	hostname completion.
;; ESC %	variable name completion.
;; ESC $	macro name completion.
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
/def -ib'^[i'	= /complete input_history
/def -ib'^[/'	= /complete filename
/def -ib'^[@'	= /complete hostname
/def -ib'^[%'	= /complete variable
/def -ib'^[$'	= /complete macroname
/def -ib'^[^W'	= /complete worldname

; /def -ib'^[~'	= /complete playername  (not implemented)
; /def -ib'^[!'	= /complete command     (not implemented)

;; /complete_playername is difficult to do correctly because of mud delays,
;; and is not portable to different muds, so it is not implemented here.


/def -i complete = \
    /complete_%{1-context} $(/last $[kbhead()])


;; /_complete_from_list <word> <list...>
;; <word> is the partial word to be completed.
;; <list> is a list of possible matches.
;; If <word> matches exactly one member of <list>, that member will be
;; inserted in the input buffer.  If multiple matches are found, the
;; longest common prefix will be inserted, and a list of matches will
;; be displayed.  If no matches are found, it simply beeps.
;; If exactly one match was found, %{_completion_suffix} or a space
;; will be appended to the completed word.
;; If the local variable %{_need_unique} is true, the list will be run
;; through /unique.

/def -i _complete_from_list = \
    /let prefix=%1%;\
    /shift%;\
    /let len=$[strlen(prefix)]%;\
    /let match=%;\
;
;   scan list for words which start with prefix.
    /while ({#}) \
        /if (strncmp({1}, prefix, len) == 0) \
            /let match=%{match} %{1}%;\
        /endif%;\
        /shift%;\
    /done%;\
;
;   Remove duplicates (and strip leading space)
    /if (_need_unique) \
        /let match=$(/unique %{match})%;\
    /else \
        /let match=$(/echo - %{match})%;\
    /endif%;\
;
    /if (match =~ "") \
;       No match was found.
        /beep 1%;\
    /elseif (match !/ "{*}") \
;       Multiple matches were found.  Use longest common prefix.
        /beep 1%;\
        /@test input(substr($$(/common_prefix %{len} %{match}), len))%;\
        /echo - %{match}%;\
    /else \
;       Exactly one match was found.
        /@test match := strcat(match, _completion_suffix)%;\
        /if (match =/ "*[A-Za-z0-9_]") \
            /@test match := strcat(match, " ")%;\
        /endif%;\
        /@test input(substr(match, len))%;\
    /endif%;\
;   Just to be safe
    /unset _completion_suffix%;\
    /unset _need_unique


/def -i complete_user_defined = \
    /_complete_from_list %1 %completion_list

/def -i ~input_history_list = \
    /let input=$(/recall -i #1)%;\
    /recall -i 1-$[substr(input, 0, strchr(input, ":")) - 1]

/def -i complete_input_history = \
    /let _need_unique=1%;\
    /_complete_from_list %1 $(/~input_history_list)

/def -i complete_dynamic = \
    /let _need_unique=1%;\
    /_complete_from_list %1 %completion_list $(/~input_history_list)


/def -i complete_filename = \
    /quote -S /_complete_from_list %1 !\
        echo `/bin/ls -dq %1* 2>/dev/null | \
            while read f; do \ test -d $$f && echo $$f/ || echo $$f; done`


/def -i complete_hostname = \
    /let _need_unique=1%;\
    /let pf=$[substr({1}, strrchr({1}, "@") + 1)]%;\
    /quote -S /_complete_from_list %{pf} !\
       echo `cat /etc/hosts %HOME/etc/hosts 2>/dev/null | \
          sed -n '/^[^#].*[ 	][ 	]*\\\\(%{pf}[^ 	]*\\\\).*/s//\\\\1/p'`


/def -i complete_variable = \
    /let part=$[substr({1}, strrchr({1}, '%') + 1)]%;\
    /if (strncmp(part, '{', 1) == 0) \
        /let part=$[substr(part, 1)]%;\
        /let _completion_suffix=}%;\
    /endif%;\
    /_complete_from_list %part \
        $(/quote -S /_complete_variable `/set%%;/setenv)

/def -i _complete_variable = \
    /let name=%-1%;\
    /@test echo(substr(name, 0, strchr(name, '=')))%;\


/def -i complete_macroname = \
    /let word=%1%;\
    /let i=$[strrchr({1}, '$')]%;\
    /if (i >= 0) \
        /if (substr(word, i+1, 1) =~ '{') \
            /@test ++i%;\
            /let _completion_suffix=}%;\
        /endif%;\
        /let word=$[substr(word, i+1)]%;\
    /elseif (strncmp(word, '/', 1) == 0) \
        /let word=$[substr(word, 1)]%;\
    /endif%;\
    /_complete_from_list %{word} $(/quote -S /last `/list -s -i - %{word}*)


/def -i complete_worldname = \
    /_complete_from_list %1 $(/quote -S /_complete_worldname `/listworlds %{1}*)

/def -i _complete_worldname = \
    /shift $[{2} =/ "-T*"]%;\
    /echo - %{2}


;; /complete_context <word>
;; Uses context to determine which completion macro to use.

/def -i complete_context = \
    /let head=$[kbhead()]%;\
    /let word=%1%;\
    /if (strchr(word, "@") >= 0) \
        /complete_hostname %1%;\
    /elseif (strchr(word, "%%") >= 0) \
        /complete_variable %1%;\
    /elseif (strchr(word, "$$") >= 0) \
        /complete_macroname %1%;\
;   /elseif (head =/ "{/*}") \
;       /complete_command %1%;\
    /elseif (head =/ "{/*}") \
        /complete_macroname %1%;\
    /elseif (regmatch("-w(.+)$$", head)) \
        /complete_worldname %P1%;\
    /elseif (head =/ "*{/[sl]et|/setenv|/unset} {*}") \
        /complete_variable %1%;\
    /elseif (head =/ "*{/load*|/save*|/lcd|/cd|/log} {*}") \
        /complete_filename %1%;\
    /elseif (head =/ "*{/def|/edit|/reedit|/undef|/list} {*}") \
        /complete_macroname %1%;\
;   /elseif (head =/ "*{wh|page|tel*|kill} {*}") \
;       /complete_playername %1%;\
    /elseif (regmatch(`/quote .*'("?)(.+)$$`, head)) \
        /let completion_suffix=%P1%;\
        /complete_filename %P2%;\
;   /elseif (regmatch('/quote .*`("?)(.+)$$', head)) \
;       /let completion_suffix=%P1%;\
;       /complete_command %P2%;\
    /elseif (regmatch('/quote .*`("?)(.+)$$', head)) \
        /let completion_suffix=%P1%;\
        /complete_macroname %P2%;\
    /elseif (head =/ "*{/world|/connect|/fg} {*}") \
        /complete_worldname %1%;\
    /elseif (head =/ "*{/telnet} {*}") \
        /complete_hostname %1%;\
    /elseif (head =/ "*/quote *!*") \
        /complete_filename %1%;\
    /elseif (head =/ "*{/@test|/expr} *") \
        /complete_variable %1%;\
    /elseif (head =/ "*{*/*|.*|tiny.*}") \
        /complete_filename %1%;\
    /else \
        /complete_dynamic %1%;\
    /endif


;;; /common_prefix <min> <list>...
;; prints the common prefix shared by each word in <list>, assuming at least
;; first <min> chars are already known to be shared.

/def -i common_prefix = \
    /let min=%1%;\
    /shift%;\
    /let prefix=%1%;\
    /let len=$[strlen(prefix)]%;\
    /while /shift%; /@test {#} & len > min%; /do \
        /let i=%min%;\
        /while (i < len & strncmp(prefix, {1}, i+1) == 0) \
            /@test ++i%;\
        /done%;\
        /let len=%i%;\
    /done%;\
    /echo - $[substr(prefix, 0, len)]

