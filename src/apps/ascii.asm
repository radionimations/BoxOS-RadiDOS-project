; ASCII.BIN - prints the printable ASCII range (32..126) as
; "<num>=<char> " columns, demonstrating multiple syscalls.

[BITS 64]
[ORG 0x200000]

%include "src/apps/_app_template.inc"

global _start
_start:
    SET_COLOR 11, 0
    PUTS    title
    SET_COLOR 7, 0

    mov     rbx, 32
.loop:
    cmp     rbx, 127
    jge     .done

    mov     rax, SYS_PRINT_INT
    mov     rdi, rbx
    int     0x80
    PUTC    '='

    mov     rax, SYS_PUTC
    mov     rdi, rbx
    int     0x80
    PUTC    ' '

    inc     rbx
    jmp     .loop
.done:
    PUTC    10
    xor     rax, rax
    ret

title: db "Printable ASCII (32-126):", 10, 0
