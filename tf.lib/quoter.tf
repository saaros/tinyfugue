;;;; Quoting utilities
;;;
;;; /qdef   [<prefix>] <name>  - quote a (current) macro definition
;;; /qmac   [<prefix>] <name>  - quote a macro from a macro file
;;; /qworld [<prefix>] <name>  - quote a world definition
;;; /qfile  [<prefix>] <name>  - quote a file
;;; /qtf    <cmd>              - quote a tf command
;;; /qsh    <cmd>              - quote a shell command
;;; /qmud   <cmd>              - quote a mud command (requires pcmd.tf to work)
;;;
;;; <prefix> is prepended to each generated line.  The default prefix is ":|",
;;; but can be changed in /qdef, /qmac, /qworld, and /qfile.

/~loaded quoter.tf

/require pcmd.tf

/def -i qdef = /quote -0 %{-L-:|} `/list %{L-@}

/set _qmac_files=%{HOME}/.tfrc %{HOME}/*.tf %{HOME}/tiny.* %{HOME}/mud/*.tf \
    %{HOME}/mud/tiny.* %{HOME}/tf/*.tf %{HOME}/tf/tiny.* %{TFLIBDIR}/*.tf

/def -i qmac = \
  /setenv prog=\
      /^\\/def.* %L *=/ { f = 1; } \
      { if (f) print \$0; } \
      /^[^;].*[^\\\\]\$/ { f = 0; }%;\
  /eval /quote -0 %{-L-:|} !awk "\\\$prog" `ls %{_qmac_files} 2>/dev/null`

/def -i qworld = /quote -0 %{-L-:|} `/listworlds %{L-@}

/def -i qfile = /quote -0 %{-L-:|} '%{L-@}

/def -i qtf = :` %*%; /quote -0 :| `%*

/def -i qsh = :! %*%; /quote -0 :| !%*

/def -i qmud =\
  /def -i -p5000 -msimple -t"%{outputprefix}" -1 -aGg qmud_pre =\
    :> %*%%;\
    /def -i -p5001 -mglob -t"*" -aGg qmud_all = :| %%%*%%;\
    /def -i -p5002 -msimple -t"%{outputsuffix}" -1 -aGg qmud_suf = \
        /undef qmud_all%;\
  /pcmd %*
