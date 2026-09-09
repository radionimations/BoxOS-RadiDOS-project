/* WINPAINT — drawing canvas with BMP save.
 *
 * Layout:
 *   Top: 16-colour palette swatches (32x18 each).
 *   Below: brush-size buttons + CLEAR + SAVE.
 *   Canvas: 320x180 pixels (8-bit indexed, palette = boot palette).
 *
 * Pixels are kept in a backing buffer in addition to being painted
 * to the window, so SAVE can serialize the canvas as a flat BMP file
 * (PAINT.BMP) in the current Executive directory.
 *
 * Esc quits.  C clears.  S saves.  Click palette/brush/clear/save
 * buttons or just drag inside the canvas to paint. */

#include "boxos_app.h"
#include "save_dialog.h"

#define CANVAS_W 320
#define CANVAS_H 180

#define PAL_H    22
#define BAR_H    18
#define MARGIN    4

#define WIN_W    (CANVAS_W + MARGIN * 2)
#define WIN_H    (PAL_H + BAR_H + CANVAS_H + MARGIN * 3)

static int win;
static int cw, ch;
static int win_x = 110, win_y = 90;

static int sel_color = 4;
static int brush_size = 4;

/* Backing buffer for SAVE — palette indices, top-down. We mirror
 * every paint so reading back is O(1). */
static uint8_t canvas[CANVAS_W * CANVAS_H];

/* Boot palette (matches fb.c's load_default_palette). 16 standard
 * VGA colours + 6x6x6 cube + 24-step grey ramp. */
static void make_palette_rgb(uint8_t* rgb_768) {
    static const uint8_t base16[16][3] = {
        {  0,   0,   0}, {  0,   0,  42}, {  0,  42,   0}, {  0,  42,  42},
        { 42,   0,   0}, { 42,   0,  42}, { 42,  21,   0}, { 42,  42,  42},
        { 21,  21,  21}, { 21,  21,  63}, { 21,  63,  21}, { 21,  63,  63},
        { 63,  21,  21}, { 63,  21,  63}, { 63,  63,  21}, { 63,  63,  63},
    };
    int o = 0;
    for (int i = 0; i < 16; i++) {
        rgb_768[o++] = base16[i][0];
        rgb_768[o++] = base16[i][1];
        rgb_768[o++] = base16[i][2];
    }
    for (int r = 0; r < 6; r++)
      for (int g = 0; g < 6; g++)
        for (int b = 0; b < 6; b++) {
            rgb_768[o++] = (uint8_t)(r * 12);
            rgb_768[o++] = (uint8_t)(g * 12);
            rgb_768[o++] = (uint8_t)(b * 12);
        }
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(2 + i * 2);
        rgb_768[o++] = v; rgb_768[o++] = v; rgb_768[o++] = v;
    }
}

/* ---- painting --------------------------------------------------- */

static void paint_palette(void) {
    int sw = (cw - MARGIN * 2) / 16;
    for (int i = 0; i < 16; i++) {
        int x = MARGIN + i * sw;
        gui_fill_rect(win, x, MARGIN, sw - 2, PAL_H - MARGIN, (uint8_t)i);
        if (i == sel_color) {
            /* highlight border */
            int b = 2;
            gui_fill_rect(win, x, MARGIN, sw - 2, b, 15);
            gui_fill_rect(win, x, MARGIN + PAL_H - MARGIN - b, sw - 2, b, 15);
            gui_fill_rect(win, x, MARGIN, b, PAL_H - MARGIN, 15);
            gui_fill_rect(win, x + sw - 2 - b, MARGIN, b, PAL_H - MARGIN, 15);
        }
    }
}

static void paint_toolbar(void) {
    int y = PAL_H + MARGIN;
    gui_fill_rect(win, 0, y, cw, BAR_H, 8);
    static const int sizes[4] = { 2, 4, 8, 16 };
    for (int i = 0; i < 4; i++) {
        int x = MARGIN + i * 36;
        int sel = (sizes[i] == brush_size);
        gui_fill_rect(win, x, y + 2, 32, BAR_H - 4, sel ? 9 : 7);
        gui_fill_rect(win, x + 16 - sizes[i] / 2,
                      y + (BAR_H - sizes[i]) / 2,
                      sizes[i], sizes[i], 0);
    }
    /* CLEAR button */
    int clr_x = cw - 56 - MARGIN - 60;
    gui_fill_rect(win, clr_x, y + 2, 56, BAR_H - 4, 12);
    gui_text(win, clr_x + 12, y + 6, "CLEAR", 15, 12);
    /* SAVE button */
    int sav_x = cw - 56 - MARGIN;
    gui_fill_rect(win, sav_x, y + 2, 56, BAR_H - 4, 2);
    gui_text(win, sav_x + 16, y + 6, "SAVE", 15, 2);
}

static void clear_canvas(void) {
    int cy = PAL_H + BAR_H + MARGIN * 2;
    int cx = MARGIN;
    gui_fill_rect(win, cx, cy, CANVAS_W, CANVAS_H, 15);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) canvas[i] = 15;
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_palette();
    paint_toolbar();
    clear_canvas();
}

/* ---- hit-test --------------------------------------------------- */

static int hit_palette(int rx, int ry, int* idx) {
    if (ry < MARGIN || ry >= PAL_H) return 0;
    int sw = (cw - MARGIN * 2) / 16;
    if (rx < MARGIN || rx >= MARGIN + sw * 16) return 0;
    *idx = (rx - MARGIN) / sw;
    return *idx >= 0 && *idx < 16;
}

#define TOOLBAR_CLEAR 1
#define TOOLBAR_SAVE  2

static int hit_toolbar(int rx, int ry, int* size, int* action) {
    int y = PAL_H + MARGIN;
    if (ry < y + 2 || ry >= y + BAR_H - 2) return 0;
    static const int sizes[4] = { 2, 4, 8, 16 };
    for (int i = 0; i < 4; i++) {
        int bx = MARGIN + i * 36;
        if (rx >= bx && rx < bx + 32) { *size = sizes[i]; return 1; }
    }
    int clr_x = cw - 56 - MARGIN - 60;
    if (rx >= clr_x && rx < clr_x + 56) { *action = TOOLBAR_CLEAR; return 1; }
    int sav_x = cw - 56 - MARGIN;
    if (rx >= sav_x && rx < sav_x + 56) { *action = TOOLBAR_SAVE;  return 1; }
    return 0;
}

static int canvas_origin_x(void) { return MARGIN; }
static int canvas_origin_y(void) { return PAL_H + BAR_H + MARGIN * 2; }

static int in_canvas(int rx, int ry, int* lx, int* ly) {
    int ox = canvas_origin_x(), oy = canvas_origin_y();
    if (rx < ox || rx >= ox + CANVAS_W) return 0;
    if (ry < oy || ry >= oy + CANVAS_H) return 0;
    *lx = rx - ox;
    *ly = ry - oy;
    return 1;
}

static void brush_at(int lx, int ly) {
    int half = brush_size / 2;
    int x0 = lx - half, y0 = ly - half;
    int x1 = x0 + brush_size, y1 = y0 + brush_size;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CANVAS_W) x1 = CANVAS_W;
    if (y1 > CANVAS_H) y1 = CANVAS_H;
    if (x1 <= x0 || y1 <= y0) return;
    /* Mirror to backing buffer. */
    for (int y = y0; y < y1; y++) {
        uint8_t* row = canvas + y * CANVAS_W;
        for (int x = x0; x < x1; x++) row[x] = (uint8_t)sel_color;
    }
    /* And to the window. */
    int ox = canvas_origin_x(), oy = canvas_origin_y();
    gui_fill_rect(win, ox + x0, oy + y0, x1 - x0, y1 - y0, (uint8_t)sel_color);
}

/* ---- BMP save --------------------------------------------------- */

static void put_le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Total BMP size: 14 (file hdr) + 40 (DIB hdr) + 256*4 (palette) +
 *                 CANVAS_W * CANVAS_H (rows are exactly 4-byte aligned
 *                 because 320 % 4 == 0). */
#define BMP_HDR_BYTES   (14 + 40 + 256 * 4)
#define BMP_PIXEL_BYTES (CANVAS_W * CANVAS_H)
#define BMP_TOTAL       (BMP_HDR_BYTES + BMP_PIXEL_BYTES)

static uint8_t bmp_buf[BMP_TOTAL];

static void encode_bmp(void) {
    uint8_t* p = bmp_buf;
    /* BITMAPFILEHEADER */
    p[0] = 'B'; p[1] = 'M';
    put_le32(p + 2,  BMP_TOTAL);
    put_le32(p + 6,  0);
    put_le32(p + 10, BMP_HDR_BYTES);
    /* BITMAPINFOHEADER */
    put_le32(p + 14, 40);
    put_le32(p + 18, CANVAS_W);
    put_le32(p + 22, CANVAS_H);              /* positive => bottom-up */
    put_le16(p + 26, 1);                     /* planes               */
    put_le16(p + 28, 8);                     /* bits per pixel       */
    put_le32(p + 30, 0);                     /* BI_RGB               */
    put_le32(p + 34, BMP_PIXEL_BYTES);
    put_le32(p + 38, 2835);                  /* x ppm (~72 dpi)      */
    put_le32(p + 42, 2835);                  /* y ppm                */
    put_le32(p + 46, 256);                   /* colors used          */
    put_le32(p + 50, 0);                     /* colors important     */
    /* Palette: BGRA per entry. fb.c's palette is 6-bit (0..63),
     * scale to 8-bit for BMP (`<< 2 | >> 4`). */
    uint8_t rgb[768];
    make_palette_rgb(rgb);
    uint8_t* pal = p + 54;
    for (int i = 0; i < 256; i++) {
        uint8_t r = rgb[i * 3 + 0];
        uint8_t g = rgb[i * 3 + 1];
        uint8_t b = rgb[i * 3 + 2];
        /* 6-bit -> 8-bit: shift left 2, OR with top 2 bits. */
        r = (uint8_t)((r << 2) | (r >> 4));
        g = (uint8_t)((g << 2) | (g >> 4));
        b = (uint8_t)((b << 2) | (b >> 4));
        pal[i * 4 + 0] = b;
        pal[i * 4 + 1] = g;
        pal[i * 4 + 2] = r;
        pal[i * 4 + 3] = 0;
    }
    /* Pixel data: bottom-up. */
    uint8_t* px = p + BMP_HDR_BYTES;
    for (int y = 0; y < CANVAS_H; y++) {
        const uint8_t* src = canvas + (CANVAS_H - 1 - y) * CANVAS_W;
        uint8_t* dst = px + y * CANVAS_W;
        for (int x = 0; x < CANVAS_W; x++) dst[x] = src[x];
    }
}

static void save_bmp(void) {
    char folder[16], name[SD_NAME_MAX];
    if (!save_dialog("PAINT.BMP", "DOCS",
                     folder, sizeof(folder),
                     name,   sizeof(name))) {
        /* user cancelled — repaint our own window so the dialog's
         * window-close hole gets the canvas back. */
        paint_full();
        for (int y = 0; y < CANVAS_H; y++)
            for (int x = 0; x < CANVAS_W; x++) {
                /* re-stream the backing canvas to the visible window */
                gui_fill_rect(win, MARGIN + x, PAL_H + BAR_H + MARGIN * 2 + y,
                              1, 1, canvas[y * CANVAS_W + x]);
            }
        return;
    }
    encode_bmp();
    int rc = bos_save_to_folder(folder, name, bmp_buf, BMP_TOTAL);
    paint_full();
    /* Re-blit our backing canvas pixels back into the window after
     * the dialog closed (which painted over everything). */
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            gui_fill_rect(win, MARGIN + x, PAL_H + BAR_H + MARGIN * 2 + y,
                          1, 1, canvas[y * CANVAS_W + x]);
    if (rc < 0) sound_error();
    else        sound_ok();
}

/* ---- main ------------------------------------------------------- */

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Paint", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINPAINT: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    paint_full();

    int painting = 0;
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(8); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == 'c' || a == 'C') clear_canvas();
            if (a == 's' || a == 'S') save_bmp();
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            int idx, sz = 0, act = 0;
            if (hit_palette(rx, ry, &idx)) {
                sel_color = idx;
                paint_palette();
            } else if (hit_toolbar(rx, ry, &sz, &act)) {
                if (act == TOOLBAR_CLEAR) { clear_canvas(); }
                else if (act == TOOLBAR_SAVE) { save_bmp(); }
                else if (sz) { brush_size = sz; paint_toolbar(); }
            } else {
                int lx, ly;
                if (in_canvas(rx, ry, &lx, &ly)) {
                    brush_at(lx, ly);
                    painting = 1;
                }
            }
        } else if (ev.type == GUI_EV_MOUSE_UP) {
            painting = 0;
        } else if (ev.type == GUI_EV_MOUSE_MOVE && painting) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            int lx, ly;
            if (in_canvas(rx, ry, &lx, &ly)) brush_at(lx, ly);
        }
    }
}
