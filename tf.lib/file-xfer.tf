;;;; File transfer macros
;;; These are designed for uploading and downloading files between 
;;; your local system and a MUF editor on a MUCK.  They can be changed
;;; to work with other mud editors.

/~loaded file-xfer.tf

;;; Local-to-mud file transfer:  /putfile <file>
;; Assumes the mud has an editor with these commands:
;;    @edit <file>      edit file <file>
;;    i                 enter insert mode
;;    .                 exit insert mode
;;    q                 quit editor

/def -i putfile =\
    @edit %1%;\
    i%;\
    /quote -0 !"cat %1; echo .; echo q"


;;; Mud-to-local file transfer:  /getfile <file>
;; Assumes your mud has an editor entered with "@edit <file>", lists a
;; file with '1 99999 l', and prints "Entering editor." and "Editor exited."
;; Change this if your mud is different.

/def -i getfile =\
    /def -i -1 -aG -p98 -msimple -t"Editor exited." getfile_end =\
        /log -w OFF%%;\
        /undef getfile_quiet%;\
    /def -i -1 -p99 -msimple -t"Entering editor." getfile_start =\
        /log -w <file>%%;
        /def -i -p97 -ag -mglob -t"*" getfile_quiet%;\
    @edit %1%;\
    1 99999 l%;\
    q

