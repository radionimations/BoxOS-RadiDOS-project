#include "boxos.h"

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
};

struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256];
static struct idt_ptr   idtr;

extern void* isr_stub_table[48];
extern void  isr128(void);

static void idt_set_gate(int n, void* handler, uint16_t selector, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[n].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[n].selector    = selector;
    idt[n].ist         = 0;
    idt[n].type_attr   = type_attr;
    idt[n].reserved    = 0;
}

void idt_init(void) {
    /* Selector 0x08 is the long-mode code segment from stage 2's GDT,
     * which is still loaded. type 0xE = 64-bit interrupt gate, P=1, DPL=0. */
    for (int i = 0; i < 48; i++)
        idt_set_gate(i, isr_stub_table[i], 0x08, 0x8E);

    /* Vector 0x80 = software syscall gate. DPL=3 so future ring-3
     * code can `int 0x80` without a GP fault. Type 0xF = TRAP gate
     * (vs. 0xE = interrupt gate) so the CPU does NOT clear IF on
     * entry: the syscall handler can still receive IRQs while it
     * waits for, e.g., a keypress in SYS_GETC. */
    idt_set_gate(0x80, (void*)isr128, 0x08, 0xEF);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)idt;
    __asm__ __volatile__ ("lidt %0" :: "m"(idtr));
}

static const char* exception_name(uint64_t v) {
    switch (v) {
        case 0:  return "Divide Error";
        case 1:  return "Debug";
        case 2:  return "NMI";
        case 3:  return "Breakpoint";
        case 4:  return "Overflow";
        case 5:  return "Bound Range";
        case 6:  return "Invalid Opcode";
        case 7:  return "Device Not Available";
        case 8:  return "Double Fault";
        case 10: return "Invalid TSS";
        case 11: return "Segment Not Present";
        case 12: return "Stack Fault";
        case 13: return "General Protection";
        case 14: return "Page Fault";
        case 16: return "x87 FP";
        case 17: return "Alignment Check";
        case 18: return "Machine Check";
        case 19: return "SIMD FP";
        default: return "Reserved/Unknown";
    }
}

void interrupt_dispatch(struct interrupt_frame* f) {
    if (f->vector < 32) {
        /* Clear the screen so the crash dump always lands on a fresh
         * surface — the lead-up output (DOOM startup, [loader] etc.)
         * scrolls so fast the user can never screenshot the crash
         * if we print after it. Loss of context is worth the gain. */
        vga_clear();
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("\n*** EXCEPTION %lu (%s)  err=0x%lx  RIP=%p ***\n",
                   f->vector, exception_name(f->vector),
                   f->error_code, (void*)f->rip);
        vga_printf("RAX=%p RBX=%p RCX=%p RDX=%p\n",
                   (void*)f->rax, (void*)f->rbx, (void*)f->rcx, (void*)f->rdx);
        vga_printf("RSI=%p RDI=%p RBP=%p RSP=%p\n",
                   (void*)f->rsi, (void*)f->rdi, (void*)f->rbp, (void*)f->rsp);
        vga_printf("R12=%p R13=%p R14=%p R15=%p\n",
                   (void*)f->r12, (void*)f->r13, (void*)f->r14, (void*)f->r15);
        if (f->vector == 14) {
            uint64_t cr2;
            __asm__ __volatile__ ("mov %%cr2, %0" : "=r"(cr2));
            vga_printf("CR2 (faulting addr) = %p\n", (void*)cr2);
            /* Dump the page-directory entry covering the faulting
             * 2 MiB region. If it's been corrupted (e.g. zeroed),
             * we'll see Present=0 here — that's the smoking gun.
             * NB: don't dereference RIP from the handler — if RIP's
             * page is unmapped that recursive read triple-faults. */
            uint64_t* pd = (uint64_t*)0x3000;
            uint64_t  pd_idx = (cr2 >> 21) & 0x1FF;
            vga_printf("PD[%lu] = %p   (covers 0x%lx00000..0x%lx00000)\n",
                       pd_idx, (void*)pd[pd_idx],
                       pd_idx * 2, pd_idx * 2 + 2);
        }
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        for (;;) __asm__ __volatile__ ("cli; hlt");
    }

    if (f->vector >= 32 && f->vector < 48) {
        uint8_t irq = (uint8_t)(f->vector - 32);
        /* Watchdog: PML4[0] should always be 0x00002003. If something
         * has wild-pointer'd zero into it (or anything else), halt
         * NOW with the last-known good state visible — before the
         * next memory access triple-faults. Runs every timer tick. */
        if (irq == 0) {
            uint64_t pml4_0 = *(volatile uint64_t*)0x1000;
            /* Mask off CPU-managed bits: Accessed (0x20), Dirty (0x40),
             * PCD (0x10), PWT (0x08). We only care that the entry still
             * Present + RW + points at our PDPT (0x2000). */
            if ((pml4_0 & ~0x78ULL) != 0x00002003ULL) {
                vga_clear();
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_printf("\n*** WATCHDOG: PML4[0] CORRUPTED ***\n");
                vga_printf("expected 0x00002003, got 0x%lx\n", pml4_0);
                vga_printf("Last known RIP (faulted code): not yet faulted\n");
                vga_printf("Caller frame RIP=%p RSP=%p\n",
                           (void*)f->rip, (void*)f->rsp);
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                for (;;) __asm__ __volatile__ ("cli; hlt");
            }
        }
        if (irq == 0)  timer_tick();
        if (irq == 1)  keyboard_irq();
        if (irq == 5)  sb16_irq();
        if (irq == 12) mouse_irq();
        pic_send_eoi(irq);
        return;
    }

    if (f->vector == 128) {
        /* Syscall: write the return value back into the saved rax so
         * POP_ALL hands it back to the app. */
        f->rax = (uint64_t)syscall_dispatch(f);
        return;
    }
}
