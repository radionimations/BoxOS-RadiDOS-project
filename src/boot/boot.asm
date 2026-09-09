; BoxOS stage 1 boot sector
; -------------------------------------------------------------------
; The BIOS loads us at physical address 0x7C00 in 16-bit real mode
; with DL set to the boot drive number. Our job:
;
;   1. If the El Torito CD loader already preloaded the rest of the
;      image into RAM (via -boot-load-size), skip the disk read.
;      Detection: the assembled stage 2 ends with a 'BOX2' marker at
;      its very last 4 bytes, so we just check for that.
;
;   2. Otherwise (HDD/floppy boot), use INT 13h LBA extensions to
;      read sectors 1..72 (stage 2 + kernel) into 0x0000:0x7E00.
;
;   3. Far-jump to stage 2 at 0x7E00.
;
; Same boot sector works on:  qemu -drive (raw HDD), qemu -cdrom,
; UTM with the .iso, and real hardware.
; -------------------------------------------------------------------

[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti

    mov     [boot_drive], dl

    mov     si, msg_boot
    call    print_string

    ; Was the rest of the image preloaded by El Torito?
    ; The marker lives at the very end of stage 2's 4 KiB region.
    cmp     dword [0x7E00 + 4096 - 4], 'BOX2'
    je      .ready

    ; HDD/floppy path: read stage 2 + kernel. The kernel has grown
    ; past the 127-sector INT 13h per-call cap, so we issue two
    ; back-to-back reads that together cover stage2 (8 sec) + up to
    ; ~92 KiB of kernel.
    mov     si, dap_image
    mov     dl, [boot_drive]
    mov     ah, 0x42
    int     0x13
    jc      disk_err

    mov     si, dap_image2
    mov     dl, [boot_drive]
    mov     ah, 0x42
    int     0x13
    jc      disk_err

.ready:
    mov     si, msg_jump
    call    print_string
    jmp     0x0000:0x7E00

disk_err:
    mov     si, msg_disk_err
    call    print_string
.hang:
    hlt
    jmp     .hang

; AH=0x0E teletype loop, DS:SI -> NUL-terminated string
print_string:
    mov     ah, 0x0E
.next:
    lodsb
    test    al, al
    jz      .done
    int     0x10
    jmp     .next
.done:
    ret

boot_drive:    db 0

; First read: 127 sectors starting at LBA 1, loaded to 0x0000:0x7E00.
; Covers stage2 (8 sectors) + first 119 sectors of kernel.
dap_image:
    db 16
    db 0
    dw 127
    dw 0x7E00
    dw 0x0000
    dq 1

; Second read: another 127 sectors starting at LBA 128, loaded
; immediately after the first read (phys 0x07E00 + 127*512 = 0x17C00,
; encoded as segment 0x17C0 offset 0). Gives us another ~63 KiB,
; total kernel cap ~125 KiB (254 sectors).
dap_image2:
    db 16
    db 0
    dw 127
    dw 0x0000
    dw 0x17C0
    dq 128

msg_boot:      db "BoxOS: stage 1...", 13, 10, 0
msg_jump:      db "BoxOS: handing off to stage 2", 13, 10, 0
msg_disk_err:  db "Disk read error - halt", 13, 10, 0

; Pad to the start of the MBR partition table. SeaBIOS' El Torito
; hard-disk emulation refuses INT 13h reads on a boot image with no
; valid partition table, so we plant one that covers our FAT12 live
; area (sectors 2048..67583). The CHS bytes are 0x00 — modern BIOSes
; honour the LBA fields when CHS is left at default.
times 446-($-$$) db 0

; Entry 1: bootable, FAT12, LBA 2048, 65536 sectors (= 32 MiB).
db 0x80                          ; active flag
db 0x00, 0x00, 0x00              ; CHS start (LBA-only)
db 0x01                          ; partition type = FAT12
db 0x00, 0x00, 0x00              ; CHS end (LBA-only)
dd 2048                          ; first LBA
dd 65536                         ; sector count

; Three empty entries.
times 16 db 0
times 16 db 0
times 16 db 0

dw 0xAA55
