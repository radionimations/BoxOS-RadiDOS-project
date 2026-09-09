#include "boxos.h"
#include "task.h"

/* BUILD_TAG comes from -DBUILD_TAG="\"...\"" in the Makefile.
 * Stamps the boot banner so we can confirm the synced kernel is the
 * one actually booting. */
#ifndef BUILD_TAG
#define BUILD_TAG "dev"
#endif

static void banner(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("+--------------------------------------------------------------+\n");
    vga_puts("|               RadiDOS 2.0   (x86_64, long mode)             |\n");
    vga_puts("|                build: " BUILD_TAG "\n");
    vga_puts("+--------------------------------------------------------------+\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void log_ok(const char* msg) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("[ok] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(msg);
    vga_putc('\n');
}

static void log_warn(const char* msg) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("[--] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(msg);
    vga_putc('\n');
}

/* Background task: bump g_task_heartbeat once per second so the WM
 * can show it next to the clock as a "scheduler is alive" indicator.
 * Proves cooperative multitasking is actually rotating tasks. */
static void heartbeat_task(void) {
    uint64_t last = 0;
    for (;;) {
        uint64_t now = timer_ms();
        if (now - last >= 1000) {
            last = now;
            g_task_heartbeat++;
        }
        /* Sleep yields under the hood — desktop gets its turn here. */
        timer_sleep_ms(100);
    }
}

/* Stage 2 identity-mapped just the first 2 MiB. Fill the rest of the
 * page directory so the entire first 1 GiB is identity-mapped via
 * 2 MiB huge pages. Apps live at 2 MiB, the heap at 16 MiB, so we
 * just want it all "always present, always writable, no worries". */
static void map_first_gigabyte(void) {
    volatile uint64_t* pd = (volatile uint64_t*)0x3000;
    for (int i = 0; i < 512; i++)
        pd[i] = ((uint64_t)i * 0x200000ULL) | 0x83ULL;   /* P | RW | PS */
    uint64_t cr3;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ __volatile__ ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void kernel_main(void) {
    /* Snapshot the BIOS font BEFORE we make any VGA register changes.
     * gfx_init reads plane 2 from 0xA0000 then restores text-mode
     * register values, so vga_init's text-mode writes still land. */
    gfx_init();
    vga_init();
    fb_init();
    banner();

    log_ok("Boot sector loaded stage 2.");
    log_ok("Stage 2 enabled long mode and jumped to kernel.");
    log_ok("VGA text driver online.");

    if (fb_present()) {
        const struct boot_info* bi = fb_bootinfo();
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK); vga_puts("[ok] ");
        vga_set_color(VGA_LIGHT_GREY,  VGA_BLACK);
        vga_printf("VBE %dx%dx%d LFB @ 0x%x  pitch=%d  mode=0x%x\n",
                   (int)bi->fb_width, (int)bi->fb_height, (int)bi->fb_bpp,
                   (unsigned)bi->fb_addr, (int)bi->fb_pitch,
                   (int)bi->fb_mode);
    } else {
        log_warn("VBE framebuffer not available — GUI will fall back to text mode.");
    }

    pic_mask_all();
    pic_remap(0x20, 0x28);
    log_ok("PIC remapped (master 0x20-0x27, slave 0x28-0x2F).");

    idt_init();
    log_ok("IDT installed (gates 0..47 + 0x80 syscall).");

    keyboard_init();
    pic_unmask(2);     /* cascade        */
    pic_unmask(1);     /* PS/2 keyboard  */
    __asm__ __volatile__ ("sti");
    log_ok("Keyboard armed, interrupts enabled.");

    mouse_init();
    log_ok("PS/2 mouse armed (IRQ12).");

    map_first_gigabyte();
    log_ok("Memory: identity-mapped 0..1 GiB.");

    timer_init(100);
    pic_unmask(0);    /* PIT, now that we have a handler installed */
    log_ok("PIT timer @ 100 Hz, ms clock running.");

    heap_init();
    log_ok("Heap: 16 MiB bump allocator @ 0x1000000.");

    /* SB16: optional. If the VM doesn't emulate a Sound Blaster the
     * probe fails and we silently fall back to PC speaker. */
    (void)sb16_init();

    /* gfx_init() removed: it was leaving the VGA registers in
     * "linear plane-2 access" state, which makes subsequent text
     * mode writes go nowhere and the display scan out garbage.
     * gfx_set_mode_13h() saves the font lazily on first call. */

    if (fs_mount() == 0) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK); vga_puts("[ok] ");
        vga_set_color(VGA_LIGHT_GREY,  VGA_BLACK);
        vga_printf("FAT12 mounted on IDE drive %d. Type DIR to list apps.\n",
                   fs_drive());
        /* Enable disk-backed log on the same drive — every status
         * line gets persisted to LBA 65520+ (8 KiB at end of disk). */
        int drv = fs_drive();
        vga_log_set_disk(drv / 2, drv % 2);
        vga_status_line("[BOOT] disk log armed");
        if (fb_present()) {
            const struct boot_info* bi = fb_bootinfo();
            char buf[80];
            int n = 0;
            const char* fmt = "[BOOT] fb ";
            while (fmt[n]) { buf[n] = fmt[n]; n++; }
            /* Build "WIDTHxHEIGHTxBPP @ HEXADDR" by hand to avoid pulling
             * in a printf into the status line. */
            int w = bi->fb_width, h = bi->fb_height, b = bi->fb_bpp;
            char tmp[16]; int t;
            #define EMIT_DEC(v) do{ int q=(v),pos=0; if(q==0){tmp[pos++]='0';} \
                else { char rev[12]; int rn=0; while(q){rev[rn++]='0'+(q%10);q/=10;} \
                       while(rn--) tmp[pos++]=rev[rn]; } \
                for(t=0;t<pos;t++) buf[n++]=tmp[t]; }while(0)
            EMIT_DEC(w); buf[n++]='x'; EMIT_DEC(h); buf[n++]='x'; EMIT_DEC(b);
            const char* a = " @ 0x"; for(int i=0;a[i];i++) buf[n++]=a[i];
            uint64_t addr = bi->fb_addr;
            for (int s = 28; s >= 0; s -= 4) {
                int nib = (int)((addr >> s) & 0xF);
                buf[n++] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
            }
            buf[n] = 0;
            vga_status_line(buf);
            #undef EMIT_DEC
        } else {
            vga_status_line("[BOOT] fb none -- text mode only");
        }
    } else {
        log_warn("No FAT12 filesystem found - DIR/RUN unavailable.");
        log_warn("Attach boxos-fs.img to the VM as a hard disk.");
    }

    /* Cooperative multitasking: register the current thread of
     * execution as task 0 ("kernel"), then spawn background tasks
     * that run alongside the desktop. timer_sleep_ms() yields on
     * each iteration so the scheduler rotates between us. */
    task_init();
    task_spawn("heartbeat", heartbeat_task);
    log_ok("Tasks: kernel + heartbeat spawned (cooperative).");

    if (fb_present() && fb_enable() == 0) {
        fbcon_init();
        /* fbcon stays invisible by default — only the Terminal window
         * activates it. Otherwise the loader's debug prints leak
         * through as black bands across the Executive. */
        fbcon_set_visible(false);
        cursor_init();
        /* First-boot Arise flow (only while /SYS/SETUP.DONE is
         * missing): Phase 1 is the RadiDOS text-mode SETUP — drive
         * list, scandisk, wipe, mark/eject — which runs BEFORE the
         * splash. Press Esc at its prompt to skip straight to the
         * graphical wizard. Once Setup writes SETUP.DONE, both
         * phases are bypassed and we go straight to login/desktop. */
        {
            uint16_t sys = fs_find_dir(0, "SYS");
            int setup_done = 0;
            if (sys != 0xFFFF) {
                static char probe[4];
                setup_done = fs_read_in(sys, "SETUP.DONE",
                                        probe, sizeof(probe)) >= 0;
            }
            if (!setup_done) {
                fbcon_set_visible(true);
                shell_radidos_setup();
                fbcon_set_visible(false);
            }
        }
        splash_show();
        desktop_loop();
        /* desktop_loop never returns in normal use; if it does
         * (Esc), drop into the bare shell as a last resort. */
    }

    shell_run();
    for (;;) __asm__ __volatile__ ("hlt");
}
