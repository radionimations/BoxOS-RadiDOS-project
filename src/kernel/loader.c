/* App loader + syscall dispatch.
 *
 * Apps are flat 64-bit binaries linked at APP_LOAD_ADDR (= 0x200000).
 * The loader reads them off the FAT12 disk into that address, then
 * `call`s the first byte with rdi pointing at the args string. The
 * app returns to us via `ret`, optionally with a status code in rax.
 *
 * Apps reach back into the kernel via `int 0x80`:
 *     rax = syscall number
 *     rdi/rsi/rdx = args
 * Result is left in rax for the app to consume. */

#include "boxos.h"
#include "task.h"

/* Cap raised to 2 MiB to fit FastDoom (~400 KiB) plus headroom.
 * App loads at virtual 0x200000 in its task's private user-space;
 * a 2 MiB huge page covers the whole region. */
#define APP_MAX_BYTES   (2 * 1024 * 1024)
#define APP_USER_BYTES  (2 * 1024 * 1024)   /* matches 2 MiB huge-page */

extern void kernel_main(void);

/* Phase 2: each app runs in its own task with a private user-space.
 *
 * Lifecycle:
 *   1. Allocate a 2 MiB-aligned physical region from the heap.
 *   2. Read the app's binary into it (kernel's identity map gives
 *      us a virtual address equal to the physical address, so a
 *      plain fs_read_file lands the bytes in the right place).
 *   3. Spawn a task whose deep-cloned PML4 maps virtual 0x200000
 *      to that physical region — so the app sees its code at the
 *      usual 0x200000 link address, but every concurrent app has
 *      a separate physical 2 MiB slab.
 *   4. Cooperatively wait (task_join) for the app to return.
 *      The desktop's chrome and any other tasks (heartbeat, …) keep
 *      ticking via the scheduler while we sit here.
 *
 * This keeps loader_run synchronous from the caller's point of view
 * (no API change for desktop's shuttle / cleanup flow), while every
 * launch now runs on its own task with a private address space. */
/* Internal helper: do everything *up to* spawning the task. Returns
 * the new task id (non-blocking — caller decides whether to join). */
static task_id loader_spawn_internal(const char* name, const char* args) {
    cursor_set_busy(1);
    uint8_t* user_phys = (uint8_t*)heap_alloc_aligned(APP_USER_BYTES,
                                                     APP_USER_BYTES);
    if (!user_phys) { cursor_set_busy(0); return -1; }
    int n = fs_read_file(name, user_phys, APP_MAX_BYTES);
    cursor_set_busy(0);
    if (n < 0) return -1;

    vga_printf("[loader] %s: %d bytes -> user_phys=0x%lx\n",
               name, n, (uint64_t)user_phys);

    /* Same input-state hygiene as the old inline path: drop queued
     * keys/events so they don't bleed into the new task. */
    keyboard_drain();

    return task_spawn_app(name, (uint64_t)user_phys, args ? args : "");
}

/* Non-blocking spawn — used by apps that want to launch other apps
 * (the Program Manager, future taskbar, etc.). Returns task id. */
task_id loader_spawn(const char* name, const char* args) {
    return loader_spawn_internal(name, args);
}

/* Blocking run — used by the desktop's launch flow that needs the
 * app's exit code (and to run cleanup hooks afterwards). */
int loader_run(const char* name, const char* args) {
    task_id tid = loader_spawn_internal(name, args);
    if (tid < 0) return -1;
    int rc = task_join(tid);
    /* Auto-clean: if the app left graphics mode dirty, restore the
     * Executive's text/256 setup. */
    gfx_app_cleanup();
    return rc;
}

/* ---------------------- syscall dispatch -------------------------- */

#define SYS_EXIT          0
#define SYS_PUTC          1
#define SYS_PUTS          2
#define SYS_GETC          3
#define SYS_CLS           4
#define SYS_SET_COLOR     5
#define SYS_PRINT_INT     6
#define SYS_GET_KEY       7        /* non-blocking; 0 if none */
#define SYS_GET_KEY_EVENT 8        /* (pressed<<8)|ascii or 0 */
#define SYS_MOUSE_POLL    9        /* fills caller buf with dx,dy,btn */

#define SYS_GFX_MODE     10        /* rdi: 0 = text, 1 = mode 13h    */
#define SYS_GFX_BLIT     11        /* rdi: ptr to 64000-byte fb      */
#define SYS_GFX_PALETTE  12        /* rdi: ptr to 768-byte RGB pal   */

#define SYS_SLEEP_MS     20        /* rdi: ms                        */
#define SYS_TICKS_MS     21        /* -> ms since boot               */

#define SYS_MALLOC       30        /* rdi: bytes -> ptr or 0         */
#define SYS_FREE         31        /* rdi: ptr                       */

#define SYS_READ_FILE    40        /* rdi: 8.3 name, rsi: buf,
                                      rdx: cap -> bytes or -1        */

#define SYS_STATUS_LINE  50        /* rdi: cstring -> overwrite the
                                      reserved bottom row of VGA     */

#define SYS_LIST_DIR     41        /* rdi: cluster (0=root)
                                      rsi: struct fat12_entry* buf
                                      rdx: max entries -> count        */

/* GUI / window-manager syscalls. Args that don't fit in 3 registers
 * are passed via a small struct (caller fills it, passes ptr in rdi). */
/* Sound (PC speaker). */
#define SYS_SOUND_TONE_ON  55      /* rdi: freq_hz                     */
#define SYS_SOUND_TONE_OFF 56
#define SYS_SOUND_BEEP     57      /* rdi: freq_hz, rsi: ms            */
#define SYS_SOUND_BELL     58
#define SYS_SOUND_OK       59
/* (chord_error reuses the next slot.) */
#define SYS_SOUND_ERROR    100

/* Sound Blaster 16 PCM (8-bit mono). */
#define SYS_SB16_PRESENT   62      /* -> 1 if SB16 detected             */
#define SYS_SB16_PLAY      63      /* rdi: ptr to {pcm,len,rate}        */
#define SYS_SB16_STOP      64
#define SYS_SB16_PLAYING   65      /* -> 1 while playback in progress   */

/* Cross-disk install (Arise SETUP). Slots in the unused 140s. */
#define SYS_INST_DRIVES   140      /* -> bit mask of present ATA drives */
#define SYS_INST_BOOT_DRV 141      /* -> (bus<<1)|drive index, or -1    */
#define SYS_INST_START    142      /* rdi: target idx -> 0 ok, <0 err   */
#define SYS_INST_CHUNK    143      /* 0=more, 1=done, <0 error          */
#define SYS_INST_PERCENT  144      /* -> 0..100                         */
#define SYS_INST_FINALIZE 145      /* rdi: ptr {idx, name, company} ->0 */
struct inst_final_args { uint32_t target_idx; const char* name; const char* company; };
#define SYS_FACTORY_RESET 146      /* wipe live FAT12 -> factory copy; 0 ok */
#define SYS_INST_ERRPHASE 147      /* -> 0 ok, 1 = read media, 2 = write target */
#define SYS_INST_BOOT_WR  148      /* -> 1 if the boot drive is a writable ATA disk */
#define SYS_INST_HAS_FACT 150      /* -> 1 if this install carries the factory backup */

/* Theme + sound enable. */
#define SYS_THEME_INDEX    110     /* -> int                           */
#define SYS_THEME_SET      111     /* rdi: index                       */
#define SYS_THEME_COUNT    112     /* -> int                           */
#define SYS_THEME_NAME     113     /* rdi: index, rsi: buf, rdx: cap   */
#define SYS_SOUND_ENABLED  114     /* -> int                           */
#define SYS_SOUND_ENABLE   115     /* rdi: 0/1                         */

/* Misc kernel queries used by Settings. */
#define SYS_WM_SET_TITLE   120     /* rdi: const char*                  */
#define SYS_HEAP_USED      121     /* -> uint64                         */
#define SYS_HEAP_TOTAL     122     /* -> uint64                         */
#define SYS_HALT           123     /* hard-halt (cli; hlt forever)      */

/* Filesystem write helpers used by Paint's save-as-BMP. */
#define SYS_DELETE_FILE    124     /* rdi: name83  -> 0/-1              */
#define SYS_CREATE_FILE    125     /* rdi: name83, rsi: buf, rdx: size  */
#define SYS_SAVE_TO_DOCS   126     /* rdi: name, rsi: buf, rdx: size    */
#define SYS_SAVE_TO_FOLDER 127     /* rdi: struct save_args*            */
struct save_args { const char* folder; const char* name;
                   const void* buf;    uint32_t size; };

#define SYS_GUI_POLL_EVENT 70      /* rdi: struct gui_event*  -> 0/1   */
#define SYS_GUI_OPEN       71      /* rdi: struct gui_open_args* -> h  */
#define SYS_GUI_CLOSE      72      /* rdi: handle                      */
#define SYS_GUI_FILL_RECT  73      /* rdi: struct gui_rect_args*       */
#define SYS_GUI_TEXT       74      /* rdi: struct gui_text_args*       */
#define SYS_GUI_SIZE       75      /* rdi: handle, rsi: *w, rdx: *h    */

struct gui_open_args { const char* title; int x, y, w, h; };
struct gui_rect_args { int handle, x, y, w, h; uint8_t color; };
struct gui_text_args { int handle, x, y; const char* s; uint8_t fg, bg; };
struct sb16_play_args { const uint8_t* pcm; uint32_t len; uint32_t rate; };

#define SYS_REBOOT       99        /* hard reset (0xCF9 / 8042 / #DF) */
#define SYS_POWEROFF     149       /* clean ACPI S5 power off          */
#define SYS_LAUNCH_APP_ASYNC 130   /* rdi: name, rsi: args -> task id */
#define SYS_LAUNCH_APP_AT    131   /* rdi: ptr to {name, args, cwd}   */

struct launch_at_args { const char* name; const char* args; uint32_t cwd_cluster; };

/* Debug counters — readable from QEMU monitor via xp at known
 * addresses. Each syscall increments its bucket. */
volatile uint64_t syscall_count_get_key  = 0;
volatile uint64_t syscall_count_ticks_ms = 0;
volatile uint64_t syscall_count_total    = 0;

int64_t syscall_dispatch(struct interrupt_frame* f) {
    uint64_t a1 = f->rdi;
    uint64_t a2 = f->rsi;
    uint64_t a3 = f->rdx;
    syscall_count_total++;
    if (f->rax == SYS_GET_KEY)  syscall_count_get_key++;
    if (f->rax == SYS_TICKS_MS) syscall_count_ticks_ms++;

    /* Synchronous PML4 corruption check on every syscall — catches
     * the corruption MUCH closer to the wild write than the 100Hz
     * timer-based watchdog. We checkpoint with bos_puts liberally so
     * this fires almost immediately if PML4[0] gets stomped. */
    {
        uint64_t pml4_0 = *(volatile uint64_t*)0x1000;
        if ((pml4_0 & ~0x78ULL) != 0x00002003ULL) {
            vga_clear();
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_printf("\n*** SYSCALL WATCHDOG: PML4[0] = 0x%lx ***\n", pml4_0);
            vga_printf("Syscall #%lu (rdi=%p rsi=%p rdx=%p)\n",
                       f->rax, (void*)a1, (void*)a2, (void*)a3);
            vga_printf("Caller RIP=%p RSP=%p\n", (void*)f->rip, (void*)f->rsp);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            for (;;) __asm__ __volatile__ ("cli; hlt");
        }
    }

    switch (f->rax) {
        case SYS_EXIT:        return 0;
        case SYS_PUTC:        vga_putc((char)a1); return 0;
        case SYS_PUTS:        vga_puts((const char*)a1); return 0;
        case SYS_GETC:        return (int64_t)keyboard_getc();
        case SYS_CLS:         vga_clear(); return 0;
        case SYS_SET_COLOR:   vga_set_color((uint8_t)a1, (uint8_t)a2); return 0;
        case SYS_PRINT_INT:   vga_printf("%ld", (int64_t)a1); return 0;
        case SYS_GET_KEY:     return (int64_t)(uint8_t)keyboard_try_getc();
        case SYS_GET_KEY_EVENT: return (int64_t)(uint16_t)keyboard_try_get_event();
        case SYS_MOUSE_POLL:  return (int64_t)mouse_poll((void*)a1);

        case SYS_GFX_MODE:
            if (a1 == 0) gfx_set_text_mode();
            else         gfx_set_mode_13h();
            return 0;
        case SYS_GFX_BLIT:    gfx_blit((const void*)a1); return 0;
        case SYS_GFX_PALETTE: gfx_set_palette((const void*)a1); return 0;

        case SYS_SLEEP_MS:    timer_sleep_ms(a1); return 0;
        case SYS_TICKS_MS:    return (int64_t)timer_ms();

        case SYS_MALLOC:      return (int64_t)(uintptr_t)heap_alloc((size_t)a1);
        case SYS_FREE:        heap_free((void*)a1); return 0;

        case SYS_READ_FILE:
            return (int64_t)fs_read_file((const char*)a1, (void*)a2, (uint32_t)a3);

        case SYS_LIST_DIR:
            return (int64_t)fs_list_dir((uint16_t)a1,
                                         (struct fat12_entry*)a2,
                                         (int)a3);

        case SYS_STATUS_LINE:
            vga_status_line((const char*)a1);
            return 0;

        case SYS_GUI_POLL_EVENT:
            return (int64_t)gui_poll_event((struct gui_event*)a1);

        case SYS_GUI_OPEN: {
            const struct gui_open_args* p = (const struct gui_open_args*)a1;
            if (!p) return -1;
            return (int64_t)wm_open_window(p->title, p->x, p->y, p->w, p->h);
        }

        case SYS_GUI_CLOSE:
            wm_close_window((int)a1);
            return 0;

        case SYS_GUI_FILL_RECT: {
            const struct gui_rect_args* p = (const struct gui_rect_args*)a1;
            if (!p) return -1;
            wm_w_fill_rect(p->handle, p->x, p->y, p->w, p->h, p->color);
            return 0;
        }

        case SYS_GUI_TEXT: {
            const struct gui_text_args* p = (const struct gui_text_args*)a1;
            if (!p) return -1;
            wm_w_text(p->handle, p->x, p->y, p->s, p->fg, p->bg);
            return 0;
        }

        case SYS_GUI_SIZE:
            wm_w_size((int)a1, (int*)a2, (int*)a3);
            return 0;

        case SYS_SOUND_TONE_ON:  sound_tone_on((uint32_t)a1);             return 0;
        case SYS_SOUND_TONE_OFF: sound_tone_off();                         return 0;
        case SYS_SOUND_BEEP:     sound_beep_ms((uint32_t)a1, (uint32_t)a2); return 0;
        case SYS_SOUND_BELL:     sound_bell();                             return 0;
        case SYS_SOUND_OK:       sound_chord_ok();                         return 0;
        case SYS_SOUND_ERROR:    sound_chord_error();                      return 0;

        case SYS_SB16_PRESENT:   return (int64_t)sb16_present();
        case SYS_SB16_PLAY: {
            const struct sb16_play_args* p = (const struct sb16_play_args*)a1;
            if (!p) return -1;
            return (int64_t)sb16_play(p->pcm, p->len, p->rate);
        }
        case SYS_SB16_STOP:      sb16_stop();                              return 0;
        case SYS_SB16_PLAYING:   return (int64_t)sb16_playing();

        case SYS_INST_DRIVES:    return (int64_t)fs_ata_present_mask();
        case SYS_INST_BOOT_DRV: {
            int b = fs_boot_bus(), d = fs_boot_drive();
            if (b < 0 || d < 0) return -1;
            return (int64_t)((b << 1) | d);
        }
        case SYS_INST_START: {
            int idx = (int)a1;
            if (idx < 0 || idx > 3) return -1;
            int bus = (idx >> 1) & 1;
            int drv = idx & 1;
            return (int64_t)fs_install_start(bus, drv);
        }
        case SYS_INST_CHUNK:     return (int64_t)fs_install_chunk();
        case SYS_INST_PERCENT:   return (int64_t)fs_install_percent();
        case SYS_INST_FINALIZE: {
            const struct inst_final_args* p = (const struct inst_final_args*)a1;
            if (!p) return -1;
            int idx = (int)p->target_idx;
            if (idx < 0 || idx > 3) return -1;
            return (int64_t)fs_install_finalize((idx >> 1) & 1, idx & 1,
                                                p->name, p->company);
        }
        case SYS_FACTORY_RESET:  return (int64_t)fs_factory_restore();
        case SYS_INST_ERRPHASE:  return (int64_t)fs_install_err_phase();
        case SYS_INST_HAS_FACT:  return (int64_t)fs_install_includes_factory();
        case SYS_INST_BOOT_WR: {
            int b = fs_boot_bus(), d = fs_boot_drive();
            if (b < 0 || d < 0) return 0;
            return (int64_t)(ata_drive_type(b, d) == 1);   /* 1 = DRV_ATA */
        }

        case SYS_THEME_INDEX:    return (int64_t)theme_index();
        case SYS_THEME_SET:      theme_set((int)a1);
                                 wm_init();
                                 return 0;
        case SYS_THEME_COUNT:    return (int64_t)theme_count();
        case SYS_THEME_NAME: {
            const struct os_theme* t = theme_at((int)a1);
            char* dst = (char*)a2;
            int   cap = (int)a3;
            int n = 0;
            if (dst && cap > 0) {
                while (t->name[n] && n < cap - 1) { dst[n] = t->name[n]; n++; }
                dst[n] = 0;
            }
            return n;
        }
        case SYS_SOUND_ENABLED:  return (int64_t)sound_is_enabled();
        case SYS_SOUND_ENABLE:   sound_set_enabled((int)a1); return 0;

        case SYS_WM_SET_TITLE:   wm_set_title((const char*)a1);
                                 wm_init();
                                 return 0;
        case SYS_HEAP_USED:      return (int64_t)heap_used();
        case SYS_HEAP_TOTAL:     return (int64_t)heap_total();
        case SYS_HALT:
            __asm__ __volatile__ ("cli");
            for (;;) __asm__ __volatile__ ("hlt");
            return 0;

        case SYS_DELETE_FILE:
            return (int64_t)fs_delete(fs_get_app_cwd(), (const char*)a1);
        case SYS_CREATE_FILE:
            return (int64_t)fs_create_file(fs_get_app_cwd(),
                                            (const char*)a1,
                                            (const void*)a2,
                                            (uint32_t)a3);
        case SYS_SAVE_TO_DOCS: {
            uint16_t docs = fs_find_dir(0, "DOCS");
            if (docs == 0xFFFF) return -1;
            /* Replace if present. */
            (void)fs_delete(docs, (const char*)a1);
            return (int64_t)fs_create_file(docs,
                                            (const char*)a1,
                                            (const void*)a2,
                                            (uint32_t)a3);
        }
        case SYS_SAVE_TO_FOLDER: {
            const struct save_args* p = (const struct save_args*)a1;
            if (!p || !p->name) return -1;
            uint16_t dir;
            if (!p->folder || p->folder[0] == 0 ||
                (p->folder[0] == '/' && p->folder[1] == 0)) {
                dir = 0;                       /* root */
            } else {
                dir = fs_find_dir(0, p->folder);
                if (dir == 0xFFFF) return -1;
            }
            (void)fs_delete(dir, p->name);
            return (int64_t)fs_create_file(dir, p->name,
                                            p->buf, p->size);
        }

        case SYS_REBOOT:
            __asm__ __volatile__ ("cli");
            /* 1) ACPI / ICH reset control register (port 0xCF9). On
             *    QEMU's i440fx this is the reliable one — SeaBIOS
             *    re-runs cleanly instead of dropping to a dead prompt. */
            outb(0xCF9, 0x02);
            outb(0xCF9, 0x06);
            /* 2) Classic 8042 keyboard-controller reset pulse. */
            for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++) { }
            outb(0x64, 0xFE);
            /* 3) Last resort: load a null IDT and fault → triple fault
             *    → CPU reset. Works on every x86 hypervisor. */
            {
                struct { uint16_t limit; uint64_t base; } __attribute__((packed))
                    null_idt = { 0, 0 };
                __asm__ __volatile__ ("lidt %0; int3" :: "m"(null_idt));
            }
            for (;;) __asm__ __volatile__ ("hlt");
            return 0;

        case SYS_POWEROFF:
            __asm__ __volatile__ ("cli");
            /* ACPI S5 (soft-off). i440fx exposes PM1a_CNT at 0x604;
             * SLP_TYP=0, SLP_EN=bit13 -> 0x2000. q35 uses 0xB004.
             * Bochs/old QEMU also honoured 0x4004/0xB004 = 0x3400. */
            outw(0x604,  0x2000);
            outw(0xB004, 0x2000);
            outw(0x4004, 0x3400);
            /* If nothing powered us off, fall back to a reset so we
             * don't sit here forever. */
            outb(0xCF9, 0x06);
            for (;;) __asm__ __volatile__ ("hlt");
            return 0;

        case SYS_LAUNCH_APP_ASYNC: {
            const char* name = (const char*)a1;
            const char* args = (const char*)a2;
            if (!name) return -1;
            /* App-to-app launches search /APPS, then /GAMES, so the
             * Program Manager (and any future taskbar) can hand us a
             * bare 8.3 name regardless of which folder the binary
             * lives in. We resolve the directory up front via a
             * lightweight name scan so loader_spawn only ever runs
             * once — every loader_spawn that gets past the fs_read
             * burns a 2 MiB user_phys block on the heap. */
            uint16_t prev = fs_get_app_cwd();
            uint16_t target = 0xFFFF;
            const char* search[] = { "APPS", "GAMES", 0 };
            static struct fat12_entry ents[64];
            for (int si = 0; search[si] && target == 0xFFFF; si++) {
                uint16_t dir = fs_find_dir(0, search[si]);
                if (dir == 0xFFFF) continue;
                int n = fs_list_dir(dir, ents, 64);
                for (int i = 0; i < n; i++) {
                    const char* a = ents[i].name;
                    const char* b = name;
                    int match = 1;
                    while (*a || *b) {
                        char ca = *a, cb = *b;
                        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
                        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
                        if (ca != cb) { match = 0; break; }
                        if (*a) a++;
                        if (*b) b++;
                    }
                    if (match) { target = dir; break; }
                }
            }
            if (target == 0xFFFF) return -1;
            fs_set_app_cwd(target);
            int64_t rc = (int64_t)loader_spawn(name, args ? args : "");
            fs_set_app_cwd(prev);
            return rc;
        }

        case SYS_LAUNCH_APP_AT: {
            /* Same as SYS_LAUNCH_APP_ASYNC, but the new task's app
             * cwd is overridden to `cwd_cluster` instead of inheriting
             * the BIN's folder. Lets WinFiles (in /DOCS) hand WinImg
             * a bare filename and have it Just Work. */
            const struct launch_at_args* la =
                (const struct launch_at_args*)a1;
            if (!la || !la->name) return -1;
            uint16_t prev = fs_get_app_cwd();
            uint16_t target = 0xFFFF;
            const char* search[] = { "APPS", "GAMES", 0 };
            static struct fat12_entry ents[64];
            for (int si = 0; search[si] && target == 0xFFFF; si++) {
                uint16_t dir = fs_find_dir(0, search[si]);
                if (dir == 0xFFFF) continue;
                int n = fs_list_dir(dir, ents, 64);
                for (int i = 0; i < n; i++) {
                    const char* a = ents[i].name;
                    const char* b = la->name;
                    int match = 1;
                    while (*a || *b) {
                        char ca = *a, cb = *b;
                        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
                        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
                        if (ca != cb) { match = 0; break; }
                        if (*a) a++;
                        if (*b) b++;
                    }
                    if (match) { target = dir; break; }
                }
            }
            if (target == 0xFFFF) return -1;
            fs_set_app_cwd(target);
            int64_t rc = (int64_t)loader_spawn(la->name,
                                               la->args ? la->args : "");
            fs_set_app_cwd(prev);
            if (rc >= 0)
                task_set_cwd_for((task_id)rc, (uint16_t)la->cwd_cluster);
            return rc;
        }

        default:              return -1;
    }
}
