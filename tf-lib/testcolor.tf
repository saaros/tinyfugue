;;;; testcolor.tf

/set _line=@
/for _i 0 7 \
    /set _line=%{_line}{C%_i}X@
/eval /set _line=%{_line}{n} @
/for _i 0 7 \
    /set _line=%{_line}{Cbg%_i} @
/eval /echo -p - Basic colors:  %{_line}{}

/set _line=@
/for _i 8 15 \
    /set _line=%{_line}{C%_i}X@
/eval /set _line=%{_line}{n} @
/for _i 8 15 \
    /set _line=%{_line}{Cbg%_i} @
/eval /echo -p - Bright colors: %{_line}{}

/_echo

/if (!features("256colors")) \
    /_echo The 256colors feature is not enabled.%; \
    /exit%; \
/endif

/_echo 6x6x6 color cubes:
/for _green 0 5 \
    /set _line=@%; \
    /for _red 0 5 \
	/for _blue 0 5 \
	    /set _line=%%%_line{Crgb%%%_red%%%_green%%%_blue}X@%%; \
	/set _line=%%{_line}{n}@%; \
    /set _line=%{_line}{} @%; \
    /for _red 0 5 \
	/for _blue 0 5 \
	    /set _line=%%%_line{Cbgrgb%%%_red%%%_green%%%_blue} @%%; \
	/set _line=%%{_line}{n}@%; \
    /echo -p - %{_line}{}
/_echo

/set _line=@
/for _i 0 23 \
    /set _line=%{_line}{Cgray%_i}X@
/eval /set _line=%{_line}{n} @
/for _i 0 23 \
    /set _line=%{_line}{Cbggray%_i} @
/eval /echo -p - Grayscales: %{_line}{}

/unset _line
