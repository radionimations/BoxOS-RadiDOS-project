/* BoxOS GUI drawing primitives.
 *
 * All operations write directly to the linear framebuffer. They are
 * no-ops if the framebuffer hasn't been enabled. Coordinates are
 * pixel-space, top-left origin. */

#include "boxos.h"

extern const unsigned char font_8x8[96][8];

/* ---- helpers ----------------------------------------------------- */

static inline void clip_to_screen(int* x, int* y, int* w, int* h,
                                  int sw, int sh) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > sw) *w = sw - *x;
    if (*y + *h > sh) *h = sh - *y;
}

/* ---- public API -------------------------------------------------- */

void gui_hline(int x, int y, int w, uint8_t color) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    if (y < 0 || y >= bi->fb_height) return;
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w; if (x1 > bi->fb_width) x1 = bi->fb_width;
    if (x1 <= x0) return;
    uint8_t* dst = fb_pixels() + (uint64_t)y * bi->fb_pitch + x0;
    memset(dst, color, (size_t)(x1 - x0));
}

void gui_vline(int x, int y, int h, uint8_t color) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    if (x < 0 || x >= bi->fb_width) return;
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h; if (y1 > bi->fb_height) y1 = bi->fb_height;
    uint8_t* dst = fb_pixels() + (uint64_t)y0 * bi->fb_pitch + x;
    for (int j = y0; j < y1; j++) {
        *dst = color;
        dst += bi->fb_pitch;
    }
}

void gui_rect(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    gui_hline(x,         y,         w, color);
    gui_hline(x,         y + h - 1, w, color);
    gui_vline(x,         y,         h, color);
    gui_vline(x + w - 1, y,         h, color);
}

void gui_fill_rect(int x, int y, int w, int h, uint8_t color) {
    fb_fill_rect(x, y, w, h, color);
}

/* Bevelled rectangle with two-tone borders (Win 1.0-ish chrome).
 * top/left use 'light', bottom/right use 'dark'. */
void gui_bevel(int x, int y, int w, int h,
               uint8_t light, uint8_t dark) {
    if (w <= 0 || h <= 0) return;
    gui_hline(x,         y,         w,     light);
    gui_vline(x,         y,         h - 1, light);
    gui_hline(x,         y + h - 1, w,     dark);
    gui_vline(x + w - 1, y,         h,     dark);
}

/* Render an 8-bit-per-pixel pixmap with a transparent-color key.
 * Pixels equal to `key` are skipped (background shows through);
 * otherwise the source byte is the palette index to write. */
void gui_blit_keyed(int x, int y, int w, int h,
                    const uint8_t* src, int src_pitch, uint8_t key) {
    if (!fb_present() || !src) return;
    const struct boot_info* bi = fb_bootinfo();
    int dx0 = x, dy0 = y, dw = w, dh = h;
    int sx_off = 0, sy_off = 0;
    if (dx0 < 0) { sx_off = -dx0; dw += dx0; dx0 = 0; }
    if (dy0 < 0) { sy_off = -dy0; dh += dy0; dy0 = 0; }
    if (dx0 + dw > bi->fb_width)  dw = bi->fb_width  - dx0;
    if (dy0 + dh > bi->fb_height) dh = bi->fb_height - dy0;
    if (dw <= 0 || dh <= 0) return;
    uint8_t* dst_row = fb_pixels() + (uint64_t)dy0 * bi->fb_pitch + dx0;
    const uint8_t* src_row = src + sy_off * src_pitch + sx_off;
    for (int j = 0; j < dh; j++) {
        for (int i = 0; i < dw; i++) {
            uint8_t s = src_row[i];
            if (s != key) dst_row[i] = s;
        }
        dst_row += bi->fb_pitch;
        src_row += src_pitch;
    }
}

/* Save a rectangular block of framebuffer pixels into `buf`. Used
 * by the cursor sprite to remember what's underneath. */
void gui_save_block(int x, int y, int w, int h, uint8_t* buf, int buf_pitch) {
    if (!fb_present() || !buf) return;
    const struct boot_info* bi = fb_bootinfo();
    int dx0 = x, dy0 = y, dw = w, dh = h;
    int sx_off = 0, sy_off = 0;
    if (dx0 < 0) { sx_off = -dx0; dw += dx0; dx0 = 0; }
    if (dy0 < 0) { sy_off = -dy0; dh += dy0; dy0 = 0; }
    if (dx0 + dw > bi->fb_width)  dw = bi->fb_width  - dx0;
    if (dy0 + dh > bi->fb_height) dh = bi->fb_height - dy0;
    if (dw <= 0 || dh <= 0) return;
    const uint8_t* src_row = fb_pixels() + (uint64_t)dy0 * bi->fb_pitch + dx0;
    uint8_t* dst_row = buf + sy_off * buf_pitch + sx_off;
    for (int j = 0; j < dh; j++) {
        for (int i = 0; i < dw; i++) dst_row[i] = src_row[i];
        src_row += bi->fb_pitch;
        dst_row += buf_pitch;
    }
}

/* Restore a saved block to the framebuffer. */
void gui_restore_block(int x, int y, int w, int h,
                       const uint8_t* buf, int buf_pitch) {
    if (!fb_present() || !buf) return;
    const struct boot_info* bi = fb_bootinfo();
    int dx0 = x, dy0 = y, dw = w, dh = h;
    int sx_off = 0, sy_off = 0;
    if (dx0 < 0) { sx_off = -dx0; dw += dx0; dx0 = 0; }
    if (dy0 < 0) { sy_off = -dy0; dh += dy0; dy0 = 0; }
    if (dx0 + dw > bi->fb_width)  dw = bi->fb_width  - dx0;
    if (dy0 + dh > bi->fb_height) dh = bi->fb_height - dy0;
    if (dw <= 0 || dh <= 0) return;
    uint8_t* dst_row = fb_pixels() + (uint64_t)dy0 * bi->fb_pitch + dx0;
    const uint8_t* src_row = buf + sy_off * buf_pitch + sx_off;
    for (int j = 0; j < dh; j++) {
        for (int i = 0; i < dw; i++) dst_row[i] = src_row[i];
        dst_row += bi->fb_pitch;
        src_row += buf_pitch;
    }
}

/* Draw a single 8x8 glyph at pixel (x, y). bg_key == 0xFF means
 * transparent background — useful for drawing labels over arbitrary
 * surfaces. Otherwise bg is filled too. */
void gui_glyph(int x, int y, char ch, uint8_t fg, uint8_t bg) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    int gi = (unsigned char)ch - 32;
    if (gi < 0 || gi >= 96) gi = 0;
    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || py >= bi->fb_height) continue;
        uint8_t bits = font_8x8[gi][row];
        uint8_t* line = fb_pixels() + (uint64_t)py * bi->fb_pitch;
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= bi->fb_width) continue;
            int lit = bits & (0x80 >> col);
            if (lit) line[px] = fg;
            else if (bg != 0xFF) line[px] = bg;
        }
    }
}

void gui_text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    if (!s) return;
    int cx = x;
    while (*s) {
        gui_glyph(cx, y, *s, fg, bg);
        cx += 8;
        s++;
    }
}
