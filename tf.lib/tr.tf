;;;; Character translation
;;; usage:  /tr <domain> <range> <string>
;;; <domain> and <range> are lists of charcters.  Each character in <string>
;;; that appears in <domain> will be translated to the corresponding
;;; character in <range>.

;;; Example:
;;; command: /def biff = /let string=%*%; /tr OIS. 01Z! $[toupper(string)]
;;; command: /biff TinyFugue is cool wares, dude.
;;; output:  T1NYFUGUE 1Z C00L WAREZ, DUDE!

/~loaded tr.tf

/def -i tr = \
    /let old=%1%;\
    /let new=%2%;\
    /let tail=%-2%;\
    /let dest=%;\
    /while /let i=$[strchr(tail, old)]%; /test i >= 0%; /do \
        /let j=$[strchr(old, substr(tail, i, 1))]%;\
        /let dest=$[strcat(dest, substr(tail,0,i), substr(new, j, 1))]%;\
        /let tail=$[substr(tail,i+1,99999999)]%;\
    /done%;\
    /echo -- %{dest}%{tail}

