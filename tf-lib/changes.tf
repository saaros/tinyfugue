;;; /changes
;; Using sed would be very fast, but not portable.  Simple-minded comparisons
;; or triggers would be too slow.  This version is complex, but not too slow.

/def -i changes = \
    /let _ver=%{*-$(/ver)}%; \
    /let _pat=%{_ver} *%; \
    /let _name=%TFLIBDIR/CHANGES%; \
    /let _fd=$[tfopen(_name, "r")]%; \
    /let _line=%; \
    /let _in=0%; \
    /while (tfread(_fd, _line) >= 0) \
	/if (!_in) \
	    /if (_line =~ _ver | _line =/ _pat) \
		/test _in := 1%; \
		/test echo(_line)%; \
	    /endif%; \
	/else \
	    /test echo(_line)%; \
	    /if (_line =~ "") \
		/test _in := 0%; \
	    /endif%; \
	/endif%; \
    /done%; \
    /test tfclose(_fd)
