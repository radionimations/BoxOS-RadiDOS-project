/* WINLOGIN — boot-time login splash for BoxOS Arise.
 *
 * Auto-launched by the kernel when /SYS/INSTALL.CFG is present.
 * Reads the user name out of the config file, shows a centred
 * greeting card, and waits for a click or Enter to dismiss. After
 * that the desktop is bare and the user clicks Start to bring up
 * apps -- the v3.0 equivalent of Windows 95's first-time-after-
 * setup screen. (No password yet -- v3.1.) */

#include "boxos_app.h"

#define WIN_W 360
#define WIN_H 200
#define WIN_X ((640 - WIN_W) / 2)
#define WIN_Y ((480 - WIN_H) / 2 - 16)

static int win;
static int cw, ch;

static char user_name[40] = "User";

static void load_user_name(void) {
    static char buf[256];
    int n = (int)bos_read_file("INSTALL.CFG", buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = 0;
    /* Lines are KEY=VALUE\n. Pull the first NAME= line. */
    for (int i = 0; i < n; ) {
        int eol = i;
        while (eol < n && buf[eol] != '\n') eol++;
        if (buf[i] == 'N' && buf[i + 1] == 'A' && buf[i + 2] == 'M' &&
            buf[i + 3] == 'E' && buf[i + 4] == '=') {
            int s = i + 5, d = 0;
            while (s < eol && d < (int)sizeof(user_name) - 1)
                user_name[d++] = buf[s++];
            user_name[d] = 0;
            return;
        }
        i = eol + 1;
    }
}

static int strlen_l(const char* s) { int n=0; while(s&&s[n])n++; return n; }

static void paint_full(void) {
    /* Window backdrop. */
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    /* Brand strip — orange band echoing the splash logo. */
    gui_fill_rect(win, 0, 0, cw, 38, 214);
    gui_fill_rect(win, 0, 37, cw, 1, 94);
    gui_text(win, 16, 6,  "BoxOS Arise", 0, 214);
    gui_text(win, 16, 22, "Sign in to continue.", 0, 214);
    /* Greeting. */
    const char* hi = "Welcome,";
    int hx = (cw - strlen_l(hi) * 8) / 2;
    gui_text(win, hx, 64, hi, 0, 7);
    int ux = (cw - strlen_l(user_name) * 8) / 2;
    gui_text(win, ux, 84, user_name, 1, 7);
    /* Button. */
    int bw = 120, bh = 26;
    int bx = (cw - bw) / 2;
    int by = ch - bh - 20;
    gui_fill_rect(win, bx, by, bw, bh, 9);
    gui_fill_rect(win, bx, by, bw, 1, 15);
    gui_fill_rect(win, bx, by, 1, bh, 15);
    gui_fill_rect(win, bx, by + bh - 1, bw, 1, 0);
    gui_fill_rect(win, bx + bw - 1, by, 1, bh, 0);
    const char* label = "Log In";
    int lx = bx + (bw - strlen_l(label) * 8) / 2;
    gui_text(win, lx, by + 9, label, 0, 9);
    /* Hint. */
    const char* hint = "Click Log In or press Enter.";
    int tx = (cw - strlen_l(hint) * 8) / 2;
    gui_text(win, tx, ch - 12, hint, 8, 7);
}

int app_main(const char* args) {
    (void)args;
    load_user_name();
    win = gui_open_window("Sign in", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINLOGIN: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    paint_full();

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(20); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27 || a == '\n' || a == '\r') {
                gui_close_window(win);
                return 0;
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            /* Any click anywhere logs in — keeps interaction stupid
             * simple. */
            gui_close_window(win);
            return 0;
        }
    }
}
