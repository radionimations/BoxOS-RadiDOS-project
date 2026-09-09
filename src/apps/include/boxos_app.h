/* BoxOS app-side API.
 * --------------------------------------------------------------
 * Apps are flat 64-bit binaries linked at 0x200000. They reach
 * into the kernel via `int 0x80` (rax = syscall number) — these
 * wrappers hide that. Include this header and link with the
 * Makefile's app pipeline; you get C, with no other dependencies.
 *
 * Calling convention for app entry:
 *   int app_main(const char* args);   // args = string after RUN
 */

#ifndef BOXOS_APP_H
#define BOXOS_APP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -------- text I/O ------------------------------------------- */
void  bos_putc(char c);
void  bos_puts(const char* s);
char  bos_getc(void);              /* blocks                     */
char  bos_try_getc(void);          /* 0 if no key                */
unsigned int bos_try_get_key(void); /* 0 if no event, else (pressed<<8)|ascii */
/* Drains accumulated mouse motion + buttons into out (12 bytes:
 * int32 dx, int32 dy, uint32 buttons). Returns 1 if there was data. */
int   bos_mouse_poll(void* out12);
void  bos_cls(void);
void  bos_set_color(int fg, int bg);
void  bos_print_int(int64_t n);

/* -------- graphics (320x200x256 mode 13h) -------------------- */
#define GFX_W 320
#define GFX_H 200

void  gfx_mode_text(void);
void  gfx_mode_13h(void);
void  gfx_blit(const void* fb_64000_bytes);
void  gfx_palette(const void* pal_768_bytes);   /* RGB *256, 0..63 */

/* -------- timing --------------------------------------------- */
void     sleep_ms(uint64_t ms);
uint64_t ticks_ms(void);

/* -------- heap ----------------------------------------------- */
void* xmalloc(uint64_t n);
void  xfree(void* p);

/* -------- file I/O ------------------------------------------- */
/* Read entire file `name` (8.3, e.g. "DOOM1.WAD") into `buf`,
 * up to `cap` bytes. Returns bytes read, or -1 on error / too big. */
int64_t bos_read_file(const char* name, void* buf, uint64_t cap);

/* Single directory entry. attr bit 0x10 = subdirectory. The first
 * cluster is enough to recursively call bos_list_dir for sub-folders. */
struct dir_entry {
    char     name[13];      /* 8.3 + dot + nul */
    uint32_t size;
    uint16_t first_cluster;
    uint8_t  attr;
};

/* List `dir_cluster` (0 = root). Fills up to `max` entries; returns
 * the count, or -1 on error. */
int bos_list_dir(unsigned dir_cluster, struct dir_entry* out, int max);

/* Overwrite the reserved bottom-of-screen status row with `msg`.
 * Pads the rest of the row with spaces. Cursor is preserved.
 * Use this for "what's happening now" updates that shouldn't pollute
 * the scrollback. */
void bos_status(const char* msg);

/* Hard reset the machine via the 8042 keyboard controller. Convenient
 * for graphics apps that don't have a reliable way to clean up VGA
 * state on exit. */
void  reboot_now(void);

/* Cleanly power the machine off (ACPI S5). Used at the end of Setup so
 * the next launch is a fresh boot off the freshly-installed disk —
 * more reliable than a guest reset, which can dead-end in the BIOS. */
void  power_off_now(void);

/* -------- GUI windows ---------------------------------------- */

#define GUI_EV_NONE        0
#define GUI_EV_KEY         1   /* arg1 = (pressed<<8)|ascii          */
#define GUI_EV_MOUSE_MOVE  2
#define GUI_EV_MOUSE_DOWN  3   /* arg1 = button mask (1=L,2=R,4=M)   */
#define GUI_EV_MOUSE_UP    4
#define GUI_EV_PAINT       5   /* WM asks you to redraw your window  */

struct gui_event {
    uint32_t type;
    int32_t  x;
    int32_t  y;
    uint32_t arg1;
    uint32_t arg2;
};

/* 1 = event filled in, 0 = none waiting. Keyboard events take
 * priority over mouse moves so apps stay responsive to typing. */
int  gui_poll_event(struct gui_event* out);

int  gui_open_window(const char* title, int x, int y, int w, int h);
void gui_close_window(int handle);
void gui_fill_rect(int handle, int x, int y, int w, int h, int color);
void gui_text(int handle, int x, int y, const char* s, int fg, int bg);
void gui_size(int handle, int* out_w, int* out_h);

/* -------- sound (PC speaker) --------------------------------- */
void sound_tone_on(uint32_t hz);
void sound_tone_off(void);
void sound_beep(uint32_t hz, uint32_t ms);
void sound_bell(void);
void sound_ok(void);
void sound_error(void);
int  sound_enabled(void);
void sound_enable(int on);

/* Sound Blaster 16 (8-bit mono PCM). */
int  bos_sb16_present(void);
int  bos_sb16_playing(void);
int  bos_sb16_play(const void* pcm, uint32_t len, uint32_t rate);
void bos_sb16_stop(void);

/* Cross-disk install (Arise Setup). Workflow:
 *   mask = bos_inst_drives();            // 4-bit map of present ATA drives
 *   boot = bos_inst_boot_drv();          // (bus<<1)|drive of the booted disk
 *   bos_inst_start(target_idx);          // pick a non-boot drive
 *   while ((rc = bos_inst_chunk()) == 0) { paint_progress(bos_inst_percent()); }
 * rc == 1 means done, rc < 0 means I/O failure. */
int  bos_inst_drives(void);
int  bos_inst_boot_drv(void);
int  bos_inst_start(int target_idx);
int  bos_inst_chunk(void);
int  bos_inst_percent(void);
/* After a failed bos_inst_chunk: 1 = read error from install media,
 * 2 = write error to the target drive (e.g. disk too small). */
int  bos_inst_err_phase(void);
/* 1 if the disk we booted from is a writable ATA drive (so an
 * in-place install is possible); 0 if it's a read-only CD/El Torito. */
int  bos_boot_drive_writable(void);
/* After the byte-copy completes, re-mount the target's FAT12 and
 * stamp /SYS/INSTALL.CFG + /SYS/SETUP.DONE so the next boot from
 * that drive goes straight to the desktop. */
int  bos_inst_finalize(int target_idx, const char* name, const char* company);
/* Wipe the live FAT12 back to the factory image embedded in the
 * boot disk (the "format C: first" option in Setup). Re-mounts on
 * success so subsequent writes hit the fresh filesystem. */
int  bos_factory_reset(void);

/* -------- theme --------------------------------------------- */
int  theme_index(void);
void theme_set(int i);
int  theme_count(void);
int  theme_name(int i, char* buf, int cap);

/* -------- system queries / chrome ----------------------------- */
void     wm_set_title(const char* s);   /* updates the menu bar title  */
uint64_t heap_used(void);
uint64_t heap_total(void);
void     halt_now(void);                /* cli;hlt forever              */

/* -------- file write ----------------------------------------- */
int  bos_delete_file(const char* name);                 /* 0 on success */
int  bos_create_file(const char* name, const void* buf, uint64_t size);
int  bos_save_to_docs(const char* name, const void* buf, uint64_t size);
/* folder: "" / NULL / "/" = root, otherwise a top-level dir name (DOCS, APPS, ...) */
int  bos_save_to_folder(const char* folder, const char* name,
                        const void* buf, uint64_t size);

/* -------- launching other apps ------------------------------- *
 * Spawns the named .BIN as a new task with a private user-space
 * (deep PML4 clone). Non-blocking: returns the spawned task id
 * (>= 0) immediately so the caller can keep running. */
int  bos_launch_app(const char* name, const char* args);
/* Like bos_launch_app, but pins the new task's cwd to `cwd_cluster`
 * instead of inheriting it from the BIN's folder. Lets a launcher in
 * folder X open a file in folder X via a bare filename, even when the
 * target app lives in /APPS. */
int  bos_launch_app_at(const char* name, const char* args, unsigned cwd_cluster);

/* -------- libc-ish ------------------------------------------- */
void* memset(void* dst, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);

#endif
