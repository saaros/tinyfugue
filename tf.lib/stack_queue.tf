;;;; Stack and queue macros.
;;;; Lets you keep stacks and queues of words.  /pop and /dequeue give their
;;;; results via /echo; use $(/pop) and $(/dequeue) to capture their results.

/~loaded stack_queue.tf

/require lisp.tf

;;; /push <word> [<stack>]
; push word onto stack.

/def -i push = /eval /set %{2-stack}=%1 %%{%{2-stack}}

;;; /pop [<stack>]
; get and remove top word from stack.

/def -i pop = /eval /car %%{%{1-stack}}%%; /set %{1-stack}=$$(/cdr %%{%{1-stack}})

;;; /enqueue <word> [<queue>]
; put word on queue.

/def -i enqueue = /eval /set %{2-queue}=%%{%{2-queue}} %1

;;; /dequeue [<queue>]
; get and remove first word from queue.

/def -i dequeue = /pop %1-queue

