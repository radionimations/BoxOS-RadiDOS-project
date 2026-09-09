; BoxOS 64-bit kernel entry stub. Lives at the very start of the
; kernel image so that stage 2's `jmp 0x10000` lands in valid code
; before we hand off to C.

[BITS 64]
global _start
extern kernel_main

section .text.entry
_start:
    cli
    cld
    xor     rbp, rbp
    mov     rsp, 0x90000
    call    kernel_main
.hang:
    cli
    hlt
    jmp     .hang
