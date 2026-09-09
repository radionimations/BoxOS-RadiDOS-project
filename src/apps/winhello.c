/* WINHELLO — smoke test for the M4 GUI syscalls.
 *
 * Opens a window in the desktop band, draws a few lines of text, and
 * loops on gui_poll_event(). Esc closes. Any other key updates a
 * counter so we can see input wired up; mouse clicks redraw a hit
 * marker at the click position. */

#include "boxos_app.h"

static void int_to_str(int n, char* out) {
    int i = 0, neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    char tmp[12]; int t = 0;
    if (n == 0) tmp[t++] = '0';
    while (n) { tmp[t++] = '0' + (n % 10); n /= 10; }
    if (neg) out[i++] = '-';
    while (t) out[i++] = tmp[--t];
    out[i] = 0;
}

static int g_win, g_w, g_h;
static int keys_seen   = 0;
static int clicks_seen = 0;

static void paint_full(void) {
    char b[16];
    gui_fill_rect(g_win, 0, 0, g_w, g_h, 7);
    gui_text(g_win, 8,  8, "Hello, BoxOS Arise!",          0, 7);
    gui_text(g_win, 8, 24, "Esc to close.",                0, 7);
    gui_text(g_win, 8, 40, "Click anywhere in this window.",0, 7);
    int_to_str(keys_seen, b);
    gui_text(g_win, 8, 60, "keys: ",   0, 7);
    gui_text(g_win, 8 + 6 * 8, 60, b,  0, 7);
    int_to_str(clicks_seen, b);
    gui_text(g_win, 8, 76, "clicks: ", 0, 7);
    gui_text(g_win, 8 + 8 * 8, 76, b,  0, 7);
}

int app_main(const char* args) {
    (void)args;

    int x = 160, y = 280;
    g_win = gui_open_window("WINHELLO", x, y, 320, 120);
    if (g_win < 0) {
        bos_puts("WINHELLO: gui_open_window failed (no fb?)\n");
        return 1;
    }
    gui_size(g_win, &g_w, &g_h);
    paint_full();

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(10); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY) {
            unsigned ascii   = ev.arg1 & 0xFF;
            int      pressed = (ev.arg1 >> 8) & 1;
            if (pressed) {
                if (ascii == 27) break;
                keys_seen++;
                paint_full();
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN) {
            clicks_seen++;
            paint_full();
            /* Hit marker. */
            int rx = ev.x - x, ry = ev.y - y - 12;
            gui_fill_rect(g_win, rx - 2, ry - 2, 4, 4, 4);
        }
    }

    gui_close_window(g_win);
    return 0;
}
