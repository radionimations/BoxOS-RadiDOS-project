; ECHO2.BIN - reads keys and echoes them until you press ESC.
; Demonstrates blocking input via SYS_GETC.

[BITS 64]
[ORG 0x200000]

%include "src/apps/_app_template.inc"

global _start
_start:
    SET_COLOR 13, 0
    PUTS    msg_intro
    SET_COLOR 7, 0

.loop:
    mov     rax, SYS_GETC
    int     0x80
    cmp     rax, 27               ; ESC?
    je      .done
    cmp     rax, 0
    je      .loop

    mov     rdi, rax
    mov     rax, SYS_PUTC
    int     0x80
    jmp     .loop

.done:
    PUTC    10
    SET_COLOR 14, 0
    PUTS    msg_bye
    SET_COLOR 7, 0
    xor     rax, rax
    ret

msg_intro: db "echo2: type any keys, ESC to quit.", 10, 0
msg_bye:   db "echo2: bye.", 10, 0
