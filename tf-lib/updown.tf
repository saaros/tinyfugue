;;;; Old style up/down keys move within an input line.

/def -F -hredef -agG ~gag_updown

/~keybind Up	/dokey_up
/~keybind Down	/dokey_down

/~defaultbind ^[[A /dokey_up
/~defaultbind ^[[B /dokey_down

/~defaultbind ^[OA /dokey_up
/~defaultbind ^[OB /dokey_down

/undef ~gag_updown
