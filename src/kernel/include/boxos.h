#ifndef BOXOS_H
#define BOXOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------------------- VGA text mode console ------------------- */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE,
};

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c);
void vga_puts(const char* s);
void vga_printf(const char* fmt, ...);
void vga_status_line(const char* msg);
const char* vga_log_line(int n);  /* 0 = oldest, returns "" if empty */
int  vga_log_lines(void);          /* number of valid lines in the log */
void vga_log_set_disk(int bus, int drive);  /* enable disk-backed log */

/* --------------------- Port I/O helpers ---------------------- */

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ __volatile__ ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ __volatile__ ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ------------------ Interrupts (IDT / PIC) ------------------- */

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void idt_init(void);

void pic_remap(uint8_t off1, uint8_t off2);
void pic_mask_all(void);
void pic_unmask(uint8_t irq);
void pic_send_eoi(uint8_t irq);

/* --------------------- PS/2 keyboard ------------------------- */

void keyboard_init(void);
void keyboard_irq(void);
char keyboard_getc(void);

/* ----------------------- ATA PIO ----------------------------- */

/* bus = 0 (primary @ 0x1F0) or 1 (secondary @ 0x170)
 * drive = 0 (master) or 1 (slave) */
int  ata_identify(int bus, int drive);                 /* 0 ok, <0 none */
int  ata_drive_type(int bus, int drive);               /* 0=none, 1=ATA, 2=ATAPI */
int  ata_read(int bus, int drive, uint32_t lba, uint8_t count, void* buf);
int  ata_write(int bus, int drive, uint32_t lba, uint8_t count, const void* buf);

/* --------------------- FAT12 read-only ----------------------- */

#define FAT12_MAX_NAME 13   /* 8.3 + dot + nul */

struct fat12_entry {
    char     name[FAT12_MAX_NAME];   /* "HELLO.BIN" */
    uint32_t size;
    uint16_t first_cluster;
    uint8_t  attr;
};

int  fs_mount(void);                                   /* probes drives, picks the FAT12 */
int  fs_drive(void);                                   /* returns the drive index in use */
int  fs_list(struct fat12_entry* out, int max);        /* root, files only (compat) */
int  fs_list_dir(uint16_t dir_cluster, struct fat12_entry* out, int max);
int  fs_read_file(const char* name83, void* buf, uint32_t cap);
int  fs_read_in(uint16_t dir_cluster, const char* name83, void* buf, uint32_t cap);
uint16_t fs_find_dir(uint16_t parent, const char* name); /* 0xFFFF if not found */
void     fs_set_app_cwd(uint16_t cluster);
uint16_t fs_get_app_cwd(void);

/* Write paths. Return 0 on success, negative on error. fs_rmdir
 * returns -2 if the directory is non-empty. */
int  fs_create_file(uint16_t dir_cluster, const char* name, const void* data, uint32_t size);
int  fs_delete    (uint16_t dir_cluster, const char* name);
int  fs_mkdir_at  (uint16_t parent_cluster, const char* name);
int  fs_rmdir     (uint16_t dir_cluster, const char* name);

/* Copy the factory FAT12 backup over the live filesystem. Returns 0 on
 * success, -1 if no backup exists (legacy layout / not the combined
 * image), -2 on I/O error. */
int  fs_factory_restore(void);

/* Cross-disk install. Pick a target (different ATA bus/drive), then
 * call fs_install_chunk in a loop (0 = continue, 1 = done, <0 error).
 * fs_install_percent reads 0..100 between calls so callers can paint
 * a progress bar. */
int  fs_install_start(int bus, int drive);
int  fs_install_chunk(void);
int  fs_install_percent(void);
int  fs_install_err_phase(void);    /* 0 ok, 1 = read media, 2 = write target */
int  fs_install_finalize(int target_bus, int target_drive,
                         const char* name, const char* company);
int  fs_ata_present_mask(void);     /* bit i = bus(i/2):drv(i&1) responded */
int  fs_boot_bus(void);
int  fs_boot_drive(void);

/* --------------------- App loader / shell -------------------- */

#define APP_LOAD_ADDR 0x200000ULL

int  loader_run(const char* name, const char* args);   /* returns app rax */
int  loader_spawn(const char* name, const char* args);  /* non-blocking */

/* EXE inspector. Returns 0 on success, -1 if not an EXE we recognise. */
struct exe_info {
    int      kind;
    uint32_t hdr_offset;
    uint32_t image_size;
    uint16_t mz_cs;
    uint16_t mz_ip;
    uint16_t pe_machine;
    uint16_t pe_subsystem;
    uint16_t pe_dll;
    uint32_t pe_entry_rva;
    uint16_t ne_target_os;
    uint16_t ne_n_segments;
};
#define EXE_KIND_NONE 0
#define EXE_KIND_MZ   1
#define EXE_KIND_NE   2
#define EXE_KIND_PE   3
#define EXE_KIND_LE   4
#define EXE_KIND_LX   5

int         exe_inspect(const char* name, struct exe_info* out);
const char* exe_kind_label(const struct exe_info* info);
const char* exe_pe_machine_label(uint16_t m);
const char* exe_pe_subsystem_label(uint16_t s);
const char* exe_runnable_message(const struct exe_info* info);

void shell_run(void);
/* First-boot text-mode setup. Runs before splash_show on a fresh
 * disk (no /SYS/INSTALL.CFG). Lists drives, lets the user scandisk
 * / wipe / mark, then returns control so the graphical wizard can
 * take over. */
void shell_radidos_setup(void);

/* --------------------- Syscall dispatch ---------------------- */

int64_t syscall_dispatch(struct interrupt_frame* f);

/* ----------------------- Timer (PIT) ------------------------- */

void     timer_init(uint32_t hz);
void     timer_tick(void);                /* called from IRQ0 */
uint64_t timer_ticks(void);
uint64_t timer_ms(void);
void     timer_sleep_ms(uint64_t ms);

/* ---------------------- Bump heap ---------------------------- */

void   heap_init(void);
void*  heap_alloc(size_t bytes);
void*  heap_alloc_aligned(size_t bytes, size_t align);
void   heap_free(void* p);
size_t heap_used(void);
size_t heap_total(void);

/* ---------------------- VGA mode 13h ------------------------- */

void gfx_init(void);                         /* snapshot the BIOS font */
uint8_t gfx_font_byte(uint8_t ch, int row);  /* BIOS 8x16 glyph row    */
void gfx_app_cleanup(void);                  /* tear down gfx window if app left it open */
int  gfx_in_app_mode(void);                  /* 1 while a fullscreen gfx app owns the framebuffer */
void wm_repaint_all(void);                   /* desktop + every chrome + post WM_PAINT to each app */
void wm_repaint_taskbar(void);               /* Re-stamp the bottom taskbar (after gfx-app exit). */
int  wm_taskbar_height(void);
int  wm_start_button_w(void);
int  wm_hit_start_button(int x, int y);      /* 1 if (x,y) is on the Start button. */

/* Sound Blaster 16 (8-bit mono PCM). */
int  sb16_init(void);
int  sb16_present(void);
int  sb16_playing(void);
int  sb16_play(const uint8_t* pcm, uint32_t len, uint32_t rate_hz);
void sb16_stop(void);
void sb16_irq(void);
void gfx_set_mode_13h(void);
void gfx_set_text_mode(void);
void gfx_blit(const void* src);              /* 320*200 bytes */
void gfx_set_palette(const void* pal);       /* 768 bytes (R,G,B *256) */

/* -------------------- VBE linear framebuffer ----------------- */
/* BootInfo struct is written by stage 2 at phys 0x500. fb.c
 * reads it on kernel entry and exposes the framebuffer geometry
 * to the rest of the kernel. fb_present() returns false if the
 * BIOS does not advertise a usable VBE 640x480x8 mode — callers
 * should fall back to text mode in that case. */
struct boot_info {
    uint32_t magic;       /* 'BOX1' = 0x31584F42 */
    uint64_t fb_addr;     /* linear framebuffer phys addr, 0 = none */
    uint32_t fb_pitch;    /* bytes per scanline */
    uint16_t fb_width;
    uint16_t fb_height;
    uint8_t  fb_bpp;
    uint8_t  reserved;
    uint16_t fb_mode;     /* VBE mode number (0 = none) */
} __attribute__((packed));

void fb_init(void);
bool fb_present(void);
const struct boot_info* fb_bootinfo(void);

/* Switch into the detected graphics mode (BGA path) and map the
 * linear framebuffer into the kernel address space. Returns 0 on
 * success, negative if the framebuffer is not present. After this
 * call the text-mode console at 0xB8000 is no longer visible — the
 * framebuffer console (M1c) takes over rendering. */
int  fb_enable(void);
void fb_fill(uint8_t color);
void fb_fill_rect(int x, int y, int w, int h, uint8_t color);
uint8_t* fb_pixels(void);   /* base of the mapped framebuffer */
void fb_reload_palette(void);    /* re-apply the OS default palette */

/* PC speaker (single-voice square wave). Apps reach these via the
 * SYS_SOUND_* syscalls; the kernel uses them for system bells. */
void sound_tone_on(uint32_t freq_hz);
void sound_tone_off(void);
void sound_beep_ms(uint32_t freq_hz, uint32_t ms);
void sound_set_enabled(int on);
int  sound_is_enabled(void);
void sound_bell(void);
void sound_chord_ok(void);
void sound_chord_error(void);

/* Framebuffer text console (mirrors the 80x25 logical text grid).
 * Reads cells via vga_visible_cell() so we don't depend on the
 * legacy 0xB8000 buffer (which gets wiped by BGA's mode switch). */
void fbcon_init(void);
bool fbcon_active(void);
void fbcon_repaint(void);
void fbcon_paint_cell(int row, int col);
void fbcon_set_cursor(int col, int row);
void fbcon_set_origin(int x, int y);
void fbcon_set_visible(bool on);  /* false outside terminal windows  */
bool fbcon_visible(void);

/* Window manager. wm_init paints just the menu bar + a flat desktop
 * background. The Terminal is now an app — see wm_open_terminal(). */
void wm_init(void);
void wm_set_title(const char* s);
const char* wm_get_title(void);
void wm_paint_clock(uint64_t uptime_s);
void wm_open_terminal(void);
void wm_open_terminal_with(const char* path);   /* run/view + wait    */

/* M6: BoxOS splash + desktop launcher. desktop_loop() returns when
 * the user presses Esc, at which point the caller should fall
 * through to shell_run(). */
void splash_show(void);
void desktop_loop(void);

/* OS theme — palette indices for chrome elements. WINSETT swaps
 * between presets at runtime by replacing the active theme and
 * triggering a desktop repaint. */
struct os_theme {
    const char* name;
    uint8_t menu_bg;       /* top menu bar background      */
    uint8_t menu_fg;       /* menu bar text                */
    uint8_t title_bg;      /* dialog/window title bars     */
    uint8_t title_fg;      /* title bar text               */
    uint8_t desktop_bg;    /* main desktop colour          */
    uint8_t accent;        /* selection / hilight          */
    uint8_t status_bg;     /* status bar bg                */
    uint8_t status_fg;     /* status bar fg                */
};

const struct os_theme* theme_active(void);
int  theme_count(void);
const struct os_theme* theme_at(int i);
void theme_set(int i);
int  theme_index(void);

/* GUI events: a single struct, polled via gui_poll_event(). Returns
 * 1 if an event was waiting, 0 otherwise. Keyboard events are
 * priority over mouse — that matches how apps usually want to react.
 * x/y on mouse events are the cursor position at event time. */
#define GUI_EV_NONE        0
#define GUI_EV_KEY         1   /* arg1 = (pressed<<8)|ascii          */
#define GUI_EV_MOUSE_MOVE  2
#define GUI_EV_MOUSE_DOWN  3   /* arg1 = button bitmask (1=L,2=R,4=M)*/
#define GUI_EV_MOUSE_UP    4
#define GUI_EV_PAINT       5   /* WM asks you to redraw your window  *
                                * after another window above closed  *
                                * or moved. arg1 = window handle.    */

struct gui_event {
    uint32_t type;
    int32_t  x;
    int32_t  y;
    uint32_t arg1;
    uint32_t arg2;
};

int  gui_poll_event(struct gui_event* out);
uint32_t mouse_buttons(void);
void keyboard_drain(void);
void keyboard_inject_key(uint8_t ascii);

/* Dynamic windows for apps. Coords for fill_rect / text are content-
 * local; the WM clips to the content rect. */
int  wm_open_window(const char* title, int x, int y, int w, int h);
void wm_close_window(int handle);
void wm_w_fill_rect(int handle, int x, int y, int w, int h, uint8_t color);
void wm_w_text(int handle, int x, int y, const char* s, uint8_t fg, uint8_t bg);
void wm_w_size(int handle, int* out_w, int* out_h);
int  wm_window_origin(int handle, int* out_x, int* out_y);

/* Drag protocol used by the Executive's mouse loop. */
int  wm_drag_begin(int mx, int my);   /* 1 if started, 0 if not on a title */
int  wm_drag_active(void);
void wm_drag_to(int mx, int my);
void wm_drag_end(void);

/* Register a callback that paints the desktop background. The WM
 * calls it before re-stamping window chromes after a drag, so what
 * was underneath the moved window gets repainted. */
void wm_set_bg_repaint(void (*fn)(void));

/* Read the cell currently visible at (row, col) of the 80x25 grid.
 * Sourced from the scrollback buffer so it works even if 0xB8000
 * has been cleared by a graphics mode switch. */
uint16_t vga_visible_cell(int row, int col);

/* GUI drawing primitives. All ops are no-ops if fb_enable() hasn't
 * succeeded. Coords are pixel-space, top-left origin. */
void gui_hline(int x, int y, int w, uint8_t color);
void gui_vline(int x, int y, int h, uint8_t color);
void gui_rect(int x, int y, int w, int h, uint8_t color);
void gui_fill_rect(int x, int y, int w, int h, uint8_t color);
void gui_bevel(int x, int y, int w, int h, uint8_t light, uint8_t dark);
void gui_blit_keyed(int x, int y, int w, int h,
                    const uint8_t* src, int src_pitch, uint8_t key);
void gui_save_block(int x, int y, int w, int h, uint8_t* buf, int buf_pitch);
void gui_restore_block(int x, int y, int w, int h,
                       const uint8_t* buf, int buf_pitch);
void gui_glyph(int x, int y, char ch, uint8_t fg, uint8_t bg);
void gui_text(int x, int y, const char* s, uint8_t fg, uint8_t bg);

/* Software mouse cursor. Pass 0xFF as bg to gui_glyph for transparent. */
void cursor_init(void);
void cursor_show(void);
void cursor_hide(void);
void cursor_handle_mouse(int dx, int dy);
void cursor_set_busy(int on);          /* hourglass when on */
int  cursor_x(void);
int  cursor_y(void);

/* Scroll-back navigation. PageUp/PageDown call these from the
 * keyboard IRQ; new output snaps the view back to live. */
void vga_scroll_up(int rows);
void vga_scroll_down(int rows);
void vga_scroll_to_bottom(void);
bool vga_is_scrolled(void);

/* Non-blocking keyboard read: 0 if no key pending, else char. */
char keyboard_try_getc(void);

/* 0 if no event, else (pressed<<8)|ascii. */
uint16_t keyboard_try_get_event(void);

/* --------------------- PS/2 mouse ---------------------------- */

void mouse_init(void);
void mouse_irq(void);
/* Drain accumulated motion + buttons. Caller passes 12 bytes:
 *   int32 dx, int32 dy, uint32 buttons (bit 0 left, 1 right, 2 mid).
 * Returns 1 if there was new data since last poll, else 0. Movement
 * accumulates across polls; buttons are level-sampled. */
int mouse_poll(void* out12);

/* ----------------- libc-ish primitives ----------------------- */

void* memset(void* dst, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int   strcmp(const char* a, const char* b);
int   strncmp(const char* a, const char* b, size_t n);
int   strcasecmp(const char* a, const char* b);
char* strstr(const char* haystack, const char* needle);

#endif
