/* Thin syscall wrappers. Every entry is one inline `int 0x80`.
 * The kernel's int-0x80 path preserves all GPRs except rax, so we
 * don't have to save anything extra here. */

#include "boxos_app.h"

static inline int64_t s0(uint64_t n) {
    int64_t r;
    __asm__ __volatile__ ("int $0x80" : "=a"(r) : "a"(n) : "memory");
    return r;
}
static inline int64_t s1(uint64_t n, uint64_t a) {
    int64_t r;
    __asm__ __volatile__ ("int $0x80" : "=a"(r) : "a"(n), "D"(a) : "memory");
    return r;
}
static inline int64_t s2(uint64_t n, uint64_t a, uint64_t b) {
    int64_t r;
    __asm__ __volatile__ ("int $0x80" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "memory");
    return r;
}
static inline int64_t s3(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    int64_t r;
    __asm__ __volatile__ ("int $0x80"
                          : "=a"(r)
                          : "a"(n), "D"(a), "S"(b), "d"(c)
                          : "memory");
    return r;
}

void  bos_putc(char c)               { s1(1,  (uint64_t)(uint8_t)c); }
void  bos_puts(const char* s)        { s1(2,  (uint64_t)s); }
char  bos_getc(void)                 { return (char)s0(3); }
void  bos_cls(void)                  { s0(4); }
void  bos_set_color(int fg, int bg)  { s2(5,  (uint64_t)fg, (uint64_t)bg); }
void  bos_print_int(int64_t n)       { s1(6,  (uint64_t)n); }
char  bos_try_getc(void)             { return (char)s0(7); }
unsigned int bos_try_get_key(void)   { return (unsigned int)s0(8); }
int   bos_mouse_poll(void* out12)    { return (int)s1(9, (uint64_t)out12); }

void  gfx_mode_text(void)            { s1(10, 0); }
void  gfx_mode_13h(void)             { s1(10, 1); }
void  gfx_blit(const void* p)        { s1(11, (uint64_t)p); }
void  gfx_palette(const void* p)     { s1(12, (uint64_t)p); }

void     sleep_ms(uint64_t ms)       { s1(20, ms); }
uint64_t ticks_ms(void)              { return (uint64_t)s0(21); }

void* xmalloc(uint64_t n)            { return (void*)(uintptr_t)s1(30, n); }
void  xfree(void* p)                 { s1(31, (uint64_t)p); }

int64_t bos_read_file(const char* name, void* buf, uint64_t cap) {
    return s3(40, (uint64_t)name, (uint64_t)buf, cap);
}

int bos_list_dir(unsigned dir_cluster, struct dir_entry* out, int max) {
    return (int)s3(41, (uint64_t)dir_cluster, (uint64_t)out, (uint64_t)max);
}

void bos_status(const char* msg) { s1(50, (uint64_t)msg); }

void  reboot_now(void)               { s0(99); for(;;); }
void  power_off_now(void)            { s0(149); for(;;); }

void sound_tone_on(uint32_t hz)         { s1(55, hz); }
void sound_tone_off(void)               { s0(56); }
void sound_beep(uint32_t hz, uint32_t ms){ s2(57, hz, ms); }
void sound_bell(void)                   { s0(58); }
void sound_ok(void)                     { s0(59); }
void sound_error(void)                  { s0(100); }

int  theme_index(void)                  { return (int)s0(110); }
void theme_set(int i)                   { s1(111, (uint64_t)i); }
int  theme_count(void)                  { return (int)s0(112); }
int  theme_name(int i, char* buf, int cap){
    return (int)s3(113, (uint64_t)i, (uint64_t)buf, (uint64_t)cap);
}
int  sound_enabled(void)                { return (int)s0(114); }
void sound_enable(int on)               { s1(115, (uint64_t)on); }

void     wm_set_title(const char* s)    { s1(120, (uint64_t)s); }
uint64_t heap_used(void)                { return (uint64_t)s0(121); }
uint64_t heap_total(void)               { return (uint64_t)s0(122); }
void     halt_now(void)                 { s0(123); for(;;); }

int  bos_delete_file(const char* name)             { return (int)s1(124, (uint64_t)name); }
int  bos_create_file(const char* name, const void* buf, uint64_t size) {
    return (int)s3(125, (uint64_t)name, (uint64_t)buf, size);
}
int  bos_save_to_docs(const char* name, const void* buf, uint64_t size) {
    return (int)s3(126, (uint64_t)name, (uint64_t)buf, size);
}
int  bos_save_to_folder(const char* folder, const char* name,
                        const void* buf, uint64_t size) {
    struct { const char* folder; const char* name;
             const void* buf;    uint32_t size; } args =
        { folder, name, buf, (uint32_t)size };
    return (int)s1(127, (uint64_t)&args);
}

int  bos_launch_app(const char* name, const char* args) {
    return (int)s2(130, (uint64_t)name, (uint64_t)args);
}

/* -------- GUI / windows ------------------------------------- */

int gui_poll_event(struct gui_event* out) {
    return (int)s1(70, (uint64_t)out);
}

int gui_open_window(const char* title, int x, int y, int w, int h) {
    struct { const char* t; int x, y, w, h; } args = { title, x, y, w, h };
    return (int)s1(71, (uint64_t)&args);
}

void gui_close_window(int handle) { s1(72, (uint64_t)handle); }

void gui_fill_rect(int handle, int x, int y, int w, int h, int color) {
    struct { int handle, x, y, w, h; uint8_t color; } args =
        { handle, x, y, w, h, (uint8_t)color };
    s1(73, (uint64_t)&args);
}

void gui_text(int handle, int x, int y, const char* s, int fg, int bg) {
    struct { int handle, x, y; const char* s; uint8_t fg, bg; } args =
        { handle, x, y, s, (uint8_t)fg, (uint8_t)bg };
    s1(74, (uint64_t)&args);
}

void gui_size(int handle, int* out_w, int* out_h) {
    s3(75, (uint64_t)handle, (uint64_t)out_w, (uint64_t)out_h);
}

int bos_sb16_present(void)  { return (int)s1(62, 0); }
int bos_sb16_playing(void)  { return (int)s1(65, 0); }
void bos_sb16_stop(void)    { s1(64, 0); }
int bos_sb16_play(const void* pcm, uint32_t len, uint32_t rate) {
    struct { const void* pcm; uint32_t len; uint32_t rate; } args = { pcm, len, rate };
    return (int)s1(63, (uint64_t)&args);
}

int bos_launch_app_at(const char* name, const char* args, unsigned cwd_cluster) {
    struct { const char* name; const char* args; uint32_t cwd; } a =
        { name, args ? args : "", (uint32_t)cwd_cluster };
    return (int)s1(131, (uint64_t)&a);
}

int bos_inst_drives(void)            { return (int)s1(140, 0); }
int bos_inst_boot_drv(void)          { return (int)s1(141, 0); }
int bos_inst_start(int target_idx)   { return (int)s1(142, (uint64_t)target_idx); }
int bos_inst_chunk(void)             { return (int)s1(143, 0); }
int bos_inst_percent(void)           { return (int)s1(144, 0); }
int bos_inst_finalize(int target_idx, const char* name, const char* company) {
    struct { uint32_t idx; const char* name; const char* company; } a =
        { (uint32_t)target_idx, name ? name : "", company ? company : "" };
    return (int)s1(145, (uint64_t)&a);
}
int bos_factory_reset(void) { return (int)s1(146, 0); }
int bos_inst_err_phase(void) { return (int)s1(147, 0); }
int bos_boot_drive_writable(void) { return (int)s1(148, 0); }
