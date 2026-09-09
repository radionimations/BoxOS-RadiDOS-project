/* Mouse cursor sprite.
 *
 * Maintains a software cursor: a 16x16 arrow drawn directly into the
 * framebuffer with a save-under-it buffer so we can restore the
 * background when the cursor moves. The mouse IRQ calls
 * cursor_handle_mouse() with each delta packet, we update position,
 * and re-draw.
 *
 * Per-cell paints from fbcon don't try to hide/restore the cursor;
 * the cursor will simply re-stamp itself on the next mouse move.
 * Acceptable for a CLI-mode kernel; the WM in M3+ will tighten this. */

#include "boxos.h"

#define CW 16   /* cursor width  */
#define CH 16   /* cursor height */

/* Compact 1-char-per-pixel sprite. '.' = transparent, '#' = black,
 * 'W' = white. Easy to hand-edit. Origin (hot-spot) is the top-left. */
static int g_busy = 0;

static const char hourglass_sprite[CH][CW + 1] = {
    "################",
    "#WWWWWWWWWWWWWW#",
    ".#WWWWWWWWWWWW#.",
    "..#WWWWWWWWWW#..",
    "...#W########W#.",
    "....#WW####WW#..",
    ".....#WW##WW#...",
    "......#W##W#....",
    "......#W##W#....",
    ".....#WWWWWW#...",
    "....#W#WWWW#W#..",
    "...#W##WWWW##W#.",
    "..#W###WWWW###W#",
    ".#W####WWWW####W",
    "################",
    "................",
};

static const char arrow_sprite[CH][CW + 1] = {
    "#...............",
    "##..............",
    "#W#.............",
    "#WW#............",
    "#WWW#...........",
    "#WWWW#..........",
    "#WWWWW#.........",
    "#WWWWWW#........",
    "#WWWWWWW#.......",
    "#WWWWWWWW#......",
    "#WWWW######.....",
    "#WW#W#..........",
    "#W#.#W#.........",
    "##..#W#.........",
    "#....#W#........",
    "......##........",
};

static uint8_t saved_bg[CW * CH];
static int  cur_x = 320;
static int  cur_y = 240;
static int  saved_x = -1;
static int  saved_y = -1;
static bool active = false;
static bool visible = false;

static void hide_at(int x, int y);
static void show_at(int x, int y);

static uint8_t pixel_color(int sx, int sy) {
    char c = g_busy ? hourglass_sprite[sy][sx] : arrow_sprite[sy][sx];
    if (c == '#') return 0;     /* palette: black */
    if (c == 'W') return 15;    /* palette: white */
    return 0xFF;                /* sentinel: transparent */
}

void cursor_set_busy(int on) {
    if (g_busy == !!on) return;
    g_busy = on ? 1 : 0;
    /* Force a redraw at the current location so the new sprite
     * shows immediately, even if the mouse hasn't moved. */
    if (active && visible) {
        hide_at(saved_x, saved_y);
        show_at(cur_x, cur_y);
    }
}

static void hide_at(int x, int y) {
    if (!active || x < 0 || y < 0) return;
    gui_restore_block(x, y, CW, CH, saved_bg, CW);
}

static void show_at(int x, int y) {
    if (!active) return;
    gui_save_block(x, y, CW, CH, saved_bg, CW);
    /* Draw the sprite over the saved background. */
    for (int j = 0; j < CH; j++) {
        for (int i = 0; i < CW; i++) {
            uint8_t c = pixel_color(i, j);
            if (c != 0xFF) {
                /* Draw exactly one pixel via gui_blit_keyed shape: do
                 * inline since we don't want the call overhead. */
                const struct boot_info* bi = fb_bootinfo();
                int px = x + i, py = y + j;
                if (px < 0 || px >= bi->fb_width)  continue;
                if (py < 0 || py >= bi->fb_height) continue;
                uint8_t* dst = fb_pixels() + (uint64_t)py * bi->fb_pitch + px;
                *dst = c;
            }
        }
    }
    saved_x = x;
    saved_y = y;
    visible = true;
}

void cursor_init(void) {
    if (!fb_present()) return;
    active = true;
    saved_x = -1;
    saved_y = -1;
    visible = false;
    cur_x = fb_bootinfo()->fb_width  / 2;
    cur_y = fb_bootinfo()->fb_height / 2;
    show_at(cur_x, cur_y);
}

void cursor_hide(void) {
    if (!active || !visible) return;
    hide_at(saved_x, saved_y);
    visible = false;
    saved_x = -1;
    saved_y = -1;
}

void cursor_show(void) {
    if (!active) return;
    /* The caller may have painted over wherever we were last drawn,
     * making saved_bg stale. Drop the saved state unconditionally
     * so show_at re-snapshots the real background under the cursor. */
    visible = false;
    saved_x = -1;
    saved_y = -1;
    show_at(cur_x, cur_y);
}

void cursor_handle_mouse(int dx, int dy) {
    if (!active) return;
    const struct boot_info* bi = fb_bootinfo();
    int nx = cur_x + dx;
    int ny = cur_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > bi->fb_width  - 1) nx = bi->fb_width  - 1;
    if (ny > bi->fb_height - 1) ny = bi->fb_height - 1;
    if (nx == cur_x && ny == cur_y) return;
    if (visible) hide_at(saved_x, saved_y);
    cur_x = nx;
    cur_y = ny;
    show_at(cur_x, cur_y);
}

int cursor_x(void) { return cur_x; }
int cursor_y(void) { return cur_y; }
