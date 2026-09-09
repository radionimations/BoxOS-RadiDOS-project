/* WINTRIS — Tetris clone for BoxOS 2.0.
 *
 * 10x20 well, 7 standard tetrominoes (I O T S Z J L) with all four
 * rotations encoded as 4x4 bitmaps. Classic NES scoring (40/100/300/
 * 1200 * level), level up every 10 lines, gravity speeds up per
 * level. Controls:
 *   Left/Right   — slide
 *   Up           — rotate clockwise
 *   Down         — soft drop (faster gravity while held)
 *   Space        — hard drop (slam to the floor + lock)
 *   P            — pause
 *   R            — new game
 *   Esc          — close window
 */

#include "boxos_app.h"

#define COLS         10
#define ROWS         20
#define CELL         18
#define BOARD_W      (COLS * CELL)
#define BOARD_H      (ROWS * CELL)
#define PANEL_W      96
#define MARGIN       8

#define WIN_W (BOARD_W + PANEL_W + 3 * MARGIN)
#define WIN_H (BOARD_H + 2 * MARGIN)

#define WIN_X 200
#define WIN_Y 30

static int win;
static int cw, ch;

/* Each 4x4 tetromino rotation packed as a uint16_t. Bit 15 is cell
 * (row 0, col 0); bit 0 is cell (row 3, col 3). */
static const uint16_t pieces[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },   /* I */
    { 0x6600, 0x6600, 0x6600, 0x6600 },   /* O */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },   /* T */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },   /* S */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },   /* Z */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },   /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },   /* L */
};

/* VGA palette indices. Board cell stores piece+1 (0 = empty). */
static const uint8_t piece_color[7] = { 11, 14, 13, 10, 12, 9, 6 };

static uint8_t board[ROWS][COLS];

static int      cur, cur_rot, cur_x, cur_y;
static int      next_piece;
static int      score, level, lines, paused, game_over;
static uint64_t last_drop_ms;
static uint64_t soft_until_ms;

/* xorshift PRNG */
static uint32_t rng = 0xdeadbeef;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

static int piece_cell(int p, int rot, int r, int c) {
    return (pieces[p][rot] >> (15 - r * 4 - c)) & 1;
}

static void int_to_str(int v, char* out) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    int o = 0;
    while (n) out[o++] = t[--n];
    out[o] = 0;
}

static int collides(int p, int rot, int px, int py) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(p, rot, r, c)) continue;
            int br = py + r, bc = px + c;
            if (bc < 0 || bc >= COLS) return 1;
            if (br >= ROWS) return 1;
            if (br >= 0 && board[br][bc]) return 1;
        }
    }
    return 0;
}

static void lock_piece(void) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(cur, cur_rot, r, c)) continue;
            int br = cur_y + r, bc = cur_x + c;
            if (br >= 0 && br < ROWS && bc >= 0 && bc < COLS)
                board[br][bc] = (uint8_t)(cur + 1);
        }
    }
}

static int clear_lines(void) {
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < COLS; c++) if (!board[r][c]) { full = 0; break; }
        if (!full) continue;
        for (int rr = r; rr > 0; rr--)
            for (int c = 0; c < COLS; c++)
                board[rr][c] = board[rr - 1][c];
        for (int c = 0; c < COLS; c++) board[0][c] = 0;
        cleared++;
        r++;   /* re-check the row that slid down into this slot */
    }
    return cleared;
}

static void update_score(int cleared) {
    static const int pts[5] = { 0, 40, 100, 300, 1200 };
    score += pts[cleared] * (level + 1);
    lines += cleared;
    int new_level = lines / 10;
    if (new_level > level) level = new_level;
}

static int drop_interval_ms(void) {
    int ms = 800 - level * 70;
    if (ms < 80) ms = 80;
    if (soft_until_ms > ticks_ms() && ms > 60) ms = 60;
    return ms;
}

static void spawn(void) {
    cur = next_piece;
    next_piece = (int)(rng_next() % 7);
    cur_rot = 0;
    cur_x = (COLS - 4) / 2;
    cur_y = (cur == 0) ? -1 : 0;
    if (collides(cur, cur_rot, cur_x, cur_y)) game_over = 1;
    last_drop_ms = ticks_ms();
}

static void new_game(void) {
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) board[r][c] = 0;
    rng = (uint32_t)(ticks_ms() ^ 0xa5a5a5a5u) | 1;
    score = 0; level = 0; lines = 0;
    paused = 0; game_over = 0;
    soft_until_ms = 0;
    next_piece = (int)(rng_next() % 7);
    spawn();
}

/* ---- painting -------------------------------------------------- */

static int board_x(void) { return MARGIN; }
static int board_y(void) { return MARGIN; }
static int panel_x(void) { return MARGIN + BOARD_W + MARGIN; }

static void draw_cell(int x, int y, int color) {
    gui_fill_rect(win, x, y, CELL, CELL, color);
    /* Bevel: light edge top/left, dark edge bottom/right. */
    gui_fill_rect(win, x, y, CELL, 1, 15);
    gui_fill_rect(win, x, y, 1, CELL, 15);
    gui_fill_rect(win, x, y + CELL - 1, CELL, 1, 0);
    gui_fill_rect(win, x + CELL - 1, y, 1, CELL, 0);
}

static void draw_board_bg(void) {
    /* Well backdrop. */
    gui_fill_rect(win, board_x() - 2, board_y() - 2,
                  BOARD_W + 4, BOARD_H + 4, 0);
    gui_fill_rect(win, board_x(), board_y(), BOARD_W, BOARD_H, 8);
}

static void draw_board(void) {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int x = board_x() + c * CELL;
            int y = board_y() + r * CELL;
            if (board[r][c]) draw_cell(x, y, piece_color[board[r][c] - 1]);
            /* No drawing for empty cells; the well backdrop shows. */
            (void)x; (void)y;
        }
    }
    /* Active piece (skip cells above the playfield). */
    if (!game_over) {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (!piece_cell(cur, cur_rot, r, c)) continue;
                int br = cur_y + r, bc = cur_x + c;
                if (br < 0 || br >= ROWS || bc < 0 || bc >= COLS) continue;
                draw_cell(board_x() + bc * CELL, board_y() + br * CELL,
                          piece_color[cur]);
            }
        }
    }
}

static void draw_next_preview(int x, int y) {
    gui_fill_rect(win, x, y, 4 * CELL + 4, 4 * CELL + 4, 0);
    gui_fill_rect(win, x + 2, y + 2, 4 * CELL, 4 * CELL, 8);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!piece_cell(next_piece, 0, r, c)) continue;
            draw_cell(x + 2 + c * CELL, y + 2 + r * CELL,
                      piece_color[next_piece]);
        }
    }
}

static void draw_panel(void) {
    int px = panel_x();
    gui_fill_rect(win, px, MARGIN, PANEL_W, BOARD_H, 7);

    gui_text(win, px + 4, MARGIN + 4, "SCORE", 0, 7);
    char buf[16]; int_to_str(score, buf);
    gui_text(win, px + 4, MARGIN + 16, buf, 1, 7);

    gui_text(win, px + 4, MARGIN + 36, "LEVEL", 0, 7);
    int_to_str(level, buf);
    gui_text(win, px + 4, MARGIN + 48, buf, 1, 7);

    gui_text(win, px + 4, MARGIN + 68, "LINES", 0, 7);
    int_to_str(lines, buf);
    gui_text(win, px + 4, MARGIN + 80, buf, 1, 7);

    gui_text(win, px + 4, MARGIN + 100, "NEXT", 0, 7);
    draw_next_preview(px + 4, MARGIN + 112);

    int hy = MARGIN + 112 + 4 * CELL + 12;
    gui_text(win, px + 4, hy +  0, "Arrows:",   0, 7);
    gui_text(win, px + 4, hy + 12, " move/rot", 0, 7);
    gui_text(win, px + 4, hy + 24, "Space=drop",0, 7);
    gui_text(win, px + 4, hy + 36, "P=pause",   0, 7);
    gui_text(win, px + 4, hy + 48, "R=new",     0, 7);

    if (paused) gui_text(win, px + 4, hy + 64, "PAUSED",    14, 7);
    if (game_over) gui_text(win, px + 4, hy + 64, "GAME OVER", 12, 7);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    draw_board_bg();
    draw_board();
    draw_panel();
}

/* ---- gameplay -------------------------------------------------- */

static void try_move(int dx, int dy) {
    if (!collides(cur, cur_rot, cur_x + dx, cur_y + dy)) {
        cur_x += dx; cur_y += dy;
    }
}

static void try_rotate(void) {
    int nr = (cur_rot + 1) & 3;
    if (!collides(cur, nr, cur_x, cur_y)) { cur_rot = nr; return; }
    /* Tiny wall-kick: nudge left/right by 1 if blocked. */
    if (!collides(cur, nr, cur_x - 1, cur_y)) { cur_rot = nr; cur_x -= 1; return; }
    if (!collides(cur, nr, cur_x + 1, cur_y)) { cur_rot = nr; cur_x += 1; return; }
}

static void hard_drop(void) {
    while (!collides(cur, cur_rot, cur_x, cur_y + 1)) cur_y++;
    lock_piece();
    int n = clear_lines();
    if (n) update_score(n);
    spawn();
}

static void tick_gravity(void) {
    uint64_t now = ticks_ms();
    if (now - last_drop_ms < (uint64_t)drop_interval_ms()) return;
    last_drop_ms = now;
    if (!collides(cur, cur_rot, cur_x, cur_y + 1)) { cur_y++; return; }
    lock_piece();
    int n = clear_lines();
    if (n) update_score(n);
    spawn();
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Tetris", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINTRIS: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    new_game();
    paint_full();

    for (;;) {
        struct gui_event ev;
        int got = gui_poll_event(&ev);
        if (got && ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (got && ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == 'r' || a == 'R') { new_game(); paint_full(); continue; }
            if (a == 'p' || a == 'P') { paused = !paused; paint_full(); continue; }
            if (game_over || paused) continue;
            if (a == 0x82) { try_move(-1, 0); paint_full(); continue; }    /* Left  */
            if (a == 0x83) { try_move( 1, 0); paint_full(); continue; }    /* Right */
            if (a == 0x80) { try_rotate();    paint_full(); continue; }    /* Up    */
            if (a == 0x81) {                                              /* Down  */
                soft_until_ms = ticks_ms() + 120;
                continue;
            }
            if (a == ' ')  { hard_drop(); paint_full(); continue; }
        }

        if (game_over || paused) { if (!got) sleep_ms(15); continue; }

        tick_gravity();
        paint_full();
        if (!got) sleep_ms(15);
    }
}
