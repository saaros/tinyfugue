;;; AT - run a command at a given time
;; syntax:  /at <when> <commands>
;; <commands> will be executed at <when>, where <when> is of the form
;; "hh:mm" or "hh:mm:ss" ("hh" is between 0 and 23).  <when> is within
;; the next 24 hours.

/~loaded at.tf

/def -i at = \
    /let delay=$[{1} - $(/time %%H:%%M:%%S)]%;\
    /repeat -$[(delay >= 0) ? delay : (delay + 24:00)] 1 %-1

