;;; AT - run a command at a given time
;; syntax:  /at <when> <commands>
;; <commands> will be executed at <when>, where <when> is of the form
;; "h:m[:s[.f]]" ("h" is between 0 and 23).  <when> is in the next 24 hours.

/loaded __TFLIB__/at.tf

/def -i at = \
    /let _delay=$[{1} - $(/time %%H:%%M:%%S.%%.)]%;\
    /repeat -$[(_delay > 0) ? _delay : (_delay + 24:00)] 1 %-1

