;;;; Keyboard stack
;;;; This is useful when you're in the middle of typing a long line,
;;;; and want to execute another command without losing the current line.
;;;; Type ESC DOWN to save the current input line on a stack.
;;;; Type ESC UP to restore the saved line.  Any number of lines can
;;;; be saved and restored.

/~loaded kbstack.tf

/def -ib^[^[OB = /kb_push
/def -ib^[^[[B = /kb_push
/def -ib^[^[OA = /kb_pop
/def -ib^[^[[A = /kb_pop

/def -i kb_push = \
    /let line=$(/recall -i 1)%;\
    /if /test line !~ ""%; /then \
        /set _kb_stack_top=$[_kb_stack_top + 1]%;\
        /set _kb_stack_%{_kb_stack_top}=%{line}%;\
    /endif%;\
    /dokey dline

/def -i kb_pop = \
    /if /test _kb_stack_top > 0%; /then \
        /dokey dline%;\
        /test input(_kb_stack_%{_kb_stack_top})%;\
        /unset _kb_stack_%{_kb_stack_top}%;\
        /set _kb_stack_top=$[_kb_stack_top - 1]%;\
    /else \
        /echo %% Keyboard stack is empty.%;\
    /endif

