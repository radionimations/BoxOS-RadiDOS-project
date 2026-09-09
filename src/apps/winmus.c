/* WINMUS — step-sequencer music builder for the PC speaker.
 *
 * 16 steps × 9 pitch rows (silence + C4..C5) per page, up to
 * MAX_PAGES pages chained back-to-back. Click a cell to set/clear
 * a note; click-and-drag horizontally along a row to paint a
 * sustained run of the same note. PLAY loops through every page
 * at the chosen BPM (add more pages → effectively infinite music).
 * SAVE writes the CURRENT page to a .MUS file.
 *
 * .MUS file format (one page):
 *   "BOXMUS\0\0"   8 bytes magic
 *   uint32 n_steps  (always 16)
 *   uint32 bpm
 *   uint32 reserved (0)
 *   uint8  pattern[n_steps]    0=silence, 1..8 = C4..C5
 */

#include "boxos_app.h"
#include "save_dialog.h"

#define STEPS     16
#define ROWS       9          /* silence + 8 notes */
#define MAX_PAGES 16

#define CELL_W 22
#define CELL_H 22

#define WIN_W  (CELL_W * STEPS + 80)
#define WIN_H  (CELL_H * ROWS + 110)        /* +30 vs old for page row */

static int win;
static int cw, ch;
static int win_x = 80, win_y = 60;

static const uint32_t notes_hz[ROWS] = {
    0,        /* row 0 = silence */
    523, 494, 440, 392, 349, 330, 294, 262,
};
static const char* note_label[ROWS] = {
    "-",  "C+", "B", "A", "G", "F", "E", "D", "C",
};

/* Pages: each page is one 16-step bar; PLAY chains them. */
static uint8_t patterns[MAX_PAGES][STEPS];
static int n_pages   = 1;
static int view_page = 0;
static int play_page = 0;

static int bpm = 120;
static int playing  = 0;
static int cur_step = 0;

/* Drag-paint state: while the left button is held over the grid,
 * sweep along a row to lay down a sustained note (or wipe one
 * away if the first cell already matched). */
static int drag_active    = 0;
static int drag_row       = 0;    /* 0 = erasing, else the row to paint */
static int last_drag_step = -1;

#define GRID_X    36
#define GRID_Y    32

static int grid_top_y(void)  { return GRID_Y; }
static int grid_left_x(void) { return GRID_X; }

static int strlen_l(const char* s) { int n=0; while(s&&s[n])n++; return n; }
static void int_to_str(int v, char* out) {
    int neg = 0; if (v < 0) { neg = 1; v = -v; }
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    int o = 0; if (neg) out[o++] = '-';
    while (n) { out[o++] = t[--n]; }
    out[o] = 0;
}

static int step_period_ms(void) {
    if (bpm < 30)  bpm = 30;
    if (bpm > 300) bpm = 300;
    return 60000 / (bpm * 4);
}

/* ---- painting -------------------------------------------------- */

static void paint_grid_cell(int step, int row) {
    int x = grid_left_x() + step * CELL_W;
    int y = grid_top_y()  + row  * CELL_H;
    uint8_t cell = patterns[view_page][step];
    int active = (cell == row && row != 0);
    uint8_t bg = active ? (uint8_t)(1 + (row & 7)) : 7;
    /* Playhead highlight — only on the page that's actually playing. */
    int playhead = (playing && step == cur_step && view_page == play_page);
    if (playhead) bg = (active ? bg : 14);
    gui_fill_rect(win, x + 1, y + 1, CELL_W - 2, CELL_H - 2, bg);
    if (active)
        gui_fill_rect(win, x + CELL_W / 2 - 4, y + CELL_H / 2 - 4, 8, 8, 0);
    gui_fill_rect(win, x, y, CELL_W, 1, 8);
    gui_fill_rect(win, x, y + CELL_H - 1, CELL_W, 1, 8);
}

static void paint_grid(void) {
    for (int r = 0; r < ROWS; r++) {
        int y = grid_top_y() + r * CELL_H;
        gui_fill_rect(win, 4, y, GRID_X - 8, CELL_H, 8);
        gui_text(win, 8, y + 7, note_label[r], 15, 8);
    }
    for (int s = 0; s < STEPS; s++)
        for (int r = 0; r < ROWS; r++)
            paint_grid_cell(s, r);
    for (int s = 0; s < STEPS; s++) {
        int x = grid_left_x() + s * CELL_W;
        char num[4]; int_to_str(s + 1, num);
        gui_fill_rect(win, x, GRID_Y - 12, CELL_W, 11, 8);
        gui_text(win, x + (CELL_W - strlen_l(num) * 8) / 2, GRID_Y - 10,
                 num, 15, 8);
    }
}

#define BTN_PLAY  1
#define BTN_STOP  2
#define BTN_CLR   3
#define BTN_SAVE  4
#define BTN_BPMD  5
#define BTN_BPMU  6
#define BTN_PREVP 7
#define BTN_NEXTP 8
#define BTN_ADDP  9
#define BTN_DELP  10

static const struct {
    int x, w, row;
    const char* label;
    int id;
    uint8_t color;
} buttons[] = {
    /* Row 0: transport / save / BPM. */
    {  4, 56, 0, "PLAY",   BTN_PLAY,  2  },
    { 64, 56, 0, "STOP",   BTN_STOP,  12 },
    {124, 56, 0, "CLEAR",  BTN_CLR,   8  },
    {184, 56, 0, "SAVE",   BTN_SAVE,  14 },
    {280, 24, 0, "-",      BTN_BPMD,  11 },
    {340, 24, 0, "+",      BTN_BPMU,  11 },
    /* Row 1: page navigation. */
    {  4, 24, 1, "<",      BTN_PREVP, 11 },
    {116, 24, 1, ">",      BTN_NEXTP, 11 },
    {148, 56, 1, "+PAGE",  BTN_ADDP,  10 },
    {212, 56, 1, "-PAGE",  BTN_DELP,  12 },
};
#define N_BUTTONS ((int)(sizeof(buttons) / sizeof(buttons[0])))

static int row0_y(void) { return GRID_Y + ROWS * CELL_H + 10; }
static int row1_y(void) { return row0_y() + 32; }

static void paint_buttons(void) {
    int y0 = row0_y(), y1 = row1_y();
    gui_fill_rect(win, 0, y0, cw, 64, 7);
    for (int i = 0; i < N_BUTTONS; i++) {
        int by = (buttons[i].row == 0) ? y0 : y1;
        int bx = buttons[i].x;
        int bw = buttons[i].w;
        uint8_t c = buttons[i].color;
        if (buttons[i].id == BTN_PLAY && playing) c = 14;
        gui_fill_rect(win, bx, by + 4, bw, 22, c);
        int lw = strlen_l(buttons[i].label) * 8;
        gui_text(win, bx + (bw - lw) / 2, by + 11, buttons[i].label, 0, c);
    }
    /* BPM display nestled between - and +. */
    char bpm_s[8]; int_to_str(bpm, bpm_s);
    gui_text(win, 308, y0 + 7, "BPM", 0, 7);
    gui_fill_rect(win, 304, y0 + 4, 36, 22, 15);
    int lw = strlen_l(bpm_s) * 8;
    gui_text(win, 304 + (36 - lw) / 2, y0 + 11, bpm_s, 0, 15);
    /* Page indicator between < and >. */
    char buf[24]; int b = 0;
    const char* p = "PAGE ";
    while (*p) buf[b++] = *p++;
    char num[4]; int_to_str(view_page + 1, num);
    int j = 0; while (num[j]) buf[b++] = num[j++];
    buf[b++] = '/';
    int_to_str(n_pages, num);
    j = 0; while (num[j]) buf[b++] = num[j++];
    buf[b] = 0;
    gui_fill_rect(win, 32, y1 + 4, 80, 22, 15);
    gui_text(win, 32 + (80 - strlen_l(buf) * 8) / 2, y1 + 11, buf, 0, 15);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_grid();
    paint_buttons();
}

/* ---- hit-testing ---------------------------------------------- */

static int hit_grid(int rx, int ry, int* step, int* row) {
    if (rx < grid_left_x() || rx >= grid_left_x() + STEPS * CELL_W) return 0;
    if (ry < grid_top_y()  || ry >= grid_top_y()  + ROWS  * CELL_H) return 0;
    *step = (rx - grid_left_x()) / CELL_W;
    *row  = (ry - grid_top_y())  / CELL_H;
    return 1;
}

/* For drag-paint we only care about the column. */
static int hit_grid_col(int rx, int ry, int* step) {
    (void)ry;
    if (rx < grid_left_x() || rx >= grid_left_x() + STEPS * CELL_W) return 0;
    *step = (rx - grid_left_x()) / CELL_W;
    return 1;
}

static int hit_button(int rx, int ry, int* btn) {
    int y0 = row0_y(), y1 = row1_y();
    for (int i = 0; i < N_BUTTONS; i++) {
        int by = (buttons[i].row == 0) ? y0 : y1;
        if (ry < by + 4 || ry >= by + 26) continue;
        int bx = buttons[i].x;
        int bw = buttons[i].w;
        if (rx >= bx && rx < bx + bw) { *btn = buttons[i].id; return 1; }
    }
    return 0;
}

/* ---- drag-paint helpers --------------------------------------- */

static void paint_step_column(int step) {
    for (int r = 0; r < ROWS; r++) paint_grid_cell(step, r);
}

static void apply_drag(int step) {
    if (step < 0 || step >= STEPS) return;
    patterns[view_page][step] = (uint8_t)drag_row;
    paint_step_column(step);
}

static void drag_to_step(int step) {
    if (step < 0 || step >= STEPS) return;
    if (last_drag_step < 0) {
        apply_drag(step);
        last_drag_step = step;
        return;
    }
    if (step == last_drag_step) return;
    int from = last_drag_step, to = step;
    if (from < to) for (int s = from + 1; s <= to; s++) apply_drag(s);
    else           for (int s = from - 1; s >= to; s--) apply_drag(s);
    last_drag_step = step;
}

/* ---- save ----------------------------------------------------- */

#define MUS_HDR 20
static uint8_t mus_buf[MUS_HDR + STEPS];

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void save_pattern(void) {
    static const char magic[8] = { 'B','O','X','M','U','S','\0','\0' };
    for (int i = 0; i < 8; i++) mus_buf[i] = (uint8_t)magic[i];
    put32(mus_buf + 8,  STEPS);
    put32(mus_buf + 12, (uint32_t)bpm);
    put32(mus_buf + 16, 0);
    for (int i = 0; i < STEPS; i++) mus_buf[MUS_HDR + i] = patterns[view_page][i];

    char folder[16], name[SD_NAME_MAX];
    if (!save_dialog("MUSIC.MUS", "DOCS",
                     folder, sizeof(folder),
                     name,   sizeof(name))) {
        paint_full();
        return;
    }
    int rc = bos_save_to_folder(folder, name, mus_buf, MUS_HDR + STEPS);
    paint_full();
    if (rc < 0) sound_error();
    else        sound_ok();
}

/* ---- page operations ------------------------------------------ */

static void clear_page(int p) {
    if (p < 0 || p >= n_pages) return;
    for (int i = 0; i < STEPS; i++) patterns[p][i] = 0;
}

static void add_page(void) {
    if (n_pages >= MAX_PAGES) { sound_error(); return; }
    clear_page(n_pages);
    n_pages++;
    view_page = n_pages - 1;
    paint_full();
}

static void del_page(void) {
    if (n_pages <= 1) { sound_error(); return; }
    /* Shift later pages down over the deleted one. */
    for (int p = view_page; p < n_pages - 1; p++)
        for (int i = 0; i < STEPS; i++)
            patterns[p][i] = patterns[p + 1][i];
    n_pages--;
    if (view_page >= n_pages) view_page = n_pages - 1;
    if (play_page >= n_pages) play_page = n_pages - 1;
    paint_full();
}

static void goto_page(int p) {
    if (p < 0) p = n_pages - 1;
    if (p >= n_pages) p = 0;
    view_page = p;
    paint_full();
}

/* ---- main ---------------------------------------------------- */

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Music", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINMUS: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    for (int p = 0; p < MAX_PAGES; p++)
        for (int i = 0; i < STEPS; i++) patterns[p][i] = 0;
    paint_full();

    uint64_t last_step_ms = 0;

    for (;;) {
        if (playing) {
            uint64_t now = ticks_ms();
            if (now - last_step_ms >= (uint64_t)step_period_ms()) {
                last_step_ms = now;
                int row = patterns[play_page][cur_step];
                if (row && notes_hz[row]) sound_tone_on(notes_hz[row]);
                else                      sound_tone_off();
                /* Repaint old + new playhead columns, but only on
                 * the page being viewed. */
                int prev_step = (cur_step + STEPS - 1) % STEPS;
                int prev_page = play_page;
                /* Advance. */
                cur_step++;
                if (cur_step >= STEPS) {
                    cur_step = 0;
                    play_page++;
                    if (play_page >= n_pages) play_page = 0;
                }
                if (view_page == prev_page) paint_step_column(prev_step);
                if (view_page == play_page) paint_step_column(cur_step);
                /* If page changed under the viewer, redraw the grid. */
                if (prev_page != play_page && view_page == play_page) {
                    paint_grid();
                }
            }
        }

        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(10); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) {
                sound_tone_off();
                gui_close_window(win); return 0;
            }
            if (a == ' ') {
                playing = !playing;
                if (!playing) sound_tone_off();
                else { last_step_ms = ticks_ms(); cur_step = 0; play_page = view_page; }
                paint_full();
            }
            if (a == 's' || a == 'S') save_pattern();
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            int step, row, btn = 0;
            if (hit_grid(rx, ry, &step, &row)) {
                /* Start a drag-paint: drag_row is the row we're
                 * painting (or 0 to wipe, if the first cell was
                 * already that row). */
                drag_active    = 1;
                last_drag_step = -1;
                drag_row = (patterns[view_page][step] == row) ? 0 : row;
                if (drag_row && notes_hz[drag_row])
                    sound_beep(notes_hz[drag_row], 60);
                drag_to_step(step);
            } else if (hit_button(rx, ry, &btn)) {
                if (btn == BTN_PLAY) {
                    playing = 1;
                    last_step_ms = ticks_ms();
                    cur_step = 0;
                    play_page = view_page;
                    paint_buttons();
                } else if (btn == BTN_STOP) {
                    playing = 0;
                    sound_tone_off();
                    paint_full();
                } else if (btn == BTN_CLR) {
                    clear_page(view_page);
                    paint_grid();
                } else if (btn == BTN_SAVE) {
                    int was_playing = playing;
                    playing = 0;
                    sound_tone_off();
                    save_pattern();
                    if (was_playing) {
                        playing = 1;
                        last_step_ms = ticks_ms();
                    }
                } else if (btn == BTN_BPMD) {
                    bpm -= 10; if (bpm < 30) bpm = 30;
                    paint_buttons();
                } else if (btn == BTN_BPMU) {
                    bpm += 10; if (bpm > 300) bpm = 300;
                    paint_buttons();
                } else if (btn == BTN_PREVP) {
                    goto_page(view_page - 1);
                } else if (btn == BTN_NEXTP) {
                    goto_page(view_page + 1);
                } else if (btn == BTN_ADDP) {
                    add_page();
                } else if (btn == BTN_DELP) {
                    del_page();
                }
            }
        } else if (ev.type == GUI_EV_MOUSE_MOVE && drag_active && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            int step;
            if (hit_grid_col(rx, ry, &step)) drag_to_step(step);
        } else if (ev.type == GUI_EV_MOUSE_UP) {
            drag_active = 0;
            last_drag_step = -1;
        }
    }
}
