/* WINNOTE — single-buffer windowed notepad.
 *
 * Captures key events, appends to a 4 KiB buffer, and re-renders
 * the visible portion as 8x8 glyphs in the window. Backspace deletes
 * the last char, Enter inserts a newline, Esc closes. No save yet —
 * the M5 milestone is just to prove text input + rendering. */

#include "boxos_app.h"

#define BUF_CAP 4096

static char  text[BUF_CAP];
static int   text_len = 0;

static void redraw_text(int win, int cw, int ch) {
    /* Clear content area, leaving 14px footer for status. */
    int body_h = ch - 14;
    gui_fill_rect(win, 0, 0, cw, body_h, 7);

    int max_cols = (cw - 8) / 8;
    int max_rows = (body_h - 8) / 8;
    if (max_cols < 4 || max_rows < 1) return;

    /* Walk the buffer; wrap on \n and on column overflow. We also
     * keep only the last `max_rows` lines so growing text just
     * scrolls. Two passes: 1st measures # of rendered rows, 2nd
     * draws starting from `skip_rows`. */
    int rows_total = 1;
    int col = 0;
    for (int i = 0; i < text_len; i++) {
        char c = text[i];
        if (c == '\n')           { rows_total++; col = 0; }
        else if (col >= max_cols){ rows_total++; col = 1; }
        else                     { col++; }
    }
    int skip_rows = rows_total - max_rows;
    if (skip_rows < 0) skip_rows = 0;

    int row = 0;
    col = 0;
    for (int i = 0; i < text_len; i++) {
        char c = text[i];
        int newline = 0;
        if (c == '\n') {
            newline = 1;
        } else if (col >= max_cols) {
            newline = 1;
            /* Don't advance i — the char gets re-handled at col 0. */
            i--;
            c = '\0';
        }
        if (newline) {
            row++;
            col = 0;
            continue;
        }
        if (row >= skip_rows) {
            int x = 4 + col * 8;
            int y = 4 + (row - skip_rows) * 8;
            char s[2] = { c, 0 };
            gui_text(win, x, y, s, 0, 7);
        }
        col++;
    }

    /* Caret at end of last visible row. */
    int caret_col = col;
    int caret_row = (rows_total - 1) - skip_rows;
    if (caret_row >= 0 && caret_col <= max_cols) {
        int x = 4 + caret_col * 8;
        int y = 4 + caret_row * 8 + 7;
        gui_fill_rect(win, x, y, 8, 1, 0);   /* black underscore */
    }
}

static void redraw_status(int win, int cw, int ch) {
    int y = ch - 12;
    gui_fill_rect(win, 0, y, cw, 12, 8);
    char info[32];
    int n = 0;
    info[n++] = ' ';
    info[n++] = '0' + (text_len / 1000) % 10;
    info[n++] = '0' + (text_len / 100)  % 10;
    info[n++] = '0' + (text_len / 10)   % 10;
    info[n++] = '0' + (text_len)        % 10;
    const char* tail = " chars. Esc=close, Bksp=del, Enter=newline.";
    int t = 0;
    while (tail[t] && n < (int)sizeof(info) - 1) info[n++] = tail[t++];
    info[n] = 0;
    gui_text(win, 4, y + 2, info, 15, 8);
}

int app_main(const char* args) {
    (void)args;
    int win = gui_open_window("NOTEPAD", 64, 268, 528, 200);
    if (win < 0) { bos_puts("WINNOTE: gui_open_window failed\n"); return 1; }

    int cw, ch;
    gui_size(win, &cw, &ch);
    redraw_text(win, cw, ch);
    redraw_status(win, cw, ch);

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(10); continue; }
        if (ev.type == GUI_EV_PAINT) { redraw_text(win, cw, ch); redraw_status(win, cw, ch); continue; }

        if (ev.type != GUI_EV_KEY) continue;
        int      pressed = (ev.arg1 >> 8) & 1;
        unsigned ascii   = ev.arg1 & 0xFF;
        if (!pressed) continue;

        if (ascii == 27) {
            gui_close_window(win);
            return 0;
        } else if (ascii == 8) {                /* backspace */
            if (text_len > 0) text_len--;
        } else if (ascii == '\r' || ascii == '\n') {
            if (text_len < BUF_CAP - 1) text[text_len++] = '\n';
        } else if (ascii >= ' ' && ascii < 127) {
            if (text_len < BUF_CAP - 1) text[text_len++] = (char)ascii;
        } else {
            continue;
        }
        redraw_text(win, cw, ch);
        redraw_status(win, cw, ch);
    }
}
