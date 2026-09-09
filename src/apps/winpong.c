/* WINPONG — two-player Pong for BoxOS 2.0.
 *
 * Left player: W (up) / S (down).
 * Right player: J (up) / K (down) — chosen so it works on any
 * keyboard without arrow-key remapping in the kernel.
 * First to 5 points wins. Esc to quit, R to restart. */

#include "boxos_app.h"

#define WIN_W   500
#define WIN_H   320
#define WIN_X   ((640 - WIN_W) / 2)
#define WIN_Y   60

static int win;
static int cw, ch;
static int win_x = WIN_X, win_y = WIN_Y;

#define COURT_TOP    24
#define PADDLE_W     6
#define PADDLE_H     48
#define BALL_SIZE    8
#define PADDLE_MARG  10
#define PADDLE_STEP  6

#define SCORE_TO_WIN 5

static int paddle_l_y, paddle_r_y;
static int prev_pl_y, prev_pr_y;
static int ball_x, ball_y;
static int prev_ball_x, prev_ball_y;
static int ball_dx, ball_dy;
static int score_l, score_r;
static int game_state;        /* 0 playing, 1 paused/waiting, 2 game over */

static int strlen_l(const char* s) { int n=0; while(s&&s[n])n++; return n; }
static void int_to_str(int v, char* out) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    int o = 0;
    while (n) out[o++] = t[--n];
    out[o] = 0;
}

static uint32_t rng_state = 1u;
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return x;
}

static void serve_ball(int to_right) {
    ball_x = cw / 2 - BALL_SIZE / 2;
    ball_y = (COURT_TOP + ch) / 2 - BALL_SIZE / 2;
    ball_dx = to_right ? 3 : -3;
    ball_dy = ((int)(rng_next() & 1)) ? 2 : -2;
    prev_ball_x = ball_x;
    prev_ball_y = ball_y;
}

static void new_game(void) {
    paddle_l_y = paddle_r_y = (COURT_TOP + ch) / 2 - PADDLE_H / 2;
    prev_pl_y = paddle_l_y;
    prev_pr_y = paddle_r_y;
    score_l = score_r = 0;
    game_state = 0;
    rng_state = (uint32_t)(ticks_ms() ^ 0xACE1u) | 1u;
    serve_ball(1);
}

static void paint_court_static(void) {
    /* Black playfield with white centre net. */
    gui_fill_rect(win, 0, COURT_TOP, cw, ch - COURT_TOP, 0);
    for (int y = COURT_TOP + 4; y < ch - 4; y += 12) {
        gui_fill_rect(win, cw / 2 - 1, y, 2, 6, 15);
    }
}

static void paint_score(void) {
    gui_fill_rect(win, 0, 0, cw, COURT_TOP, 8);
    char sl[8], sr[8];
    int_to_str(score_l, sl);
    int_to_str(score_r, sr);
    gui_text(win, 20, 8, sl, 15, 8);
    gui_text(win, cw - 28, 8, sr, 15, 8);
    const char* m = game_state == 2
        ? (score_l > score_r ? "Left wins! R = restart" : "Right wins! R = restart")
        : "W/S vs J/K  -  Esc to quit";
    int mw = strlen_l(m) * 8;
    gui_text(win, (cw - mw) / 2, 8, m, 15, 8);
}

static void paint_paddle(int x, int y, int prev_y) {
    if (prev_y != y) {
        gui_fill_rect(win, x, prev_y, PADDLE_W, PADDLE_H, 0);
    }
    gui_fill_rect(win, x, y, PADDLE_W, PADDLE_H, 15);
}

static void paint_ball(void) {
    if (prev_ball_x != ball_x || prev_ball_y != ball_y) {
        gui_fill_rect(win, prev_ball_x, prev_ball_y, BALL_SIZE, BALL_SIZE, 0);
        /* Redraw centre net if we erased over it. */
        int cx = cw / 2 - 1;
        if (prev_ball_x + BALL_SIZE > cx && prev_ball_x < cx + 2) {
            for (int y = COURT_TOP + 4; y < ch - 4; y += 12) {
                if (prev_ball_y + BALL_SIZE > y && prev_ball_y < y + 6) {
                    gui_fill_rect(win, cx, y, 2, 6, 15);
                }
            }
        }
    }
    gui_fill_rect(win, ball_x, ball_y, BALL_SIZE, BALL_SIZE, 15);
    prev_ball_x = ball_x;
    prev_ball_y = ball_y;
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_score();
    paint_court_static();
    paint_paddle(PADDLE_MARG, paddle_l_y, paddle_l_y);
    paint_paddle(cw - PADDLE_MARG - PADDLE_W, paddle_r_y, paddle_r_y);
    paint_ball();
}

static void clamp_paddle(int* y) {
    if (*y < COURT_TOP + 2) *y = COURT_TOP + 2;
    if (*y > ch - 2 - PADDLE_H) *y = ch - 2 - PADDLE_H;
}

static void on_key_press(unsigned a) {
    if (a == 'w' || a == 'W') paddle_l_y -= PADDLE_STEP;
    if (a == 's' || a == 'S') paddle_l_y += PADDLE_STEP;
    if (a == 'j' || a == 'J') paddle_r_y -= PADDLE_STEP;
    if (a == 'k' || a == 'K') paddle_r_y += PADDLE_STEP;
    if (a == 'r' || a == 'R') new_game();
    clamp_paddle(&paddle_l_y);
    clamp_paddle(&paddle_r_y);
}

static void tick_ball(void) {
    if (game_state != 0) return;
    ball_x += ball_dx;
    ball_y += ball_dy;

    /* Top / bottom walls */
    if (ball_y < COURT_TOP + 2) {
        ball_y = COURT_TOP + 2;
        ball_dy = -ball_dy;
        sound_beep(880, 15);
    }
    if (ball_y + BALL_SIZE > ch - 2) {
        ball_y = ch - 2 - BALL_SIZE;
        ball_dy = -ball_dy;
        sound_beep(880, 15);
    }

    /* Left paddle collision */
    int lpx = PADDLE_MARG;
    if (ball_x <= lpx + PADDLE_W &&
        ball_x >= lpx - 2 &&
        ball_y + BALL_SIZE >= paddle_l_y &&
        ball_y <= paddle_l_y + PADDLE_H &&
        ball_dx < 0) {
        ball_dx = -ball_dx + 0;
        ball_x = lpx + PADDLE_W;
        sound_beep(660, 20);
    }
    /* Right paddle collision */
    int rpx = cw - PADDLE_MARG - PADDLE_W;
    if (ball_x + BALL_SIZE >= rpx &&
        ball_x + BALL_SIZE <= rpx + PADDLE_W + 2 &&
        ball_y + BALL_SIZE >= paddle_r_y &&
        ball_y <= paddle_r_y + PADDLE_H &&
        ball_dx > 0) {
        ball_dx = -ball_dx;
        ball_x = rpx - BALL_SIZE;
        sound_beep(660, 20);
    }

    /* Off the left edge — right player scores */
    if (ball_x + BALL_SIZE < 0) {
        score_r++;
        sound_beep(220, 80);
        if (score_r >= SCORE_TO_WIN) { game_state = 2; sound_ok(); }
        else serve_ball(0);
    }
    /* Off the right edge — left player scores */
    if (ball_x > cw) {
        score_l++;
        sound_beep(220, 80);
        if (score_l >= SCORE_TO_WIN) { game_state = 2; sound_ok(); }
        else serve_ball(1);
    }
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Pong", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINPONG: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    new_game();
    paint_full();

    uint64_t last_tick = ticks_ms();
    int needs_score_repaint = 0;

    for (;;) {
        uint64_t now = ticks_ms();
        if (now - last_tick >= 25) {                /* ~40 Hz physics */
            last_tick = now;
            int old_l = score_l, old_r = score_r, old_state = game_state;
            tick_ball();
            paint_ball();
            if (old_l != score_l || old_r != score_r || old_state != game_state) {
                needs_score_repaint = 1;
            }
        }
        if (paddle_l_y != prev_pl_y) {
            paint_paddle(PADDLE_MARG, paddle_l_y, prev_pl_y);
            prev_pl_y = paddle_l_y;
        }
        if (paddle_r_y != prev_pr_y) {
            paint_paddle(cw - PADDLE_MARG - PADDLE_W, paddle_r_y, prev_pr_y);
            prev_pr_y = paddle_r_y;
        }
        if (needs_score_repaint) {
            paint_score();
            needs_score_repaint = 0;
        }

        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(5); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            on_key_press(a);
        }
    }
}
