;;; Self-reproducing macro (without using ${self})
;;; Not useful, just interesting.

/def self=/let q="%;/let p=%% %;/let f=strcat("/def self=/let q=",q,p,";/let p=",p,p," ",p,";/let f=",f,p,";/test echo(",p,"f)")%;/test echo(%f)

