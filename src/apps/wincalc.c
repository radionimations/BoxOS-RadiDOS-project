/* WINCALC — windowed calculator with a clickable button grid plus
 * keyboard input. Reuses the calc.c expression parser. */

#include "boxos_app.h"

/* ---- expression parser (vendored from calc.c) ----------------- */

static int  is_digit(char c) { return c >= '0' && c <= '9'; }
static const char* p;
static int err;

static void skip_ws(void) { while (*p == ' ' || *p == '\t') p++; }

static int64_t parse_expr(void);

static int64_t parse_num(void) {
    int64_t v = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p == '+')  p++;
    skip_ws();
    if (!is_digit(*p)) { err = 1; return 0; }
    while (is_digit(*p)) v = v * 10 + (*p++ - '0');
    return neg ? -v : v;
}

static int64_t parse_atom(void) {
    skip_ws();
    if (*p == '(') {
        p++;
        int64_t v = parse_expr();
        skip_ws();
        if (*p != ')') { err = 1; return 0; }
        p++;
        return v;
    }
    return parse_num();
}

static int64_t parse_term(void) {
    int64_t v = parse_atom();
    for (;;) {
        skip_ws();
        char op = *p;
        if (op != '*' && op != '/' && op != '%') return v;
        p++;
        int64_t r = parse_atom();
        if (op == '*')      v = v * r;
        else if (r == 0)    { err = 1; return 0; }
        else if (op == '/') v = v / r;
        else                v = v % r;
    }
}

static int64_t parse_expr(void) {
    int64_t v = parse_term();
    for (;;) {
        skip_ws();
        char op = *p;
        if (op != '+' && op != '-') return v;
        p++;
        int64_t r = parse_term();
        v = (op == '+') ? v + r : v - r;
    }
}

/* ---- UI -------------------------------------------------------- */

#define WIN_W 256
#define WIN_H 256

#define BTN_COLS 4
#define BTN_ROWS 5
#define BTN_MARGIN 8
#define DISPLAY_H  32

/* Button labels in row-major order, top to bottom, left to right. */
static const char* btn_labels[BTN_ROWS][BTN_COLS] = {
    { "C", "(", ")", "/" },
    { "7", "8", "9", "*" },
    { "4", "5", "6", "-" },
    { "1", "2", "3", "+" },
    { "0", ".", "=", "" },
};

static char  expr[64];
static int   expr_len = 0;
static char  result[40];
static int   has_result = 0;

static void compute(void) {
    expr[expr_len] = 0;
    p = expr;
    err = 0;
    int64_t v = parse_expr();
    skip_ws();
    if (err || *p) {
        const char* m = "error";
        int n = 0;
        while (m[n]) { result[n] = m[n]; n++; }
        result[n] = 0;
    } else {
        char tmp[24]; int t = 0;
        int neg = (v < 0);
        if (neg) v = -v;
        if (v == 0) tmp[t++] = '0';
        while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
        int n = 0;
        if (neg) result[n++] = '-';
        while (t) result[n++] = tmp[--t];
        result[n] = 0;
    }
    has_result = 1;
}

static void btn_rect(int r, int c, int* bx, int* by, int* bw, int* bh) {
    int avail_w = WIN_W - 2 * BTN_MARGIN;
    int avail_h = WIN_H - DISPLAY_H - 2 * BTN_MARGIN - 14;
    *bw = (avail_w - (BTN_COLS - 1) * 4) / BTN_COLS;
    *bh = (avail_h - (BTN_ROWS - 1) * 4) / BTN_ROWS;
    *bx = BTN_MARGIN + c * (*bw + 4);
    *by = DISPLAY_H + BTN_MARGIN + r * (*bh + 4);
}

static void redraw_display(int win) {
    /* Display row at top of content. */
    gui_fill_rect(win, 4, 4, WIN_W - 22, DISPLAY_H - 8, 15);
    expr[expr_len] = 0;
    /* Show the current expression on the left. */
    gui_text(win, 8, 8, expr, 0, 15);
    /* Show last result on the right (truncated). */
    if (has_result) {
        int rx = WIN_W - 18 - (int)8 * 12;
        if (rx < 8 + 8 * expr_len + 8) rx = 8 + 8 * expr_len + 8;
        gui_text(win, rx, 18, result, 4, 15);
    }
}

static void redraw_buttons(int win) {
    for (int r = 0; r < BTN_ROWS; r++) {
        for (int c = 0; c < BTN_COLS; c++) {
            const char* lbl = btn_labels[r][c];
            if (!lbl[0]) continue;
            int bx, by, bw, bh;
            btn_rect(r, c, &bx, &by, &bw, &bh);
            int color = (lbl[0] >= '0' && lbl[0] <= '9') ? 7 : 11;
            if (lbl[0] == '=') color = 14;     /* yellow accent */
            if (lbl[0] == 'C') color = 12;     /* light red     */
            gui_fill_rect(win, bx, by, bw, bh, color);
            int tx = bx + bw / 2 - 4;
            int ty = by + bh / 2 - 4;
            gui_text(win, tx, ty, lbl, 0, color);
        }
    }
}

static int hit_button(int rel_x, int rel_y, int* out_r, int* out_c) {
    for (int r = 0; r < BTN_ROWS; r++) {
        for (int c = 0; c < BTN_COLS; c++) {
            const char* lbl = btn_labels[r][c];
            if (!lbl[0]) continue;
            int bx, by, bw, bh;
            btn_rect(r, c, &bx, &by, &bw, &bh);
            if (rel_x >= bx && rel_x < bx + bw &&
                rel_y >= by && rel_y < by + bh) {
                *out_r = r;
                *out_c = c;
                return 1;
            }
        }
    }
    return 0;
}

static void apply_label(const char* lbl) {
    if (!lbl[0]) return;
    if (lbl[0] == 'C') {
        expr_len = 0;
        has_result = 0;
        return;
    }
    if (lbl[0] == '=') {
        compute();
        return;
    }
    if (expr_len < (int)sizeof(expr) - 1) {
        expr[expr_len++] = lbl[0];
        has_result = 0;
    }
}

static void apply_key(unsigned ascii) {
    if (ascii == 27) return;          /* Esc handled outside */
    if (ascii == 8) {
        if (expr_len > 0) expr_len--;
        has_result = 0;
        return;
    }
    if (ascii == '\r' || ascii == '\n' || ascii == '=') {
        compute();
        return;
    }
    if (ascii == 'c' || ascii == 'C') {
        expr_len = 0;
        has_result = 0;
        return;
    }
    if (is_digit((char)ascii) ||
        ascii == '+' || ascii == '-' ||
        ascii == '*' || ascii == '/' ||
        ascii == '%' || ascii == '(' || ascii == ')' || ascii == '.') {
        if (expr_len < (int)sizeof(expr) - 1) expr[expr_len++] = (char)ascii;
        has_result = 0;
    }
}

int app_main(const char* args) {
    (void)args;
    int win = gui_open_window("CALC", 200, 240, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINCALC: cannot open window\n"); return 1; }

    int cw, ch;
    gui_size(win, &cw, &ch);
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    redraw_display(win);
    redraw_buttons(win);

    /* The window's outer rect on screen is (200, 240, WIN_W, WIN_H);
     * its content origin is the upper-left of the content area. We
     * computed those positions inside gui_open_window. To convert
     * mouse events (screen coords) to content-relative coords we
     * subtract the window outer (x, y) and the chrome offsets:
     * 1px border + 12px title bar = 13. */
    const int win_origin_x = 200 + 1;
    const int win_origin_y = 240 + 1 + 12;

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(10); continue; }
        if (ev.type == GUI_EV_PAINT) { redraw_display(win); redraw_buttons(win); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned ascii = ev.arg1 & 0xFF;
            if (ascii == 27) { gui_close_window(win); return 0; }
            apply_key(ascii);
            redraw_display(win);
        } else if (ev.type == GUI_EV_MOUSE_DOWN) {
            int rx = ev.x - win_origin_x;
            int ry = ev.y - win_origin_y;
            int r, c;
            if (hit_button(rx, ry, &r, &c)) {
                apply_label(btn_labels[r][c]);
                redraw_display(win);
                /* Briefly invert the clicked button for feedback. */
                int bx, by, bw, bh;
                btn_rect(r, c, &bx, &by, &bw, &bh);
                gui_fill_rect(win, bx, by, bw, bh, 0);
                gui_text(win, bx + bw / 2 - 4, by + bh / 2 - 4,
                         btn_labels[r][c], 15, 0);
                sleep_ms(80);
                redraw_buttons(win);
            }
        }
    }
}
