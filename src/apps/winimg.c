/* WINIMG — image viewer.
 *
 * Supports:
 *   - 8-bit indexed BMP (the format Paint writes)
 *   - Baseline JPEG (decoded by the embedded jpeg.h library)
 *
 * Loads the file passed in `args` (or PAINT.BMP if no args), figures
 * out the format from the file's first bytes, decodes, dithers down
 * to BoxOS's 256-colour palette, and shows it in a window. */

#include "boxos_app.h"
#include "jpeg.h"   /* baseline JPEG decoder, header-only */

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define MAX_IMG_BYTES   (8 * 1024 * 1024)        /* 8 MiB cap for input */
static uint8_t in_buf[MAX_IMG_BYTES];

/* The boot palette in 8-bit-per-channel form (matches what fb.c
 * loads at boot). Used to do nearest-colour matching when displaying
 * RGB images on our 8-bit indexed screen. */
static uint8_t g_pal_rgb[256 * 3];

static void build_palette(void) {
    /* 16 standard VGA */
    static const uint8_t base16[16][3] = {
        {  0,   0,   0}, {  0,   0,  42}, {  0,  42,   0}, {  0,  42,  42},
        { 42,   0,   0}, { 42,   0,  42}, { 42,  21,   0}, { 42,  42,  42},
        { 21,  21,  21}, { 21,  21,  63}, { 21,  63,  21}, { 21,  63,  63},
        { 63,  21,  21}, { 63,  21,  63}, { 63,  63,  21}, { 63,  63,  63},
    };
    int o = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t r = base16[i][0], g = base16[i][1], b = base16[i][2];
        g_pal_rgb[o++] = (uint8_t)((r << 2) | (r >> 4));
        g_pal_rgb[o++] = (uint8_t)((g << 2) | (g >> 4));
        g_pal_rgb[o++] = (uint8_t)((b << 2) | (b >> 4));
    }
    /* 6x6x6 cube */
    for (int r = 0; r < 6; r++)
      for (int g = 0; g < 6; g++)
        for (int b = 0; b < 6; b++) {
            uint8_t R = (uint8_t)(r * 12), G = (uint8_t)(g * 12), B = (uint8_t)(b * 12);
            g_pal_rgb[o++] = (uint8_t)((R << 2) | (R >> 4));
            g_pal_rgb[o++] = (uint8_t)((G << 2) | (G >> 4));
            g_pal_rgb[o++] = (uint8_t)((B << 2) | (B >> 4));
        }
    /* grayscale ramp */
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(2 + i * 2);
        v = (uint8_t)((v << 2) | (v >> 4));
        g_pal_rgb[o++] = v; g_pal_rgb[o++] = v; g_pal_rgb[o++] = v;
    }
}

/* Nearest-colour match (squared-error) — slow O(256) per pixel but
 * fine for one-shot viewing. */
static uint8_t nearest_pal(int r, int g, int b) {
    int best_i = 0;
    int best_d = 0x7fffffff;
    const uint8_t* p = g_pal_rgb;
    for (int i = 0; i < 256; i++) {
        int dr = r - p[0];
        int dg = g - p[1];
        int db = b - p[2];
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best_i = i; }
        p += 3;
    }
    return (uint8_t)best_i;
}

/* ---- BMP loader (8-bit indexed only) -------------------------- */

static int show_bmp(const uint8_t* buf, int sz, const char* title) {
    if (sz < 54 || buf[0] != 'B' || buf[1] != 'M') return -1;
    uint32_t data_off = le32(buf + 10);
    int32_t  width    = (int32_t)le32(buf + 18);
    int32_t  height_s = (int32_t)le32(buf + 22);
    uint16_t bpp      = le16(buf + 28);
    uint32_t comp     = le32(buf + 30);
    if (bpp != 8 || comp != 0) return -1;
    int H = height_s < 0 ? -height_s : height_s;
    int top_down = height_s < 0;
    if (width <= 0 || H <= 0 || width > 640 || H > 480) return -1;

    int win_w = width + 8;
    int win_h = H + 16;
    int win_x = (640 - win_w) / 2;
    int win_y = (480 - win_h) / 2;
    if (win_y < 18) win_y = 18;

    int win = gui_open_window(title, win_x, win_y, win_w, win_h);
    if (win < 0) return -1;

    int row_stride = (width + 3) & ~3;
    for (int y = 0; y < H; y++) {
        int sy = top_down ? y : (H - 1 - y);
        const uint8_t* row = buf + data_off + sy * row_stride;
        int x = 0;
        while (x < width) {
            uint8_t v = row[x];
            int run = 1;
            while (x + run < width && row[x + run] == v) run++;
            gui_fill_rect(win, 4 + x, 4 + y, run, 1, v);
            x += run;
        }
    }
    /* Wait for Esc. */
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(20); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            if ((ev.arg1 & 0xFF) == 27) { gui_close_window(win); return 0; }
        }
    }
}

/* ---- JPEG path ------------------------------------------------- */

/* Show the decoded RGB inside a window, scaling down by an integer
 * factor if the image is bigger than the screen. */
static int show_rgb(const uint8_t* rgb, int W, int H, const char* title) {
    int max_w = 632, max_h = 440;
    int scale = 1;
    while ((W / scale) > max_w || (H / scale) > max_h) scale++;
    int dw = W / scale, dh = H / scale;
    int win_w = dw + 8, win_h = dh + 16;
    int win_x = (640 - win_w) / 2;
    int win_y = (480 - win_h) / 2;
    if (win_y < 18) win_y = 18;
    int win = gui_open_window(title, win_x, win_y, win_w, win_h);
    if (win < 0) return -1;
    /* Stream pixels into the window; collapse runs of identical
     * palette indices into single gui_fill_rect calls to keep the
     * syscall count down. */
    for (int y = 0; y < dh; y++) {
        int sy = y * scale;
        if (sy >= H) sy = H - 1;
        const uint8_t* row = rgb + sy * W * 3;
        int x = 0;
        while (x < dw) {
            int sx = x * scale;
            if (sx >= W) sx = W - 1;
            int r = row[sx * 3], g = row[sx * 3 + 1], b = row[sx * 3 + 2];
            uint8_t pi = nearest_pal(r, g, b);
            int run = 1;
            while (x + run < dw) {
                int sx2 = (x + run) * scale;
                if (sx2 >= W) sx2 = W - 1;
                int r2 = row[sx2 * 3], g2 = row[sx2 * 3 + 1], b2 = row[sx2 * 3 + 2];
                uint8_t pi2 = nearest_pal(r2, g2, b2);
                if (pi2 != pi) break;
                run++;
            }
            gui_fill_rect(win, 4 + x, 4 + y, run, 1, pi);
            x += run;
        }
    }
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(20); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            if ((ev.arg1 & 0xFF) == 27) { gui_close_window(win); return 0; }
        }
    }
}

static int show_error(const char* title, const char* msg) {
    int win_w = 360, win_h = 80;
    int win_x = (640 - win_w) / 2;
    int win_y = (480 - win_h) / 2;
    int win = gui_open_window(title, win_x, win_y, win_w, win_h);
    if (win < 0) return 1;
    gui_fill_rect(win, 8, 8, win_w - 16, 16, 12);
    gui_text(win, 16, 12, msg, 15, 12);
    gui_text(win, 16, 40, "Esc closes.", 0, 7);
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(20); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1) &&
            (ev.arg1 & 0xFF) == 27) {
            gui_close_window(win); return 0;
        }
    }
}

/* BOXANIM .ANI playback. */
static int show_anim(const uint8_t* buf, int sz, const char* title) {
    if (sz < 32) return -1;
    /* magic */
    static const char want[8] = { 'B','O','X','A','N','I','M','\0' };
    for (int i = 0; i < 8; i++) if (buf[i] != (uint8_t)want[i]) return -1;
    uint32_t W  = ((uint32_t)buf[8])  | ((uint32_t)buf[9]  << 8) |
                  ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
    uint32_t H  = ((uint32_t)buf[12]) | ((uint32_t)buf[13] << 8) |
                  ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
    uint32_t NF = ((uint32_t)buf[16]) | ((uint32_t)buf[17] << 8) |
                  ((uint32_t)buf[18] << 16) | ((uint32_t)buf[19] << 24);
    uint32_t FPS= ((uint32_t)buf[20]) | ((uint32_t)buf[21] << 8) |
                  ((uint32_t)buf[22] << 16) | ((uint32_t)buf[23] << 24);
    if (W == 0 || H == 0 || NF == 0 || NF > 12 || W > 640 || H > 480)
        return -1;
    /* freq table follows: max 12 entries * 4 bytes. */
    const uint8_t* freq_p = buf + 32;
    uint32_t freq[12];
    for (uint32_t i = 0; i < 12; i++) {
        freq[i] = ((uint32_t)freq_p[i*4]) | ((uint32_t)freq_p[i*4+1] << 8) |
                  ((uint32_t)freq_p[i*4+2] << 16) | ((uint32_t)freq_p[i*4+3] << 24);
    }
    const uint8_t* pixels = freq_p + 12 * 4;
    int frame_size = (int)(W * H);
    if ((int)(pixels - buf) + (int)NF * frame_size > sz) return -1;

    int win_w = (int)W + 8;
    int win_h = (int)H + 24;
    int win_x = (640 - win_w) / 2;
    int win_y = (480 - win_h) / 2;
    if (win_y < 18) win_y = 18;
    int win = gui_open_window(title, win_x, win_y, win_w, win_h);
    if (win < 0) return -1;
    gui_text(win, 4, 4 + (int)H + 4, "Esc closes  Space pause", 0, 7);

    int frame = 0;
    int paused = 0;
    uint64_t last = ticks_ms();
    uint64_t period = FPS > 0 ? (1000 / FPS) : 250;
    /* Initial paint of frame 0 + first tone. */
    for (uint32_t y = 0; y < H; y++) {
        const uint8_t* row = pixels + frame * frame_size + y * W;
        uint32_t x = 0;
        while (x < W) {
            uint8_t v = row[x];
            uint32_t run = 1;
            while (x + run < W && row[x + run] == v) run++;
            gui_fill_rect(win, 4 + (int)x, 4 + (int)y, (int)run, 1, v);
            x += run;
        }
    }
    if (freq[0]) sound_tone_on(freq[0]);
    for (;;) {
        struct gui_event ev;
        while (gui_poll_event(&ev)) {
            if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
                unsigned a = ev.arg1 & 0xFF;
                if (a == 27) {
                    sound_tone_off();
                    gui_close_window(win);
                    return 0;
                }
                if (a == ' ') {
                    paused = !paused;
                    if (paused) sound_tone_off();
                    else        last = ticks_ms();
                }
            }
        }
        if (!paused) {
            uint64_t now = ticks_ms();
            if (now - last >= period) {
                last = now;
                frame++;
                if (frame >= (int)NF) frame = 0;
                /* draw frame */
                for (uint32_t y = 0; y < H; y++) {
                    const uint8_t* row = pixels + frame * frame_size + y * W;
                    uint32_t x = 0;
                    while (x < W) {
                        uint8_t v = row[x];
                        uint32_t run = 1;
                        while (x + run < W && row[x + run] == v) run++;
                        gui_fill_rect(win, 4 + (int)x, 4 + (int)y, (int)run, 1, v);
                        x += run;
                    }
                }
                if (freq[frame]) sound_tone_on(freq[frame]);
                else             sound_tone_off();
            }
        }
        sleep_ms(8);
    }
}

int app_main(const char* args) {
    int from_pm = !(args && args[0]);
    const char* name = from_pm ? "PAINT.BMP" : args;
    int sz = (int)bos_read_file(name, in_buf, sizeof(in_buf));
    if (sz < 0) {
        /* No image to show. Most users hit this by launching from
         * Program Manager — they expect a window, not silence. Open
         * a small one explaining how to pick a file. */
        if (from_pm) {
            return show_error("Image View",
                              "No image open. Use File Mgr to "
                              "pick a .BMP/.JPG, then open it.");
        }
        return show_error(name, "Cannot read this file.");
    }

    char title[64]; int t = 0;
    while (name[t] && t < 60) { title[t] = name[t]; t++; }
    title[t] = 0;

    build_palette();

    /* Sniff format from the first bytes. */
    if (sz >= 8 &&
        in_buf[0] == 'B' && in_buf[1] == 'O' && in_buf[2] == 'X' &&
        in_buf[3] == 'A' && in_buf[4] == 'N' && in_buf[5] == 'I' &&
        in_buf[6] == 'M') {
        return show_anim(in_buf, sz, title);
    }
    if (sz >= 2 && in_buf[0] == 'B' && in_buf[1] == 'M') {
        if (show_bmp(in_buf, sz, title) >= 0) return 0;
        return show_error(title, "BMP: unsupported (must be 8-bit indexed).");
    }
    if (sz >= 3 && in_buf[0] == 0xFF && in_buf[1] == 0xD8 && in_buf[2] == 0xFF) {
        uint8_t* rgb = 0;
        int W = 0, H = 0;
        int rc = jpeg_decode(in_buf, sz, &rgb, &W, &H);
        if (rc < 0 || !rgb) {
            return show_error(title,
                              "JPEG: decode failed (only baseline is "
                              "supported).");
        }
        rc = show_rgb(rgb, W, H, title);
        xfree(rgb);
        return rc;
    }
    return show_error(title, "Unknown image format.");
}
