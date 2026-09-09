/* WINSNAKE — Snake game in a window with PC speaker SFX.
 *
 * Grid: 30x18 cells of 12x12 pixels = 360x216. Window content is
 * 360x244 (with a 28-px header for score and instructions).
 *
 * Controls: arrow keys (sent as raw scancodes by the keyboard driver,
 * but only the ASCII we get is what we get) -- so we map h/j/k/l /
 * w/a/s/d for movement. Esc to quit. */

#include "boxos_app.h"

#define COLS 30
#define ROWS 18
#define CELL 12
#define HEADER_H 28
#define WIN_W (COLS * CELL + 2)         /* +2 border                   */
#define WIN_H (ROWS * CELL + HEADER_H + 14)

#define MAX_LEN (COLS * ROWS)

static int win;
static int cw, ch;
static int win_x = 140, win_y = 100;

static int snake_x[MAX_LEN], snake_y[MAX_LEN];
static int snake_len, dir_dx, dir_dy;
static int food_x, food_y;
static int score;
static int alive;
static unsigned rng = 0xCAFE;

static int rand_range(int n) {
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 16) % (unsigned)n);
}

static void place_food(void) {
retry:
    food_x = rand_range(COLS);
    food_y = rand_range(ROWS);
    for (int i = 0; i < snake_len; i++)
        if (snake_x[i] == food_x && snake_y[i] == food_y) goto retry;
}

static void int_to_str(int v, char* out) {
    char tmp[12]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
    int o = 0;
    while (t) out[o++] = tmp[--t];
    out[o] = 0;
}

static void paint_header(void) {
    gui_fill_rect(win, 0, 0, cw, HEADER_H, 1);
    gui_text(win, 8, 8, "SCORE:", 15, 1);
    char num[12]; int_to_str(score, num);
    gui_text(win, 56, 8, num, 14, 1);
    gui_text(win, 140, 8, "WASD or HJKL  |  Esc=quit", 11, 1);
}

static void paint_cell(int x, int y, uint8_t color) {
    int px = 1 + x * CELL;
    int py = HEADER_H + 1 + y * CELL;
    gui_fill_rect(win, px, py, CELL, CELL, color);
}

static void paint_full(void) {
    paint_header();
    /* board background */
    gui_fill_rect(win, 0, HEADER_H, cw, ch - HEADER_H, 0);
    /* food */
    paint_cell(food_x, food_y, 12);
    /* snake */
    for (int i = 0; i < snake_len; i++) {
        paint_cell(snake_x[i], snake_y[i], i == 0 ? 14 : 2);
    }
}

static void reset_game(void) {
    snake_len = 4;
    int sx = COLS / 2, sy = ROWS / 2;
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = sx - i;
        snake_y[i] = sy;
    }
    dir_dx = 1; dir_dy = 0;
    score = 0;
    alive = 1;
    place_food();
    paint_full();
}

static void game_over(void) {
    alive = 0;
    sound_error();
    gui_fill_rect(win, cw / 2 - 80, ch / 2 - 16, 160, 32, 4);
    gui_text(win, cw / 2 - 36, ch / 2 - 4, "GAME OVER", 15, 4);
    /* Halt-style state — wait for any key to restart. */
}

static void step(void) {
    if (!alive) return;
    int nx = snake_x[0] + dir_dx;
    int ny = snake_y[0] + dir_dy;
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        game_over(); return;
    }
    /* self-collide check (skip the tail tip — it's about to vacate). */
    for (int i = 0; i < snake_len - 1; i++) {
        if (snake_x[i] == nx && snake_y[i] == ny) { game_over(); return; }
    }
    int ate = (nx == food_x && ny == food_y);
    if (ate) {
        if (snake_len < MAX_LEN) snake_len++;
        score += 10;
        sound_beep(1320, 40);
        place_food();
    } else {
        /* clear old tail */
        paint_cell(snake_x[snake_len - 1], snake_y[snake_len - 1], 0);
    }
    /* shift body */
    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = nx;
    snake_y[0] = ny;
    /* paint head + previous head as body */
    paint_cell(snake_x[1], snake_y[1], 2);
    paint_cell(nx, ny, 14);
    if (ate) paint_cell(food_x, food_y, 12);
    paint_header();
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Snake", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINSNAKE: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    rng ^= (unsigned)ticks_ms();
    reset_game();

    uint64_t last_step = ticks_ms();
    const uint64_t step_ms = 90;

    for (;;) {
        struct gui_event ev;
        while (gui_poll_event(&ev)) {
            if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }
            if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
                unsigned a = ev.arg1 & 0xFF;
                if (a == 27) { gui_close_window(win); return 0; }
                if (!alive) { reset_game(); continue; }
                if ((a == 'w' || a == 'W' || a == 'k' || a == 'K') && dir_dy != 1) { dir_dx = 0; dir_dy = -1; }
                if ((a == 's' || a == 'S' || a == 'j' || a == 'J') && dir_dy != -1){ dir_dx = 0; dir_dy =  1; }
                if ((a == 'a' || a == 'A' || a == 'h' || a == 'H') && dir_dx != 1) { dir_dx = -1; dir_dy = 0; }
                if ((a == 'd' || a == 'D' || a == 'l' || a == 'L') && dir_dx != -1){ dir_dx =  1; dir_dy = 0; }
            }
        }
        uint64_t now = ticks_ms();
        if (now - last_step >= step_ms) {
            last_step = now;
            step();
        }
        sleep_ms(10);
    }
}
