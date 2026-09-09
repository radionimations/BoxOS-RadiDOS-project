/* WINRVRS — Reversi (Othello) for BoxOS 2.0.
 *
 * Classic 8x8 board. You are Black; the computer is White. Left-click
 * a square that's a legal move and your piece goes there, flipping
 * every captured line. After your move the AI replies (greedy: it
 * picks the move that flips the most discs). If a side has no legal
 * move it passes; if neither side can move the game ends. R = new
 * game, Esc = close. */

#include "boxos_app.h"

#define N            8
#define CELL        32
#define MARGIN       8
#define TOP_BAR     32
#define BOTTOM_BAR  16

#define WIN_W (N * CELL + 2 * MARGIN)
#define WIN_H (N * CELL + TOP_BAR + BOTTOM_BAR + 2 * MARGIN)

#define WIN_X 180
#define WIN_Y 40

#define EMPTY 0
#define BLACK 1
#define WHITE 2

static int win;
static int cw, ch;

static uint8_t board[N][N];
static int     turn;            /* whose turn — BLACK or WHITE */
static int     game_over;
static int     last_passed;     /* did the previous player skip */

static void int_to_str(int v, char* out) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    int o = 0;
    while (n) out[o++] = t[--n];
    out[o] = 0;
}

static int opp(int p) { return p == BLACK ? WHITE : BLACK; }

/* Walk one direction from (r,c) for player p. Returns the count of
 * opponent discs flipped (0 if not a legal capture along this ray). */
static int ray_flip(int r, int c, int dr, int dc, int p, int do_flip) {
    int nr = r + dr, nc = c + dc;
    int count = 0;
    while (nr >= 0 && nr < N && nc >= 0 && nc < N) {
        if (board[nr][nc] == opp(p)) {
            count++;
            nr += dr; nc += dc;
            continue;
        }
        if (board[nr][nc] == p) {
            if (count == 0) return 0;
            if (do_flip) {
                int fr = r + dr, fc = c + dc;
                for (int k = 0; k < count; k++) {
                    board[fr][fc] = (uint8_t)p;
                    fr += dr; fc += dc;
                }
            }
            return count;
        }
        return 0;
    }
    return 0;
}

static int captures_at(int r, int c, int p) {
    if (board[r][c] != EMPTY) return 0;
    int total = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if (dr | dc) total += ray_flip(r, c, dr, dc, p, 0);
    return total;
}

static int try_play(int r, int c, int p) {
    if (board[r][c] != EMPTY) return 0;
    int any = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            if (ray_flip(r, c, dr, dc, p, 1) > 0) any = 1;
        }
    }
    if (any) board[r][c] = (uint8_t)p;
    return any;
}

static int has_any_move(int p) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (captures_at(r, c, p) > 0) return 1;
    return 0;
}

/* Greedy AI: pick the move with the highest capture count, breaking
 * ties towards the upper-left so play is deterministic. */
static int ai_pick(int* out_r, int* out_c) {
    int best = -1, br = -1, bc = -1;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            int n = captures_at(r, c, WHITE);
            if (n > best) { best = n; br = r; bc = c; }
        }
    }
    if (best <= 0) return 0;
    *out_r = br; *out_c = bc;
    return 1;
}

static void new_game(void) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            board[r][c] = EMPTY;
    board[3][3] = WHITE; board[4][4] = WHITE;
    board[3][4] = BLACK; board[4][3] = BLACK;
    turn = BLACK;
    game_over = 0;
    last_passed = 0;
}

/* ---- painting -------------------------------------------------- */

static int board_x(void) { return MARGIN; }
static int board_y(void) { return TOP_BAR + MARGIN; }

static void draw_disc(int r, int c, int colour) {
    int x = board_x() + c * CELL;
    int y = board_y() + r * CELL;
    /* Octagon-ish disc: a fat plus + a centre square = looks round
     * enough at 32 px without needing per-pixel circle code. */
    int q = CELL / 4;
    gui_fill_rect(win, x + q,      y + 2,      CELL - 2*q, CELL - 4, colour);
    gui_fill_rect(win, x + 2,      y + q,      CELL - 4,   CELL - 2*q, colour);
    /* Edge highlight so black discs read against the green felt. */
    int ec = (colour == 0) ? 8 : 0;
    gui_fill_rect(win, x + q,      y + 2,      CELL - 2*q, 1, ec);
    gui_fill_rect(win, x + q,      y + CELL-3, CELL - 2*q, 1, ec);
    gui_fill_rect(win, x + 2,      y + q,      1, CELL - 2*q, ec);
    gui_fill_rect(win, x + CELL-3, y + q,      1, CELL - 2*q, ec);
}

static void draw_cell(int r, int c) {
    int x = board_x() + c * CELL;
    int y = board_y() + r * CELL;
    gui_fill_rect(win, x, y, CELL, CELL, 2);          /* green felt */
    gui_fill_rect(win, x, y, CELL, 1, 0);
    gui_fill_rect(win, x, y + CELL - 1, CELL, 1, 0);
    gui_fill_rect(win, x, y, 1, CELL, 0);
    gui_fill_rect(win, x + CELL - 1, y, 1, CELL, 0);
    if (board[r][c] == BLACK) draw_disc(r, c, 0);
    else if (board[r][c] == WHITE) draw_disc(r, c, 15);
    /* Hint marker for legal moves on the human's turn. */
    else if (!game_over && turn == BLACK && captures_at(r, c, BLACK) > 0) {
        gui_fill_rect(win, x + CELL/2 - 2, y + CELL/2 - 2, 4, 4, 14);
    }
}

static void draw_board(void) {
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            draw_cell(r, c);
}

static void draw_status(void) {
    int b = 0, w = 0;
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            if (board[r][c] == BLACK) b++;
            else if (board[r][c] == WHITE) w++;
        }
    /* Top bar: scores + whose turn. */
    gui_fill_rect(win, 0, 0, cw, TOP_BAR, 7);
    char buf[32];
    int_to_str(b, buf);
    gui_text(win, MARGIN,         8,  "BLACK", 0, 7);
    gui_text(win, MARGIN + 56,    8,  buf,     0, 7);
    int_to_str(w, buf);
    gui_text(win, MARGIN,         20, "WHITE", 15, 7);
    gui_text(win, MARGIN + 56,    20, buf,     15, 7);
    const char* who;
    if (game_over) {
        if (b > w) who = "BLACK WINS";
        else if (w > b) who = "WHITE WINS";
        else who = "DRAW";
    } else {
        who = (turn == BLACK) ? "YOUR TURN" : "AI THINKING";
    }
    gui_text(win, MARGIN + 120, 14, who, 1, 7);
    /* Footer hint. */
    gui_fill_rect(win, 0, ch - BOTTOM_BAR, cw, BOTTOM_BAR, 8);
    gui_text(win, MARGIN, ch - BOTTOM_BAR + 4,
             "R = new game   Esc = close", 15, 8);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    draw_status();
    /* Border around the board. */
    gui_fill_rect(win, board_x() - 2, board_y() - 2,
                  N * CELL + 4, N * CELL + 4, 0);
    draw_board();
}

/* ---- input ---------------------------------------------------- */

static int hit_cell(int rx, int ry, int* out_r, int* out_c) {
    if (rx < board_x() || ry < board_y()) return 0;
    int c = (rx - board_x()) / CELL;
    int r = (ry - board_y()) / CELL;
    if (r < 0 || r >= N || c < 0 || c >= N) return 0;
    *out_r = r; *out_c = c;
    return 1;
}

/* Advance the turn marker; if the next side has no moves, pass. If
 * both pass in a row the game is over. */
static void advance_turn(void) {
    int next = opp(turn);
    if (has_any_move(next)) {
        turn = next;
        last_passed = 0;
        return;
    }
    if (last_passed) { game_over = 1; return; }
    last_passed = 1;
    /* Same player keeps the turn — opponent had no move. If THEY
     * also have no move, game is over too. */
    if (!has_any_move(turn)) game_over = 1;
}

static void ai_turn(void) {
    int r, c;
    if (!ai_pick(&r, &c)) {                /* AI must pass */
        if (last_passed) { game_over = 1; }
        else { last_passed = 1; }
        if (!has_any_move(BLACK)) game_over = 1;
        turn = BLACK;
        return;
    }
    try_play(r, c, WHITE);
    advance_turn();
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Reversi", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINRVRS: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    new_game();
    paint_full();

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(15); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == 'r' || a == 'R') {
                new_game();
                paint_full();
                continue;
            }
        }

        if (game_over) continue;
        if (turn != BLACK) continue;       /* AI moves automatically */

        if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - WIN_X - 1;
            int ry = ev.y - WIN_Y - 13;
            int r, c;
            if (!hit_cell(rx, ry, &r, &c)) continue;
            if (captures_at(r, c, BLACK) == 0) { sound_beep(220, 25); continue; }
            try_play(r, c, BLACK);
            advance_turn();
            paint_full();
            if (game_over) continue;
            /* AI's turn — small visible pause so the user sees their
             * move land before the reply. */
            sleep_ms(250);
            while (!game_over && turn == WHITE) {
                ai_turn();
                paint_full();
            }
        }
    }
}
