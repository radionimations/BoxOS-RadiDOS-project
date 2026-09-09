/* WINCLOCK — a small uptime clock window.
 *
 * No RTC on the platform yet, so we show milliseconds-since-boot
 * formatted as HH:MM:SS. Polls events at 100Hz; Esc closes. */

#include "boxos_app.h"

static void format2(char* dst, unsigned v) {
    dst[0] = '0' + (v / 10) % 10;
    dst[1] = '0' + v % 10;
}

static void format_uptime(char* out, uint64_t ms) {
    uint64_t s  = ms / 1000;
    unsigned hh = (unsigned)(s / 3600);
    unsigned mm = (unsigned)((s / 60) % 60);
    unsigned ss = (unsigned)(s % 60);
    if (hh > 99) hh = 99;
    format2(out + 0, hh); out[2] = ':';
    format2(out + 3, mm); out[5] = ':';
    format2(out + 6, ss); out[8] = 0;
}

static int g_win, g_w, g_h;

static void paint_full(void) {
    char buf[12];
    format_uptime(buf, ticks_ms());
    gui_fill_rect(g_win, 0, 0, g_w, g_h, 7);
    gui_text(g_win, 16, 8,  "BoxOS uptime:", 0, 7);
    gui_text(g_win, 16, 28, buf,             4, 7);
    gui_text(g_win, 16, 56, "Esc to close.", 0, 7);
}

int app_main(const char* args) {
    (void)args;
    g_win = gui_open_window("CLOCK", 200, 280, 240, 96);
    if (g_win < 0) {
        bos_puts("WINCLOCK: cannot open window (no fb?)\n");
        return 1;
    }
    gui_size(g_win, &g_w, &g_h);
    paint_full();

    uint64_t last_shown = ticks_ms();
    char buf[12];
    for (;;) {
        struct gui_event ev;
        while (gui_poll_event(&ev)) {
            if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }
            if (ev.type == GUI_EV_KEY && (ev.arg1 >> 8) & 1) {
                unsigned ascii = ev.arg1 & 0xFF;
                if (ascii == 27) { gui_close_window(g_win); return 0; }
            }
        }
        uint64_t now = ticks_ms();
        if (now / 1000 != last_shown / 1000) {
            last_shown = now;
            format_uptime(buf, now);
            gui_fill_rect(g_win, 16, 24, 200, 16, 7);
            gui_text(g_win, 16, 28, buf, 4, 7);
        }
        sleep_ms(50);
    }
}
