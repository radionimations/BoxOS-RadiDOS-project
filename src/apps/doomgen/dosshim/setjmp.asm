; x86_64 SysV setjmp/longjmp.
; jmp_buf layout: rbx, rbp, r12, r13, r14, r15, rsp_after_call, rip
; (8 bytes each, 64 bytes total, matches `unsigned long [8]` in setjmp.h)

bits 64
default rel

global setjmp
global longjmp

setjmp:
    mov     [rdi + 0],  rbx
    mov     [rdi + 8],  rbp
    mov     [rdi + 16], r12
    mov     [rdi + 24], r13
    mov     [rdi + 32], r14
    mov     [rdi + 40], r15
    lea     rax, [rsp + 8]      ; rsp value the caller expects after our 'ret'
    mov     [rdi + 48], rax
    mov     rax, [rsp]          ; saved rip = caller's return address
    mov     [rdi + 56], rax
    xor     eax, eax
    ret

longjmp:
    mov     eax, esi            ; return value
    test    eax, eax
    jne     .nonzero
    mov     eax, 1              ; longjmp(env, 0) must yield 1
.nonzero:
    mov     rbx, [rdi + 0]
    mov     rbp, [rdi + 8]
    mov     r12, [rdi + 16]
    mov     r13, [rdi + 24]
    mov     r14, [rdi + 32]
    mov     r15, [rdi + 40]
    mov     rsp, [rdi + 48]
    mov     rcx, [rdi + 56]
    jmp     rcx
