;;; textutil.tf
;;; unix-like text utilities

/loaded __TFLIB__/textutil.tf

; Note: users should not rely on %_loaded_libs like this.  I can get away
; with this here only because this and /loaded are internal to TF.
/if (_loaded_libs =/ "*{__TFLIB__/grep.tf}*") \
    /echo -e %% Warning: textutil.tf and grep.tf have conflicting defintions.%;\
/endif

/require lisp.tf


/def -i _grep = \
    /let line=%; \
    /let count=0%; \
    /while (tfread(line) >= 0) \
        /test (%{*}) & (++count, opt_c | tfwrite(line))%; \
    /done%; \
    /test opt_c & echo(count)%; \
    /return count

; ... %| /fgrep [-cv] <string>
/def -i fgrep = \
    /if (!getopts("cv", 0)) /return 0%; /endif%; \
    /let pattern=%*%; \
    /_grep (strstr(line, pattern) < 0) == opt_v

; ... %| /grep [-cv] <glob>
/def -i grep = \
    /if (!getopts("cv", 0)) /return 0%; /endif%; \
    /let pattern=%*%; \
    /_grep (line !/ pattern) == opt_v

; ... %| /egrep [-cv] <regexp>
/def -i egrep = \
    /if (!getopts("cv", 0)) /return 0%; /endif%; \
    /let pattern=%*%; \
    /_grep !regmatch(pattern, line) == opt_v


; /copyio <in_handle> <out_handle>
; copies lines from <in_handle> to <out_handle>.
/def -i copyio = \
    /let in=%{1-i}%; \
    /let out=%{2-o}%; \
    /let line=%; \
    /while (tfread(in, line) >= 0) \
        /test tfwrite(out, line)%; \
    /done

; /readfile <file> %| ...
/def -i readfile = \
    /let handle=%; \
    /test ((handle := tfopen({1}, "r")) >= 0) & \
        (copyio(handle, "o"), tfclose(handle))

; ... %| /writefile <file>
/def -i writefile = \
    /let handle=%; \
    /if (!getopts("a", 0)) /return 0%; /endif%; \
    /test ((handle := tfopen({1}, opt_a ? "a" : "w")) >= 0) & \
        (copyio("i", handle), tfclose(handle))


; ... %| /head [-n<count>] [<handle>]
; outputs first <count> lines of <handle> or tfin.
/def -i head = \
    /if (!getopts("n#", 10)) /return 0%; /endif%; \
    /let handle=%{1-i}%; \
    /let line=%; \
    /while (tfread(handle, line) >= 0) \
        /if (opt_n) \
            /test --opt_n, echo(line)%; \
        /endif%; \
    /done


; ... %| /wc [-lwc] [<handle>]
; counts lines, words, and/or characters of text from <handle> or tfin.
/def -i wc = \
    /if (!getopts("lwc", 0)) /return 0%; /endif%; \
    /let handle=%{1-i}%; \
    /let lines=0%; \
    /let words=0%; \
    /let chars=0%; \
    /let line=%; \
    /let body=0%; \
    /if (!opt_l & !opt_w & !opt_c) /test opt_l:= opt_w:= opt_c:= 1%; /endif%; \
    /if (opt_l) /let body=%body, ++lines%; /endif%; \
    /if (opt_w) /let body=%body, words:=words+$$(/length %%line)%; /endif%; \
    /if (opt_c) /let body=%body, chars:=chars+strlen(line)%; /endif%; \
    /eval \
        /while (tfread(handle, line) >= 0) \
            /test %body%%; \
        /done%; \
    /echo $[opt_l ? lines : ""] $[opt_w ? words : ""] $[opt_c ? chars : ""]

; ... %| /tee <handle> %| ...
; copies tfin to <handle> AND tfout.
/def -i tee = \
    /let line=%; \
    /while (tfread(in, line) >= 0) \
        /test tfwrite({*}, line), tfwrite(line)%; \
    /done

; ... %| /fmt 
; copies input to output, with adjacent non-blank lines joined
/def -i fmt = \
    /let line=%; \
    /let text=%; \
    /while (tfread(line) >= 0) \
        /if (line =~ "" & text !~ "") \
            /echo - %{text}%; \
            /echo%; \
            /let text=%; \
        /else \
            /let text=%{text} %{line}%; \
        /endif%; \
    /done%; \
    /echo - %{text}

; ... %| /uniq
; copies input to output, with adjacent duplicate lines removed
/def -i uniq = \
    /let prev=%; \
    /let line=%; \
    /while (tfread(line) >= 0) \
        /if (line !~ prev) \
            /test echo(line), prev:=line%; \
        /endif%; \
    /done

