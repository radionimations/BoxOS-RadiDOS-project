/* BoxOS kernel link map.
 * Stage 2 jumps to physical address 0x8E00, so the entry stub
 * (.text.entry from kentry.asm) MUST be the very first thing here.
 *
 * Layout in low memory after either boot path:
 *   0x07C00  boot sector
 *   0x07E00  stage 2 (4 KiB)
 *   0x08E00  kernel (this file's load address)
 *   0x90000  initial stack top
 *   0xB8000  VGA text framebuffer
 */

ENTRY(_start)

SECTIONS {
    . = 0x8E00;

    .text : ALIGN(16) {
        *(.text.entry)
        *(.text*)
    }

    .rodata : ALIGN(16) {
        *(.rodata*)
    }

    .data : ALIGN(16) {
        *(.data*)
    }

    /* Place BSS above the legacy VGA/BIOS hole (0xA0000..0xFFFFF).
     * Without this gap, large BSS arrays (e.g. desktop.c's 1 MiB
     * shuttle) land at addresses 0xA0000..0xC0000 which are mapped
     * to VGA hardware planes, not RAM — writes go to video memory
     * and reads return 0xFF, which broke fs_read_in / image viewing. */
    . = 0x100000;
    .bss : ALIGN(16) {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame*)
        *(.rela.*)
    }
}
