;;;; rwho - remote who
;;; syntax:  /rwho
;;;          /rwho who=<playername>
;;;          /rwho mud=<mudname>
;;; Gets a remote WHO list from a mudwho server.  The first form gives a
;;; complete list, the other forms give partial lists.  Due to the short
;;; timeout of the mudwho server, sometimes the complete list is sent
;;; even if the second or third format is used (send complaints to the
;;; author or maintainer of the mudwho server, not to me).

;;; Make sure you /load rwho.tf _after_ you define your worlds, or rwho
;;; will be the default world.

/~loaded rwho.tf

;; This site is current as of November 1993, but is subject to change.
/addworld rwho riemann.math.okstate.edu 6889

/def -i rwho = \
    /def -i -1 -F -msimple -h'connect rwho' ~connect_rwho = \
        /send -wrwho -- %*%;\
    /world rwho
