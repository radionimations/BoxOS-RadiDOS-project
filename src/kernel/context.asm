; Cooperative context switch for BoxOS 2.0's scheduler.
;
;   void context_switch(uint64_t* save_rsp_p,
;                       uint64_t  load_rsp,
;                       uint64_t  load_cr3);
;     rdi = where to store the *outgoing* task's RSP
;     rsi = the *incoming* task's RSP to load
;     rdx = the *incoming* task's PML4 physical address (cr3)
;
; SysV AMD64: rbx, rbp, r12-r15 are callee-saved, the rest are caller-
; saved (already preserved by the C compiler at the call site). We
; pushf/cli + popf around the switch so a half-finished swap can't
; race with an interrupt that lands on a stack pointer mid-transition.
;
; CR3 is reloaded only if it actually changes — a needless write
; flushes the TLB and tanks performance after a switch.

[BITS 64]
global context_switch

section .text
context_switch:
    pushfq
    cli
    push    rbp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     [rdi], rsp          ; save outgoing RSP
    mov     rsp, rsi            ; load incoming RSP

    mov     rax, cr3
    cmp     rax, rdx
    je      .skip_cr3
    mov     cr3, rdx
.skip_cr3:

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    popfq                       ; restores IF (re-enables interrupts if they were on)
    ret
