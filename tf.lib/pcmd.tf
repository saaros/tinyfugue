;;; prefix/suffix for mud commands

/~loaded pcmd.tf

/def -i pfxrand = \
    /set outputprefix=<pre:$[rand()]>%;\
    /set outputsuffix=<suf:$[rand()]>

/pfxrand

/def -i pfxon = \
    OUTPUTPREFIX %{outputprefix}%;\
    OUTPUTSUFFIX %{outputsuffix}

/def -i pfxoff = \
    OUTPUTPREFIX%;\
    OUTPUTSUFFIX

/def -i pcmd = \
    /pfxon%;\
    %*%;\
    /pfxoff%;\
    /pfxrand

