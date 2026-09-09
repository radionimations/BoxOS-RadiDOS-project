/* WINFLIP — frame-by-frame animation editor (FlipaClip-ish).
 *
 * 12 frames of 240x160 pixels, palette-indexed (256 colours from
 * the boot palette). Each frame can carry a single PC-speaker note
 * — silence or one of 8 piano keys (C4..C5). On save, all frames
 * plus the per-frame note table are written to /DOCS/ANIM.ANI in
 * the BOXANIM v2 format. WINIMG plays it back: pixels at the chosen
 * FPS, tones in sync via the PC speaker.
 *
 * No real MP3 — that needs a PCI sound card and a 5000-line
 * decoder. The single-voice square-wave we already have is what we
 * can synchronise to a frame stream.
 *
 * Layout (window 264 x 322):
 *   y=  0.. 21   colour palette (16 swatches, 22 px tall)
 *   y= 22.. 39   toolbar (brush sizes, CLEAR, PLAY, FPS, SAVE)
 *   y= 40.. 47   piano row (1=silence, 2..9=C4..C5)
 *   y= 48..207   canvas 240x160
 *   y=208..223   frame strip (12 thumbnails 20x14, current hilit)
 *   y=224..240   status (frame N/M, fps, current note, file)
 */

#include "boxos_app.h"
#include "save_dialog.h"

#define CANVAS_W   240
#define CANVAS_H   160
#define MAX_FRAMES  12

#define WIN_W      480       /* Wider than canvas — gives the toolbar
                              * room for brush sizes + FPS + SAVE so
                              * nothing collides with PLAY. */
#define PAL_H      22
#define TOOL_H     18
#define PIANO_H    16
#define STRIP_H    16
#define STATUS_H   16
#define WIN_H      (PAL_H + TOOL_H + PIANO_H + CANVAS_H + STRIP_H + STATUS_H + 12)

static int win;
static int cw, ch;
static int win_x = 80, win_y = 60;

/* ---- per-frame state ------------------------------------------ */

static uint8_t  frames[MAX_FRAMES][CANVAS_H * CANVAS_W];
static uint32_t frame_freq[MAX_FRAMES];
static int      n_frames = 1;
static int      cur_frame = 0;

/* ---- editor state --------------------------------------------- */

static int sel_color = 4;
static int brush_size = 4;
static int playing = 0;
static int fps = 8;

/* C4 .. C5 in Hz, plus silence at slot 0. */
static const uint32_t notes_hz[9] = {
    0, 262, 294, 330, 349, 392, 440, 494, 523,
};
static const char* note_label[9] = {
    "-", "C", "D", "E", "F", "G", "A", "B", "C+",
};

/* ---- helpers -------------------------------------------------- */

static int strlen_l(const char* s) { int n=0; while(s&&s[n])n++; return n; }
static void int_to_str(int v, char* out) {
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    char tmp[12]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
    int o = 0;
    if (neg) out[o++] = '-';
    while (t) out[o++] = tmp[--t];
    out[o] = 0;
}

/* ---- layout helpers ------------------------------------------- */

static int palette_y(void)  { return 0; }
static int toolbar_y(void)  { return PAL_H + 2; }
static int piano_y(void)    { return toolbar_y() + TOOL_H + 2; }
static int canvas_y(void)   { return piano_y() + PIANO_H + 2; }
static int canvas_x(void)   { return (cw - CANVAS_W) / 2; }
static int strip_y(void)    { return canvas_y() + CANVAS_H + 2; }
static int status_y(void)   { return strip_y() + STRIP_H + 2; }

/* ---- painting ------------------------------------------------- */

static void paint_palette(void) {
    int sw = (cw - 16) / 16;
    int y = palette_y() + 2;
    int off = 8;
    for (int i = 0; i < 16; i++) {
        int x = off + i * sw;
        gui_fill_rect(win, x, y, sw - 2, PAL_H - 6, (uint8_t)i);
        if (i == sel_color) {
            int b = 2;
            gui_fill_rect(win, x, y, sw - 2, b, 15);
            gui_fill_rect(win, x, y + PAL_H - 6 - b, sw - 2, b, 15);
            gui_fill_rect(win, x, y, b, PAL_H - 6, 15);
            gui_fill_rect(win, x + sw - 2 - b, y, b, PAL_H - 6, 15);
        }
    }
}

#define BTN_PREV   1
#define BTN_NEXT   2
#define BTN_ADD    3
#define BTN_DEL    4
#define BTN_CLEAR  5
#define BTN_PLAY   6
#define BTN_FPS    7
#define BTN_SAVE   8

static struct { int x, w; const char* label; int id; uint8_t color; } toolbar_btns[] = {
    {   8, 32, "<",     BTN_PREV,  11 },
    {  44, 32, ">",     BTN_NEXT,  11 },
    {  80, 32, "+",     BTN_ADD,    2 },
    { 116, 32, "-",     BTN_DEL,   12 },
    { 152, 40, "CLR",   BTN_CLEAR,  8 },
    { 196, 40, "PLAY",  BTN_PLAY,   2 },
    { 240, 16, ".",     0,          7 },     /* size 2 */
    { 256, 16, ".",     0,          7 },     /* size 4 (sel by default) */
    /* SAVE button is last — placed dynamically right-aligned. */
};
#define N_TOOL_BTNS ((int)(sizeof(toolbar_btns) / sizeof(toolbar_btns[0])))

static void paint_toolbar(void) {
    int y = toolbar_y();
    gui_fill_rect(win, 0, y, cw, TOOL_H, 8);
    /* prev/next/add/del/clr/play */
    for (int i = 0; i < 6; i++) {
        int x = toolbar_btns[i].x;
        int w = toolbar_btns[i].w;
        uint8_t c = toolbar_btns[i].color;
        if (toolbar_btns[i].id == BTN_PLAY && playing) c = 12;
        gui_fill_rect(win, x, y + 2, w, TOOL_H - 4, c);
        const char* L = toolbar_btns[i].id == BTN_PLAY && playing ? "STOP" : toolbar_btns[i].label;
        int lw = strlen_l(L) * 8;
        gui_text(win, x + (w - lw) / 2, y + 5, L, 0, c);
    }
    /* brush size buttons (4 sizes) */
    static const int sizes[4] = { 2, 4, 8, 16 };
    int bx0 = 240;
    for (int i = 0; i < 4; i++) {
        int sel = (sizes[i] == brush_size);
        gui_fill_rect(win, bx0 + i * 18, y + 2, 16, TOOL_H - 4, sel ? 9 : 7);
        gui_fill_rect(win, bx0 + i * 18 + 8 - sizes[i] / 2,
                      y + (TOOL_H - sizes[i]) / 2,
                      sizes[i], sizes[i], 0);
    }
    /* FPS picker — placed AFTER the brush buttons (x = 240..310) so
     * it doesn't sit on top of PLAY. */
    int fps_x = bx0 + 4 * 18 + 8;       /* 240 + 72 + 8 = 320 */
    gui_text(win, fps_x, y + 5, "FPS", 0, 8);
    char fps_s[8]; int_to_str(fps, fps_s);
    gui_fill_rect(win, fps_x + 28, y + 2, 24, TOOL_H - 4, 14);
    gui_text(win, fps_x + 32, y + 5, fps_s, 0, 14);

    /* SAVE on right edge */
    int sav_x = cw - 56;
    gui_fill_rect(win, sav_x, y + 2, 48, TOOL_H - 4, 2);
    gui_text(win, sav_x + 12, y + 5, "SAVE", 15, 2);
}

static void paint_piano(void) {
    int y = piano_y();
    gui_fill_rect(win, 0, y, cw, PIANO_H, 7);
    int bw = (cw - 16) / 9;
    int sx = 8;
    int active_note = 0;
    for (int i = 0; i < 9; i++) {
        if (notes_hz[i] == frame_freq[cur_frame]) { active_note = i; break; }
    }
    for (int i = 0; i < 9; i++) {
        int x = sx + i * bw;
        uint8_t bg = (i == active_note) ? 9 : (i == 0 ? 8 : 15);
        uint8_t fg = (i == active_note) ? 15 : 0;
        gui_fill_rect(win, x, y, bw - 2, PIANO_H, bg);
        int lw = strlen_l(note_label[i]) * 8;
        gui_text(win, x + (bw - 2 - lw) / 2, y + 4, note_label[i], fg, bg);
    }
}

static void paint_canvas(void) {
    int x = canvas_x(), y = canvas_y();
    /* Slow-but-simple: stream the frame buffer through fill_rect
     * runs. We collapse runs of identical palette indices. */
    for (int py = 0; py < CANVAS_H; py++) {
        const uint8_t* row = frames[cur_frame] + py * CANVAS_W;
        int px = 0;
        while (px < CANVAS_W) {
            uint8_t v = row[px];
            int run = 1;
            while (px + run < CANVAS_W && row[px + run] == v) run++;
            gui_fill_rect(win, x + px, y + py, run, 1, v);
            px += run;
        }
    }
    /* 1-px frame border */
    gui_fill_rect(win, x - 1, y - 1, CANVAS_W + 2, 1, 0);
    gui_fill_rect(win, x - 1, y + CANVAS_H, CANVAS_W + 2, 1, 0);
    gui_fill_rect(win, x - 1, y - 1, 1, CANVAS_H + 2, 0);
    gui_fill_rect(win, x + CANVAS_W, y - 1, 1, CANVAS_H + 2, 0);
}

static void paint_strip(void) {
    int y = strip_y();
    gui_fill_rect(win, 0, y, cw, STRIP_H, 8);
    int sx = (cw - MAX_FRAMES * 20) / 2;
    for (int i = 0; i < MAX_FRAMES; i++) {
        int x = sx + i * 20;
        int active = (i < n_frames);
        int sel = (i == cur_frame);
        gui_fill_rect(win, x, y + 1, 18, STRIP_H - 2, sel ? 14 : (active ? 7 : 8));
        if (active) {
            /* tiny preview: average central pixel */
            uint8_t v = frames[i][CANVAS_H/2 * CANVAS_W + CANVAS_W/2];
            gui_fill_rect(win, x + 2, y + 3, 14, STRIP_H - 6, v);
            char s[3];
            s[0] = '0' + ((i + 1) / 10);
            s[1] = '0' + ((i + 1) % 10);
            s[2] = 0;
            gui_text(win, x + (s[0] == '0' ? 5 : 1), y + 4, s + (s[0] == '0' ? 1 : 0),
                     sel ? 0 : 15, sel ? 14 : (active ? 7 : 8));
        }
    }
}

static void paint_status(void) {
    int y = status_y();
    gui_fill_rect(win, 0, y, cw, STATUS_H, 8);
    /* "FRAME 3/12   FPS 8   NOTE C+   ANIM.ANI" */
    char line[80]; int p = 0;
    const char* f1 = "FRAME ";
    int i = 0; while (f1[i]) line[p++] = f1[i++];
    char num[8];
    int_to_str(cur_frame + 1, num); i = 0; while (num[i]) line[p++] = num[i++];
    line[p++] = '/';
    int_to_str(n_frames, num); i = 0; while (num[i]) line[p++] = num[i++];
    const char* f2 = "  FPS ";
    i = 0; while (f2[i]) line[p++] = f2[i++];
    int_to_str(fps, num); i = 0; while (num[i]) line[p++] = num[i++];
    const char* f3 = "  NOTE ";
    i = 0; while (f3[i]) line[p++] = f3[i++];
    int active_note = 0;
    for (int j = 0; j < 9; j++)
        if (notes_hz[j] == frame_freq[cur_frame]) { active_note = j; break; }
    const char* nl = note_label[active_note];
    i = 0; while (nl[i]) line[p++] = nl[i++];
    const char* f4 = "  -> /DOCS/ANIM.ANI";
    i = 0; while (f4[i]) line[p++] = f4[i++];
    line[p] = 0;
    gui_text(win, 8, y + 4, line, 15, 8);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_palette();
    paint_toolbar();
    paint_piano();
    paint_canvas();
    paint_strip();
    paint_status();
}

/* ---- hit-testing ---------------------------------------------- */

static int hit_palette(int rx, int ry, int* idx) {
    int y = palette_y() + 2;
    if (ry < y || ry >= y + PAL_H - 6) return 0;
    int sw = (cw - 16) / 16;
    int off = 8;
    if (rx < off || rx >= off + sw * 16) return 0;
    *idx = (rx - off) / sw;
    return *idx >= 0 && *idx < 16;
}

static int hit_toolbar(int rx, int ry, int* btn, int* size) {
    int y = toolbar_y();
    if (ry < y + 2 || ry >= y + TOOL_H - 2) return 0;
    /* dedicated buttons */
    for (int i = 0; i < 6; i++) {
        int bx = toolbar_btns[i].x;
        int bw = toolbar_btns[i].w;
        if (rx >= bx && rx < bx + bw) { *btn = toolbar_btns[i].id; return 1; }
    }
    /* brush sizes */
    static const int sizes[4] = { 2, 4, 8, 16 };
    int bx0 = 240;
    for (int i = 0; i < 4; i++) {
        if (rx >= bx0 + i * 18 && rx < bx0 + i * 18 + 16) {
            *size = sizes[i]; return 1;
        }
    }
    /* FPS box — matches paint_toolbar geometry (placed after brushes). */
    int fps_x = bx0 + 4 * 18 + 8;
    if (rx >= fps_x + 28 && rx < fps_x + 52) { *btn = BTN_FPS; return 1; }
    /* SAVE */
    int sav_x = cw - 56;
    if (rx >= sav_x && rx < sav_x + 48) { *btn = BTN_SAVE; return 1; }
    return 0;
}

static int hit_piano(int rx, int ry, int* idx) {
    int y = piano_y();
    if (ry < y || ry >= y + PIANO_H) return 0;
    int bw = (cw - 16) / 9;
    int sx = 8;
    if (rx < sx || rx >= sx + bw * 9) return 0;
    *idx = (rx - sx) / bw;
    return *idx >= 0 && *idx < 9;
}

static int hit_strip(int rx, int ry, int* fi) {
    int y = strip_y();
    if (ry < y || ry >= y + STRIP_H) return 0;
    int sx = (cw - MAX_FRAMES * 20) / 2;
    if (rx < sx || rx >= sx + MAX_FRAMES * 20) return 0;
    *fi = (rx - sx) / 20;
    return *fi >= 0 && *fi < MAX_FRAMES;
}

static int in_canvas(int rx, int ry, int* lx, int* ly) {
    int ox = canvas_x(), oy = canvas_y();
    if (rx < ox || rx >= ox + CANVAS_W) return 0;
    if (ry < oy || ry >= oy + CANVAS_H) return 0;
    *lx = rx - ox; *ly = ry - oy;
    return 1;
}

/* ---- drawing -------------------------------------------------- */

static void brush_at(int lx, int ly) {
    int half = brush_size / 2;
    int x0 = lx - half, y0 = ly - half;
    int x1 = x0 + brush_size, y1 = y0 + brush_size;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CANVAS_W) x1 = CANVAS_W;
    if (y1 > CANVAS_H) y1 = CANVAS_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            frames[cur_frame][y * CANVAS_W + x] = (uint8_t)sel_color;
    int ox = canvas_x(), oy = canvas_y();
    gui_fill_rect(win, ox + x0, oy + y0, x1 - x0, y1 - y0, (uint8_t)sel_color);
}

static void clear_frame(void) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) frames[cur_frame][i] = 15;
    paint_canvas();
    paint_strip();
}

/* ---- frame ops ------------------------------------------------ */

static void copy_frame(int src, int dst) {
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++)
        frames[dst][i] = frames[src][i];
    frame_freq[dst] = frame_freq[src];
}

static void add_frame(void) {
    if (n_frames >= MAX_FRAMES) { sound_error(); return; }
    /* Insert AFTER the current frame, copying its content as a
     * starting point ("hold" frame). */
    for (int i = n_frames; i > cur_frame + 1; i--) {
        copy_frame(i - 1, i);
    }
    n_frames++;
    cur_frame++;
    /* New frame keeps a copy of the previous; user paints differences. */
    paint_full();
}

static void delete_frame(void) {
    if (n_frames <= 1) { sound_error(); return; }
    for (int i = cur_frame; i < n_frames - 1; i++) copy_frame(i + 1, i);
    n_frames--;
    if (cur_frame >= n_frames) cur_frame = n_frames - 1;
    paint_full();
}

/* ---- save ---------------------------------------------------- */

#define ANI_HDR_SIZE   32
#define ANI_FREQ_SIZE  (MAX_FRAMES * 4)
#define ANI_TOTAL      (ANI_HDR_SIZE + MAX_FRAMES * 4 + MAX_FRAMES * CANVAS_W * CANVAS_H)

static uint8_t ani_buf[ANI_TOTAL];

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void save_anim(void) {
    /* Header */
    const char magic[8] = { 'B','O','X','A','N','I','M','\0' };
    for (int i = 0; i < 8; i++) ani_buf[i] = magic[i];
    put32(ani_buf + 8,  CANVAS_W);
    put32(ani_buf + 12, CANVAS_H);
    put32(ani_buf + 16, (uint32_t)n_frames);
    put32(ani_buf + 20, (uint32_t)fps);
    put32(ani_buf + 24, 1);                /* version: 1 = with audio */
    put32(ani_buf + 28, 0);                /* reserved                */
    /* Frequency table — n_frames entries, then padding to MAX. */
    for (int i = 0; i < MAX_FRAMES; i++) {
        put32(ani_buf + ANI_HDR_SIZE + i * 4,
              i < n_frames ? frame_freq[i] : 0);
    }
    /* Pixel data — n_frames × W × H, then zero padding. */
    int pixel_off = ANI_HDR_SIZE + MAX_FRAMES * 4;
    for (int i = 0; i < n_frames; i++) {
        for (int j = 0; j < CANVAS_W * CANVAS_H; j++) {
            ani_buf[pixel_off + i * CANVAS_W * CANVAS_H + j] = frames[i][j];
        }
    }
    int total = pixel_off + n_frames * CANVAS_W * CANVAS_H;

    char folder[16], name[SD_NAME_MAX];
    if (!save_dialog("ANIM.ANI", "DOCS",
                     folder, sizeof(folder),
                     name,   sizeof(name))) {
        paint_full();
        return;
    }
    int rc = bos_save_to_folder(folder, name, ani_buf, total);
    paint_full();
    if (rc < 0) sound_error();
    else        sound_ok();
}

/* ---- main ---------------------------------------------------- */

static void switch_frame(int new_idx) {
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= n_frames) new_idx = n_frames - 1;
    if (new_idx == cur_frame) return;
    cur_frame = new_idx;
    paint_canvas();
    paint_piano();
    paint_strip();
    paint_status();
}

static void cycle_fps(void) {
    static const int fps_seq[4] = { 4, 8, 12, 24 };
    for (int i = 0; i < 4; i++) {
        if (fps_seq[i] == fps) {
            fps = fps_seq[(i + 1) % 4];
            return;
        }
    }
    fps = 8;
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Animation", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINFLIP: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    /* Init: 1 blank white frame, silent. */
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) frames[0][i] = 15;
    for (int i = 0; i < MAX_FRAMES; i++) frame_freq[i] = 0;
    paint_full();

    int painting = 0;
    uint64_t last_play_tick = 0;

    for (;;) {
        /* Playback advance */
        if (playing) {
            uint64_t now = ticks_ms();
            uint64_t period = 1000 / (uint64_t)(fps > 0 ? fps : 1);
            if (now - last_play_tick >= period) {
                last_play_tick = now;
                /* Play current frame's note for this frame's duration. */
                if (frame_freq[cur_frame]) sound_tone_on(frame_freq[cur_frame]);
                else                       sound_tone_off();
                int next = cur_frame + 1;
                if (next >= n_frames) next = 0;
                cur_frame = next;
                paint_canvas();
                paint_strip();
                paint_status();
            }
        }

        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(8); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { sound_tone_off(); gui_close_window(win); return 0; }
            if (a == ' ') {
                playing = !playing;
                if (!playing) sound_tone_off();
                last_play_tick = ticks_ms();
                paint_toolbar();
            }
            if (a == ',') switch_frame(cur_frame - 1);
            if (a == '.') switch_frame(cur_frame + 1);
            if (a == 's' || a == 'S') save_anim();
            if (a == 'c' || a == 'C') clear_frame();
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            int idx, btn = 0, sz = 0, fi = 0;
            if (hit_palette(rx, ry, &idx))   { sel_color = idx; paint_palette(); }
            else if (hit_piano(rx, ry, &idx)){ frame_freq[cur_frame] = notes_hz[idx];
                                               paint_piano(); paint_status();
                                               if (notes_hz[idx]) sound_beep(notes_hz[idx], 80); }
            else if (hit_strip(rx, ry, &fi)) {
                if (fi < n_frames) switch_frame(fi);
            }
            else if (hit_toolbar(rx, ry, &btn, &sz)) {
                if (sz)               { brush_size = sz; paint_toolbar(); }
                else if (btn == BTN_PREV) switch_frame(cur_frame - 1);
                else if (btn == BTN_NEXT) switch_frame(cur_frame + 1);
                else if (btn == BTN_ADD)  add_frame();
                else if (btn == BTN_DEL)  delete_frame();
                else if (btn == BTN_CLEAR)clear_frame();
                else if (btn == BTN_PLAY) {
                    playing = !playing;
                    if (!playing) sound_tone_off();
                    last_play_tick = ticks_ms();
                    paint_toolbar();
                }
                else if (btn == BTN_FPS)  { cycle_fps(); paint_toolbar(); paint_status(); }
                else if (btn == BTN_SAVE) save_anim();
            }
            else {
                int lx, ly;
                if (in_canvas(rx, ry, &lx, &ly)) { brush_at(lx, ly); painting = 1; }
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
