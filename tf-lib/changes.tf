;;; /changes
;; Using sed would be very fast, but not portable.  Simple-minded comparisons
;; or triggers would be too slow.  This version is complex, but not too slow.

/def -i changes = \
    /let _ver=%{*-$(/ver)}%; \
    /def ~changes = /~changes_out %%*%;\
    /def ~changes_out = \
;       ; look for the version number marking top of section
        /if ({*} !/ ": %{_ver} *") /break%%; /endif%%;\
        /echo - %%{-L}%%; \
        /edit ~changes = /~changes_in %%%*%; \
    /def ~changes_in = \
        /echo - %%{-L}%%; \
;       ; look for blank line marking end of section
        /if ({*} =~ ":  :") \
            /edit ~changes = /~changes_out %%%*%%; \
        /endif%;\
    /quote -S /~changes : '"%TFLIBDIR/CHANGES" :%; \
    /purge ~changes*
