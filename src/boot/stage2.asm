; BoxOS stage 2
; -------------------------------------------------------------------
; Loaded by stage 1 at 0x7E00 in 16-bit real mode. We:
;   1. Enable A20 (fast gate, port 0x92).
;   2. Stamp a BootInfo magic at phys 0x500 so the kernel can tell
;      the struct exists. Graphics-mode setup is done in the kernel
;      via the BGA IO ports (works in QEMU/UTM/VirtualBox); real
;      VBE for bare metal will land in a later milestone.
;   3. Build identity-mapping page tables for the first 2 MiB
;      (one PML4 + one PDPT + one PD with a 2 MiB huge entry).
;        PML4 @ 0x1000 -> PDPT @ 0x2000 -> PD @ 0x3000 -> phys 0
;   4. Load a 64-bit GDT (null, code, data).
;   5. Set CR4.PAE, CR3, EFER.LME, CR0.PG|PE  (real mode -> long mode).
;   6. Far-jump to 64-bit code segment, then jump to kernel @ 0x8E00.
; -------------------------------------------------------------------

; -------------------------------------------------------------------
; BootInfo layout at phys 0x500 (read by kernel after long-mode entry):
;   +0x00  u32 magic     'BOX1' = 0x31584F42
;   +0x04  u64 fb_addr   linear framebuffer phys addr (0 = unknown)
;   +0x0C  u32 fb_pitch  bytes per scanline
;   +0x10  u16 fb_width
;   +0x12  u16 fb_height
;   +0x14  u8  fb_bpp
;   +0x15  u8  reserved
;   +0x16  u16 fb_mode   VBE mode number used (0 = none)
; -------------------------------------------------------------------

[BITS 16]
[ORG 0x7E00]

stage2_start:
    mov     si, msg_stage2
    call    print16

    ; --- Enable A20 (port 0x92, "fast" method) ---
    in      al, 0x92
    test    al, 0x02
    jnz     .a20_ok
    or      al, 0x02
    and     al, 0xFE        ; never set bit 0 (would cause reset)
    out     0x92, al
.a20_ok:

    ; --- BootInfo: just stamp the magic. Graphics setup happens in
    ;     the kernel; we leave fb_addr=0 here. ---
    mov     dword [0x500], 0x31584F42
    mov     dword [0x504], 0
    mov     dword [0x508], 0
    mov     dword [0x50C], 0
    mov     word  [0x510], 0
    mov     word  [0x512], 0
    mov     byte  [0x514], 0
    mov     byte  [0x515], 0
    mov     word  [0x516], 0

    ; --- Zero PML4, PDPT, PD at 0x1000..0x3FFF (12 KiB) ---
    cld
    xor     ax, ax
    mov     es, ax
    mov     di, 0x1000
    mov     cx, 0x1800      ; 0x3000 bytes / 2 = 0x1800 words
    rep     stosw

    ; PML4[0] -> PDPT @ 0x2000 | present | writable
    mov     dword [0x1000], 0x00002003

    ; PDPT[0] -> PD @ 0x3000 | present | writable
    mov     dword [0x2000], 0x00003003

    ; PD[0] -> phys 0x000000 | present | writable | PS (2 MiB page)
    mov     dword [0x3000], 0x00000083

    ; --- Load 64-bit GDT ---
    lgdt    [gdt64.pointer]

    ; --- CR4.PAE = 1 ---
    mov     eax, cr4
    or      eax, 1 << 5
    mov     cr4, eax

    ; --- CR3 = PML4 ---
    mov     eax, 0x1000
    mov     cr3, eax

    ; --- EFER.LME = 1 (Long Mode Enable) ---
    mov     ecx, 0xC0000080
    rdmsr
    or      eax, 1 << 8
    wrmsr

    ; --- CR0.PG = 1, CR0.PE = 1 (paging + protected -> long mode) ---
    mov     eax, cr0
    or      eax, (1 << 31) | (1 << 0)
    mov     cr0, eax

    ; --- Far jump into 64-bit code segment ---
    jmp     dword gdt64.code:long_mode_entry

; AH=0x0E teletype loop, DS:SI -> NUL-terminated string
print16:
    mov     ah, 0x0E
.l: lodsb
    test    al, al
    jz      .d
    int     0x10
    jmp     .l
.d: ret

; -------------------------------------------------------------------
[BITS 64]
long_mode_entry:
    ; Reload data segment registers with the data selector.
    mov     ax, gdt64.data
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax

    mov     rsp, 0x90000

    ; Jump to kernel entry point at physical 0x8E00 (right after us).
    mov     rax, 0x8E00
    jmp     rax

; -------------------------------------------------------------------
; Data
; -------------------------------------------------------------------
msg_stage2: db "BoxOS: stage 2 -> long mode", 13, 10, 0

align 8
gdt64:
    dq 0                                              ; 0x00 null
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)          ; 0x08 code, exec/read, present, long
.data: equ $ - gdt64
    dq (1<<41) | (1<<44) | (1<<47)                    ; 0x10 data, writable, present
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

; Pad to exactly 8 sectors (4096 bytes) so the on-disk layout matches
; what stage 1 reads. The last 4 bytes hold a magic marker that lets
; stage 1 detect "I was preloaded by El Torito, skip the disk read".
times 4096 - 4 - ($-$$) db 0
db 'BOX2'
