;;;; $Id: textencode.tf,v 35000.1 2002/04/17 17:39:34 hawkeye Exp $
;;;; Encode/decode text to/from a form containing only letters, digits, and
;;;; underscores.

/loaded textencode.tf

/def -i textencode = \
    /let src=%; \
    /let dst=%; \
    /test src:={*}%; \
    /while (regmatch("[^a-zA-Z0-9]", src)) \
	/test dst := strcat(dst, {PL}, '_', ascii({P0}), '_')%; \
	/test src := {PR}%; \
    /done%; \
    /result strcat(dst, src)

/def -i textdecode = \
    /let src=%; \
    /let dst=%; \
    /test src:={*}%; \
    /while (regmatch("_([0-9]+)_", src)) \
	/test dst := strcat(dst, {PL}, char({P1}))%; \
	/test src := {PR}%; \
    /done%; \
    /result strcat(dst, src)
