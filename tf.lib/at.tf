;;; AT - run a command at a given time
;; syntax:  /at <when> <commands>
;; <commands> will be executed at <when>, where <when> is of the form
;; "hh:mm" or "hh:mm:ss" ("hh" is between 0 and 23).  <when> is within
;; the next 24 hours.

/~loaded at.tf

/def -i at = \
    /let delay=0%;\
    /let when=%1%;\
    /if /test (delay := when - $(/time %%H:%%M:%%S)) < 0%; /then \
        /test delay := 24:00 + delay%;\
    /endif%;\
    /repeat -%{delay} 1 %-1

