/* Link map for BoxOS C apps.
 *
 * Apps run as flat binaries (objcopy -O binary), loaded by the
 * kernel at physical 0x200000. The `.text.entry` section (which
 * contains _start from app_crt.asm) MUST be first so that the
 * very first byte at 0x200000 is the entry point.
 *
 * __bss_start / __bss_end let crt0 zero static globals at startup,
 * since flat-binary doesn't carry .bss bytes.
 */

ENTRY(_start)

SECTIONS {
    . = 0x200000;

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

    .bss : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame*)
        *(.rela.*)
    }
}
