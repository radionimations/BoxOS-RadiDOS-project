/* WINMINES — Minesweeper, BoxOS 2.0 Games pack.
 *
 * Classic 9x9 grid with 10 mines. Left-click reveals a cell; if
 * the cell has no adjacent mines, flood-fill reveals neighbours.
 * Right-click (or shift+left-click) flags a suspected mine.
 *
 * Reveal a mine and you lose. Reveal every safe cell and you win. */

#include "boxos_app.h"

#define COLS 9
#define ROWS 9
#define MINES 10

#define CELL 20
#define MARGIN 8
#define TOP_BAR 28

#define WIN_W (COLS * CELL + 2 * MARGIN)
#define WIN_H (ROWS * CELL + TOP_BAR + 2 * MARGIN)

#define WIN_X 220
#define WIN_Y 80

static int win;
static int cw, ch;
static int win_x = WIN_X, win_y = WIN_Y;

/* Game state. */
static uint8_t  mines[ROWS][COLS];     /* 1 if cell holds a mine */
static uint8_t  adj  [ROWS][COLS];     /* count of neighbours */
static uint8_t  rev  [ROWS][COLS];     /* revealed flag */
static uint8_t  flag [ROWS][COLS];     /* user flag */
static int      revealed_count;        /* safe cells revealed */
static int      flag_count;            /* placed flags */
static int      game_state;            /* 0=playing 1=won -1=lost */
static uint64_t game_start_ms;

/* Tiny xorshift PRNG seeded from ticks_ms. */
static uint32_t rng_state = 0x12345678u;
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int strlen_l(const char* s) { int n=0; while(s&&s[n])n++; return n; }
static void int_to_str(int v, char* out) {
    int neg = 0; if (v < 0) { neg = 1; v = -v; }
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    int o = 0; if (neg) out[o++] = '-';
    while (n) out[o++] = t[--n];
    out[o] = 0;
}

static void new_game(void) {
    rng_state = (uint32_t)(ticks_ms() ^ 0xdeadbeef) | 1u;
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        mines[r][c] = 0; adj[r][c] = 0; rev[r][c] = 0; flag[r][c] = 0;
    }
    int placed = 0;
    while (placed < MINES) {
        int r = rng_next() % ROWS;
        int c = rng_next() % COLS;
        if (!mines[r][c]) { mines[r][c] = 1; placed++; }
    }
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        int n = 0;
        for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) continue;
            if (mines[nr][nc]) n++;
        }
        adj[r][c] = (uint8_t)n;
    }
    revealed_count = 0;
    flag_count = 0;
    game_state = 0;
    game_start_ms = ticks_ms();
}

/* Iterative flood-fill reveal — recursion is risky on app stacks. */
static void reveal_at(int r, int c) {
    /* Simple stack via a flat array (max COLS*ROWS = 81 entries). */
    int stk_r[81], stk_c[81], sp = 0;
    stk_r[sp] = r; stk_c[sp] = c; sp++;
    while (sp > 0) {
        sp--;
        int rr = stk_r[sp], cc = stk_c[sp];
        if (rr < 0 || rr >= ROWS || cc < 0 || cc >= COLS) continue;
        if (rev[rr][cc] || flag[rr][cc]) continue;
        rev[rr][cc] = 1;
        revealed_count++;
        if (adj[rr][cc] != 0) continue;
        for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            if (sp < 81) {
                stk_r[sp] = rr + dr; stk_c[sp] = cc + dc; sp++;
            }
        }
    }
}

/* ---- painting ------------------------------------------------- */

static const uint8_t number_colours[9] = {
    7,    /* unused (0 -> blank) */
    9,    /* 1 = blue   */
    2,    /* 2 = green  */
    4,    /* 3 = red    */
    1,    /* 4 = dark blue */
    6,    /* 5 = brown  */
    3,    /* 6 = cyan   */
    0,    /* 7 = black  */
    8,    /* 8 = grey   */
};

static int cell_x(int c) { return MARGIN + c * CELL; }
static int cell_y(int r) { return TOP_BAR + r * CELL; }

static void paint_cell(int r, int c) {
    int x = cell_x(c);
    int y = cell_y(r);
    if (rev[r][c]) {
        /* Revealed — flat light-grey. */
        gui_fill_rect(win, x, y, CELL, CELL, 7);
        gui_fill_rect(win, x, y, CELL, 1, 8);
        gui_fill_rect(win, x, y, 1, CELL, 8);
        if (mines[r][c]) {
            /* Hit-mine cell: red background, X in centre. */
            gui_fill_rect(win, x + 1, y + 1, CELL - 2, CELL - 2, 4);
            gui_text(win, x + 6, y + 6, "*", 0, 4);
        } else if (adj[r][c] > 0) {
            char s[2] = { (char)('0' + adj[r][c]), 0 };
            uint8_t col = number_colours[adj[r][c]];
            gui_text(win, x + 6, y + 6, s, col, 7);
        }
    } else {
        /* Unrevealed — raised look: light face, dark right/bottom. */
        gui_fill_rect(win, x, y, CELL, CELL, 8);
        gui_fill_rect(win, x + 1, y + 1, CELL - 2, CELL - 2, 7);
        if (flag[r][c]) {
            gui_fill_rect(win, x + 5, y + 5, CELL - 10, CELL - 10, 4);
            gui_text(win, x + 6, y + 6, "F", 15, 4);
        }
    }
}

static void paint_grid(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            paint_cell(r, c);
}

static void paint_top_bar(void) {
    int by = 4;
    gui_fill_rect(win, MARGIN, by, cw - 2 * MARGIN, 20, 8);
    /* Mines remaining = total - flags */
    int remaining = MINES - flag_count;
    char buf[8]; int_to_str(remaining, buf);
    gui_fill_rect(win, MARGIN + 4, by + 2, 36, 16, 0);
    gui_text(win, MARGIN + 6, by + 5, buf, 4, 0);
    /* Status face (centre): :) playing, :| won, :( lost */
    int sx = MARGIN + (cw - 2 * MARGIN) / 2 - 8;
    gui_fill_rect(win, sx - 2, by + 2, 18, 16, 14);
    const char* face = game_state == 0 ? ":)" :
                       game_state > 0  ? ":D" : "X(";
    gui_text(win, sx, by + 5, face, 0, 14);
    /* Timer (seconds since game start) */
    int t = (int)((ticks_ms() - game_start_ms) / 1000);
    if (t > 999) t = 999;
    if (game_state != 0) t = (int)((game_start_ms / 1000)); /* freeze on end */
    char tbuf[8]; int_to_str(t, tbuf);
    int tw = strlen_l(tbuf) * 8;
    gui_fill_rect(win, cw - MARGIN - 40, by + 2, 36, 16, 0);
    gui_text(win, cw - MARGIN - 6 - tw, by + 5, tbuf, 4, 0);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_top_bar();
    paint_grid();
}

/* ---- hit-test + click handling -------------------------------- */

static int hit_grid(int rx, int ry, int* out_r, int* out_c) {
    if (rx < MARGIN || rx >= MARGIN + COLS * CELL) return 0;
    if (ry < TOP_BAR || ry >= TOP_BAR + ROWS * CELL) return 0;
    *out_c = (rx - MARGIN) / CELL;
    *out_r = (ry - TOP_BAR) / CELL;
    return 1;
}

static int hit_smiley(int rx, int ry) {
    int by = 4;
    int sx = MARGIN + (cw - 2 * MARGIN) / 2 - 8;
    return rx >= sx - 2 && rx < sx + 16 && ry >= by + 2 && ry < by + 18;
}

static void reveal_all_mines(void) {
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        if (mines[r][c]) rev[r][c] = 1;
    }
}

static void on_left_click(int r, int c) {
    if (game_state != 0) return;
    if (rev[r][c] || flag[r][c]) return;
    if (mines[r][c]) {
        rev[r][c] = 1;
        reveal_all_mines();
        game_state = -1;
        sound_error();
        return;
    }
    reveal_at(r, c);
    if (revealed_count >= ROWS * COLS - MINES) {
        game_state = 1;
        sound_ok();
    }
}

static void on_right_click(int r, int c) {
    if (game_state != 0) return;
    if (rev[r][c]) return;
    if (flag[r][c]) { flag[r][c] = 0; flag_count--; }
    else            { flag[r][c] = 1; flag_count++; }
    sound_beep(660, 30);
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Minesweeper", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINMINES: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    new_game();
    paint_full();

    uint64_t last_timer_paint = 0;
    for (;;) {
        /* Live timer in the top bar — repaint once a second. */
        uint64_t now = ticks_ms();
        if (game_state == 0 && now - last_timer_paint > 950) {
            last_timer_paint = now;
            paint_top_bar();
        }

        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(15); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == 'n' || a == 'N' || a == 'r' || a == 'R') {
                new_game(); paint_full();
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            if (hit_smiley(rx, ry)) {
                new_game(); paint_full();
                continue;
            }
            int r = 0, c = 0;
            if (hit_grid(rx, ry, &r, &c)) {
                if (ev.arg1 & 1)      on_left_click(r, c);
                else if (ev.arg1 & 2) on_right_click(r, c);
                paint_full();
            }
        }
    }
}
