;;; $Id: local-eg.tf,v 35000.3 1998/04/09 20:26:16 hawkeye Exp $

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; TF local library
;;; This file is not required, but contains some examples of commands
;;; you might want to use in your version of %TFLIBDIR/local.tf.  Other
;;; commands can be added here as well.  Use of the -i option is
;;; recommended with any /def commands placed in this file.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; Mark this file as already loaded, for /require.
/~loaded local.tf

;;; If this tf is available to users without shells, you probably want
;;; to restrict their access. (See "/help restrict" in tf).
; /restrict SHELL
; /restrict FILE
; /restrict WORLD

;;; HELPFILE is defined during installation.  If you want to move the
;;; helpfile without re-installing, uncomment and edit the line below.

; /def -i HELPFILE=/usr/local/lib/tf-lib/help

;;; Mail location
;; If your system keeps incoming mail in the recipient's home, instead of
;; in a central spool directory, uncomment the lines below (and edit the
;; "/set MAIL" line if needed).

; /eval \
;     /if ( MAIL !~ "" ) \
;         /set MAIL=%{HOME}/.mailbox %;\
;     /endif

;;; file compression
;; COMPRESS_READ should contain a command that take a filename as an
;; argument, and prints its output on stdout.  The library is configured
;; to use '.Z' and 'zcat'.  To use a different program, uncomment the pair
;; used on your system, or add your own.

; GNU compression
; /def -i COMPRESS_SUFFIX=.gz
; /def -i COMPRESS_READ=zcat

; SysV compression
; /def -i COMPRESS_SUFFIX=.z
; /def -i COMPRESS_READ=pcat

