;;; Keybindings for extended functions.

/~loaded kbbind.tf

/require kbfunc.tf

/def -ib'^[c'	= /kb_capitalize_word
/def -ib'^[l'	= /kb_downcase_word
/def -ib'^[u'	= /kb_upcase_word
/def -ib'^T'	= /kb_transpose_chars
/def -ib'^[_'	= /kb_last_argument
/def -ib'^[.'	= /kb_last_argument
