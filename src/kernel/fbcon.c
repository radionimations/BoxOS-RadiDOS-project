/* Framebuffer text console.
 *
 * Reads cells from vga.c's scrollback buffer (via vga_visible_cell)
 * rather than the legacy 0xB8000 text-mode RAM, because BGA's mode
 * switch wipes the legacy region. The scrollback survives.
 *
 * 80x25 cells * 8x8 pixels = 640x200 pixels in the upper portion of
 * a 640x480 screen. Below that we paint a flat band so the BoxOS
 * GUI work can sit there in M2+. */

#include "boxos.h"

/* fbcon now uses the BIOS-supplied 8x16 VGA font via gfx_font_byte().
 * This gives us proper "DOS-looking" character cells instead of the
 * squished 8x8 hand-rolled font used during M1c bring-up. */

#define COLS 80
#define ROWS 25
#define GLYPH_W 8
#define GLYPH_H 16

static bool g_active = false;
static bool g_visible = true;     /* true while a terminal window owns
                                     the screen; false during the
                                     Executive so loader debug prints
                                     don't leak through                */
static int  g_cur_row = 0;
static int  g_cur_col = 0;
static int  g_prev_cur_row = -1;
static int  g_prev_cur_col = -1;
static bool g_cur_visible = true;
static int  g_origin_x = 0;
static int  g_origin_y = 0;

/* Our default palette in fb.c puts the standard 16 VGA colors at
 * indices 0..15, so this is the identity. Kept as a function so the
 * palette can be swapped later without touching the renderer. */
static inline uint8_t vga_to_fb(uint8_t vga_idx) {
    return vga_idx & 0x0F;
}

static void paint_cell(int col, int row, char ch, uint8_t attr) {
    uint8_t* fb = fb_pixels();
    if (!fb) return;
    const struct boot_info* bi = fb_bootinfo();
    int pitch = bi->fb_pitch;

    uint8_t fg = vga_to_fb(attr & 0x0F);
    uint8_t bg = vga_to_fb((attr >> 4) & 0x0F);

    int x0 = g_origin_x + col * GLYPH_W;
    int y0 = g_origin_y + row * GLYPH_H;
    for (int y = 0; y < GLYPH_H; y++) {
        uint8_t row_bits = gfx_font_byte((uint8_t)ch, y);
        uint8_t* dst = fb + (uint64_t)(y0 + y) * pitch + x0;
        for (int x = 0; x < GLYPH_W; x++) {
            uint8_t mask = (uint8_t)(0x80 >> x);
            dst[x] = (row_bits & mask) ? fg : bg;
        }
    }
}

void fbcon_init(void) {
    if (!fb_present()) return;
    g_active = true;
    g_cur_row = 0;
    g_cur_col = 0;
    g_prev_cur_row = -1;
    g_prev_cur_col = -1;
}

void fbcon_set_origin(int x, int y) {
    g_origin_x = x;
    g_origin_y = y;
}

void fbcon_set_visible(bool on) { g_visible = on; }
bool fbcon_visible(void)        { return g_visible; }

bool fbcon_active(void) { return g_active; }

void fbcon_paint_cell(int row, int col) {
    if (!g_active || !g_visible) return;
    if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return;
    uint16_t v = vga_visible_cell(row, col);
    paint_cell(col, row, (char)(v & 0xFF), (uint8_t)(v >> 8));
}

static void paint_cursor_only(void) {
    if (!g_active || !g_visible) return;
    if (g_prev_cur_row >= 0 &&
        g_prev_cur_row < ROWS && g_prev_cur_col < COLS) {
        uint16_t v = vga_visible_cell(g_prev_cur_row, g_prev_cur_col);
        paint_cell(g_prev_cur_col, g_prev_cur_row,
                   (char)(v & 0xFF), (uint8_t)(v >> 8));
    }
    if (g_cur_visible && g_cur_row >= 0 && g_cur_row < ROWS &&
        g_cur_col >= 0 && g_cur_col < COLS) {
        uint8_t* fb = fb_pixels();
        if (fb) {
            const struct boot_info* bi = fb_bootinfo();
            int pitch = bi->fb_pitch;
            int x0 = g_origin_x + g_cur_col * GLYPH_W;
            int y0 = g_origin_y + g_cur_row * GLYPH_H + GLYPH_H - 1;
            uint16_t v = vga_visible_cell(g_cur_row, g_cur_col);
            uint8_t fg = vga_to_fb((uint8_t)(v >> 8) & 0x0F);
            uint8_t* dst = fb + (uint64_t)y0 * pitch + x0;
            for (int x = 0; x < GLYPH_W; x++) dst[x] = fg;
        }
    }
    g_prev_cur_row = g_cur_row;
    g_prev_cur_col = g_cur_col;
}

void fbcon_set_cursor(int col, int row) {
    g_cur_col = col;
    g_cur_row = row;
    paint_cursor_only();
}

void fbcon_repaint(void) {
    if (!g_active || !g_visible) return;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            uint16_t v = vga_visible_cell(r, c);
            paint_cell(c, r, (char)(v & 0xFF), (uint8_t)(v >> 8));
        }
    }
    g_prev_cur_row = -1;
    paint_cursor_only();
}
