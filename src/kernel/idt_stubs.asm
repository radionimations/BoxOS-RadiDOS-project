; BoxOS IDT stubs
; -------------------------------------------------------------------
; Generates 48 ISR entry points (vectors 0..47) and an array of
; pointers `isr_stub_table` that the C IDT init code uses to fill
; in the IDT entries.
;
; Stack layout when interrupt_dispatch is called (low addresses
; first, matching `struct interrupt_frame` in boxos.h):
;
;   r15 r14 r13 r12 r11 r10 r9 r8       (PUSH_ALL, last-pushed = top)
;   rdi rsi rbp rbx rdx rcx rax
;   vector              <- pushed by ISR_NOERR/ISR_ERR macro
;   error_code          <- pushed by CPU (real exceptions) or our 0
;   rip cs rflags rsp ss                 <- pushed by CPU
; -------------------------------------------------------------------

[BITS 64]

extern interrupt_dispatch

%macro PUSH_ALL 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

%macro ISR_NOERR 1
global isr%1
isr%1:
    push    qword 0
    push    qword %1
    jmp     isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push    qword %1
    jmp     isr_common
%endmacro

isr_common:
    ; Vector is at [rsp+8] (we pushed err_code last in ISR_ERR, or 0
    ; in ISR_NOERR, then pushed vector). Only stamp VGA on CPU
    ; exceptions (vec < 32). Output: "!! <vec_dec> " at row 24 col 0
    ; on red bg. Survives even if C handler can't run.
    push    rax
    push    rcx
    push    rdi
    mov     rcx, [rsp + 24]     ; vector
    cmp     rcx, 32
    jae     .skip_marker
    mov     rdi, 0xB8000 + (24 * 80 * 2)
    mov     ax, 0x4F21          ; '!'
    mov     [rdi + 0], ax
    mov     [rdi + 2], ax
    mov     [rdi + 4], ax       ; "!!!"
    ; Print vector as 2 decimal digits at col 4..5 (red bg, ASCII)
    mov     rax, rcx
    xor     rdx, rdx
    mov     rcx, 10
    div     rcx                 ; rax = tens, rdx = ones
    add     al, '0'
    add     dl, '0'
    mov     ah, 0x4F
    mov     [rdi + 8], ax       ; tens at col 4
    mov     dh, 0x4F
    mov     [rdi + 10], dx      ; ones at col 5
.skip_marker:
    pop     rdi
    pop     rcx
    pop     rax

    PUSH_ALL
    cld
    mov     rdi, rsp
    call    interrupt_dispatch
    POP_ALL
    add     rsp, 16              ; drop vector + error_code
    iretq

; --- CPU exception vectors 0..31 --------------------------------
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; --- IRQ vectors 32..47 (after PIC remap) -----------------------
%assign i 32
%rep 16
    ISR_NOERR i
%assign i i+1
%endrep

; --- Software interrupt 0x80 (= 128) for app syscalls -----------
ISR_NOERR 128

; --- Pointer table the C code uses to install IDT entries -------
section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dq isr %+ i
%assign i i+1
%endrep
