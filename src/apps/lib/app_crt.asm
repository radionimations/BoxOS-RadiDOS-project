; BoxOS app entry stub.
; --------------------------------------------------------------
; Lives at the very start of the linked binary (section
; `.text.entry`, placed first by app.ld). Zeros .bss, calls
; app_main(args), then returns to the kernel with the result.

[BITS 64]

extern app_main
extern __bss_start
extern __bss_end

section .text.entry
global _start
_start:
    ; rdi already holds the args pointer the kernel passed.
    ; Stash it in r12 (callee-saved, so syscalls preserve it).
    mov     r12, rdi

    ; Zero .bss so static globals start at 0.
    cld
    mov     rdi, __bss_start
    mov     rcx, __bss_end
    sub     rcx, rdi
    test    rcx, rcx
    jz      .ready
    xor     al, al
    rep     stosb
.ready:

    mov     rdi, r12          ; restore args ptr
    call    app_main
    ret                       ; back to the kernel; rax = exit code
