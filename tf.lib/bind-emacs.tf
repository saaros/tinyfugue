;;;; emacs style keybindings for TinyFugue

/~loaded bind-emacs.tf

/def -i -b"^a"		= /dokey home
/def -i -b"^b"		= /dokey left
/def -i -b"^d"		= /dokey dch
/def -i -b"^e"		= /dokey end
/def -i -b"^f"		= /dokey right
/def -i -b"^j"		= /dokey newline
/def -i -b"^k"		= /dokey deol
/def -i -b"^l"		= /dokey redraw
/def -i -b"^n"		= /dokey recallf
/def -i -b"^p"		= /dokey recallb
/def -i -b"^v"		= /dokey page
/def -i -b"^?"		= /dokey bspc
/def -i -b"^hm"		= /visual
/def -i -b"^hb"		= /list -ib
/def -i -b"^h?"		= /help
/def -i -b"^h^h"	= /help
/def -i -b"^x^b"	= /listsockets
/def -i -b"^x^d"	= /quote -0 /echo !ls
/def -i -b"^x1"		= /visual off
/def -i -b"^x2"		= /visual on
/def -i -b"^xk"		= /dc
/def -i -b"^[!"		= /sh
/def -i -b"^[>"		= /dokey flush
/def -i -b"^[b"		= /dokey wleft
/def -i -b"^[f"		= /dokey wright
/def -i -b"^[n"		= /dokey socketf
/def -i -b"^[p"		= /dokey socketb
/def -i -b"^[v"		= /dokey insert
/def -i -b"^[^?"	= /dokey bword
