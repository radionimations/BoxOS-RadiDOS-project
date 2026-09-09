; HELLO.BIN - tiny "hello world" demo for BoxOS.
;
; rdi on entry  -> args string (NUL-terminated, possibly empty)
; rax at return -> exit code  (0 = ok)

[BITS 64]
[ORG 0x200000]

%include "src/apps/_app_template.inc"

global _start
_start:
    push    rdi                ; save args pointer across syscalls

    SET_COLOR 14, 0            ; yellow
    PUTS    msg_hi
    SET_COLOR 7, 0             ; light grey

    PUTS    msg_args_open
    mov     rax, SYS_PUTS
    pop     rdi                ; restore args pointer
    int     0x80
    PUTS    msg_args_close

    xor     rax, rax
    ret

msg_hi:          db "Hello from a BoxOS app!", 10, 0
msg_args_open:   db "(args were: '", 0
msg_args_close:  db "')", 10, 0
