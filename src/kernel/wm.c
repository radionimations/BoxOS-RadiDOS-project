/* BoxOS window manager.
 *
 * M3: a fixed layout with a top menu bar, a single bordered Terminal
 * window that wraps the existing 80x25 fbcon text grid, and a
 * desktop band below where M5+ apps will paint windows.
 *
 * The Terminal title/border are static — repaints during normal
 * shell scroll only touch the inside of the text grid, so we do not
 * need to redraw the chrome on every keystroke. */

#include "boxos.h"
#include "task.h"

/* Layout constants. y axis grows downward. */
#define MENU_H        0      /* Arise has no top menu bar — taskbar at bottom */
#define TASKBAR_H    24
#define START_W      72
#define WIN_TITLE_H  12
#define WIN_BORDER    1
#define COLS_8       80
#define ROWS_25      25

/* Computer name (set by Setup; also surfaces as the WM hostname). */
static char g_menu_title[40] = "BoxOS Arise";

void wm_set_title(const char* s) {
    int n = 0;
    while (s && s[n] && n < (int)sizeof(g_menu_title) - 1) {
        g_menu_title[n] = s[n];
        n++;
    }
    g_menu_title[n] = 0;
}

const char* wm_get_title(void) { return g_menu_title; }

/* Forward — drawn from wm_init and wm_paint_clock. */
static void paint_taskbar_static(int W, int H);

void wm_init(void) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    int W = bi->fb_width, H = bi->fb_height;
    const struct os_theme* T = theme_active();

    /* Desktop wallpaper. */
    fb_fill_rect(0, 0, W, H, T->desktop_bg);

    /* Win95-style taskbar pinned to the bottom. */
    paint_taskbar_static(W, H);
}

/* Static bits of the taskbar — bar, Start button, 1-px separator.
 * The clock is repainted independently each second by wm_paint_clock. */
static void paint_taskbar_static(int W, int H) {
    const struct os_theme* T = theme_active();
    int y = H - TASKBAR_H;
    fb_fill_rect(0, y, W, TASKBAR_H, T->menu_bg);
    /* 1-px highlight along the top of the taskbar for a Win95-y bevel. */
    fb_fill_rect(0, y, W, 1, 15);
    /* Start button (raised tile). */
    int bx = 4, by = y + 3, bw = START_W, bh = TASKBAR_H - 6;
    fb_fill_rect(bx, by, bw, bh, 7);
    fb_fill_rect(bx, by, bw, 1, 15);
    fb_fill_rect(bx, by, 1, bh, 15);
    fb_fill_rect(bx, by + bh - 1, bw, 1, 0);
    fb_fill_rect(bx + bw - 1, by, 1, bh, 0);
    /* Tiny BoxOS glyph (orange tile + S) to the left of the label. */
    fb_fill_rect(bx + 5, by + 4, 12, 10, 214);
    fb_fill_rect(bx + 9, by + 4, 4,  10, 178);
    gui_text(bx + 22, by + 5, "Start", 0, 7);
}

/* Re-stamp the taskbar after a fullscreen gfx app exits or after a
 * theme change. Public so desktop / setup code can call it. */
void wm_repaint_taskbar(void) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    paint_taskbar_static(bi->fb_width, bi->fb_height);
}

int  wm_taskbar_height(void) { return TASKBAR_H; }
int  wm_start_button_w(void) { return START_W; }
/* Hit-test the Start button rect (relative to the framebuffer). */
int  wm_hit_start_button(int x, int y) {
    if (!fb_present()) return 0;
    const struct boot_info* bi = fb_bootinfo();
    int ty = bi->fb_height - TASKBAR_H;
    return x >= 4 && x < 4 + START_W && y >= ty + 3 && y < ty + TASKBAR_H - 3;
}

/* Repaint just the right side of the menu bar (the clock area) so
 * desktop_loop can call it once a second without a full chrome
 * repaint. Shows "[HB:N]  HH:MM:SS" where N is the heartbeat counter
 * bumped by the cooperative scheduler's background task — visible
 * proof that yield() is actually rotating tasks. */
void wm_paint_clock(uint64_t uptime_s) {
    if (!fb_present()) return;
    if (gfx_in_app_mode()) return;
    const struct boot_info* bi = fb_bootinfo();
    const struct os_theme* T = theme_active();
    int W = bi->fb_width, H = bi->fb_height;
    int by = H - TASKBAR_H;

    int hh = (int)(uptime_s / 3600); if (hh > 99) hh = 99;
    int mm = (int)((uptime_s / 60) % 60);
    int ss = (int)(uptime_s % 60);
    char buf[16];
    int i = 0;
    buf[i++] = '0' + (char)((hh / 10) % 10);
    buf[i++] = '0' + (char)(hh % 10);
    buf[i++] = ':';
    buf[i++] = '0' + (char)((mm / 10) % 10);
    buf[i++] = '0' + (char)(mm % 10);
    buf[i++] = ':';
    buf[i++] = '0' + (char)((ss / 10) % 10);
    buf[i++] = '0' + (char)(ss % 10);
    buf[i] = 0;

    /* Sunken clock tile at the bottom-right of the taskbar. */
    int cw = 8 * 8 + 8;
    int cx = W - cw - 4;
    int cy = by + 3;
    int ch = TASKBAR_H - 6;
    fb_fill_rect(cx, cy, cw, ch, T->menu_bg);
    fb_fill_rect(cx, cy, cw, 1, 0);
    fb_fill_rect(cx, cy, 1, ch, 0);
    fb_fill_rect(cx, cy + ch - 1, cw, 1, 15);
    fb_fill_rect(cx + cw - 1, cy, 1, ch, 15);
    gui_text(cx + 4, cy + 4, buf, T->menu_fg, T->menu_bg);

    /* Heartbeat counter just left of the clock, small, low contrast. */
    extern volatile uint32_t g_task_heartbeat;
    uint32_t hb = g_task_heartbeat;
    char hbuf[16];
    int hi = 0;
    hbuf[hi++] = 'H'; hbuf[hi++] = 'B'; hbuf[hi++] = ':';
    if (hb == 0) hbuf[hi++] = '0';
    else {
        char rev[10]; int rn = 0;
        while (hb && rn < 9) { rev[rn++] = (char)('0' + (hb % 10)); hb /= 10; }
        while (rn--) hbuf[hi++] = rev[rn];
    }
    hbuf[hi] = 0;
    int hx = cx - hi * 8 - 8;
    if (hx < 4 + START_W + 8) hx = 4 + START_W + 8;
    fb_fill_rect(hx, by + 4, hi * 8, ch - 2, T->menu_bg);
    gui_text(hx, by + 8, hbuf, T->menu_fg, T->menu_bg);
}

/* Open a Terminal window that fills most of the desktop and run the
 * shell inside it. Returns when the shell exits via EXIT/HALT/cmd_quit.
 * fbcon's origin is moved into the content rect for the lifetime of
 * the window, then reset to (0, 0) on close. */
void wm_open_terminal(void) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    int W = bi->fb_width, H = bi->fb_height;

    /* Centred terminal: 80x25 cells of 8x16 = 640x400 content + chrome. */
    int term_w = COLS_8 * 8 + 2 * WIN_BORDER;
    int term_h = ROWS_25 * 16 + WIN_TITLE_H + 2 * WIN_BORDER;
    int term_x = (W - term_w) / 2;
    int term_y = (H - TASKBAR_H - term_h) / 2;
    if (term_x < 0) term_x = 0;
    if (term_y < 2) term_y = 2;

    int win = wm_open_window("Terminal", term_x, term_y, term_w, term_h);
    if (win < 0) return;

    /* Hide the OS cursor while the shell owns the screen — fbcon is
     * about to repaint over wherever the cursor was, and the saved
     * background would go stale. The desktop_loop re-shows it. */
    cursor_hide();

    /* Black content background (Terminal aesthetic). */
    int cx = term_x + WIN_BORDER;
    int cy = term_y + WIN_BORDER + WIN_TITLE_H;
    int cw = COLS_8 * 8;
    int ch = ROWS_25 * 16;
    fb_fill_rect(cx, cy, cw, ch, 0);

    /* Point fbcon at the content rect, clear the scrollback so the
     * pre-existing boot logs don't leak into the new terminal, and
     * paint a fresh banner. */
    fbcon_set_origin(cx, cy);
    fbcon_set_visible(true);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
    fbcon_repaint();

    shell_run();

    /* Shell exited (EXIT/HALT). Tear down the window and restore the
     * desktop. The shell only consumes from the ASCII ring; every key
     * the user typed also accumulated press+release events in the
     * separate event ring that gui_poll_event drains. Without this
     * drain, hundreds of stale key events flood the desktop after the
     * terminal closes and starve mouse clicks. */
    keyboard_drain();
    fbcon_set_visible(false);
    fbcon_set_origin(0, 0);
    wm_close_window(win);
}

/* Open a one-shot terminal window that either:
 *   - runs the .BIN at `path` (text-mode app whose stdout we want
 *     the user to see), or
 *   - dumps the contents of the .TXT at `path` (file viewer).
 * The window stays open until the user presses any key. */
void wm_open_terminal_with(const char* path) {
    if (!fb_present() || !path) return;
    const struct boot_info* bi = fb_bootinfo();
    int W = bi->fb_width, H = bi->fb_height;

    int term_w = COLS_8 * 8 + 2 * WIN_BORDER;
    int term_h = ROWS_25 * 16 + WIN_TITLE_H + 2 * WIN_BORDER;
    int term_x = (W - term_w) / 2;
    int term_y = (H - TASKBAR_H - term_h) / 2;
    if (term_x < 0) term_x = 0;
    if (term_y < 2) term_y = 2;

    /* Title showing what we're running. */
    char title[64];
    int n = 0;
    while (path[n] && n < 50) { title[n] = path[n]; n++; }
    title[n] = 0;
    int win = wm_open_window(title, term_x, term_y, term_w, term_h);
    if (win < 0) return;

    cursor_hide();
    int cx = term_x + WIN_BORDER;
    int cy = term_y + WIN_BORDER + WIN_TITLE_H;
    fb_fill_rect(cx, cy, COLS_8 * 8, ROWS_25 * 16, 0);

    fbcon_set_origin(cx, cy);
    fbcon_set_visible(true);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
    fbcon_repaint();

    /* Decide what to do based on the suffix. */
    int p_n = 0; while (path[p_n]) p_n++;
    int is_txt = (p_n >= 4 &&
                  (path[p_n - 4] == '.') &&
                  (path[p_n - 3] == 'T' || path[p_n - 3] == 't') &&
                  (path[p_n - 2] == 'X' || path[p_n - 2] == 'x') &&
                  (path[p_n - 1] == 'T' || path[p_n - 1] == 't'));
    if (is_txt) {
        /* Dump the file. Up to 8 KiB inline. */
        static char buf[8192];
        int got = fs_read_file(path, buf, sizeof(buf) - 1);
        if (got < 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_printf("Could not read %s\n", path);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        } else {
            buf[got] = 0;
            vga_puts(buf);
            if (got > 0 && buf[got - 1] != '\n') vga_putc('\n');
        }
    } else {
        /* Run the binary; stdout lands in the terminal window. */
        keyboard_drain();   /* don't bleed the launching click into stdin */
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_printf("[run] %s\n", path);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        int rc = loader_run(path, "");
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_printf("\n[exit %d]\n", rc);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("\n--- press any key to close ---");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    keyboard_drain();
    (void)keyboard_getc();

    /* Drop the closing keypress's release event (and anything the
     * runner left behind) so it doesn't bleed into the desktop. */
    keyboard_drain();
    fbcon_set_visible(false);
    fbcon_set_origin(0, 0);
    wm_close_window(win);
}


/* ---- dynamic windows ------------------------------------------- */

#define MAX_WINS 4

struct win {
    int  used;
    int  x, y, w, h;          /* outer rect on screen */
    int  cx, cy, cw, ch;      /* inner content rect   */
    char title[40];
};

static struct win g_wins[MAX_WINS];

/* Z-order: front-to-back. g_z[0] is the topmost window's handle,
 * g_z[g_z_count-1] is the bottom. Handles in g_wins[] are stable
 * slot indices; this list re-orders them for stacking. */
static int g_z[MAX_WINS];
static int g_z_count = 0;

static int z_index_of(int h) {
    for (int i = 0; i < g_z_count; i++) if (g_z[i] == h) return i;
    return -1;
}

static void z_push_top(int h) {
    /* Already in list? Move it to the front; otherwise insert. */
    int idx = z_index_of(h);
    if (idx < 0) {
        if (g_z_count < MAX_WINS) g_z_count++;
        idx = g_z_count - 1;
    }
    for (int i = idx; i > 0; i--) g_z[i] = g_z[i - 1];
    g_z[0] = h;
}

static void z_remove(int h) {
    int idx = z_index_of(h);
    if (idx < 0) return;
    for (int i = idx; i < g_z_count - 1; i++) g_z[i] = g_z[i + 1];
    g_z_count--;
}

/* Pull the named window to the top of the stack and repaint its
 * chrome so the raise is visible immediately. No content repaint —
 * the new top's owner will get an expose message in Phase 1.5. */
void wm_raise_window(int h);   /* forward */

/* ---- z-order clipping ------------------------------------------ */
/* Any paint into the framebuffer for a given window's handle has to
 * skip the parts of the rect that fall inside a window above it in
 * z-order — otherwise lower windows happily stomp on the chrome and
 * content of windows that should be on top. */

/* Subtract rect B from rect A, producing up to four non-overlapping
 * sub-rects of A that lie outside B. Returns how many were written
 * to out[][4]; max is the capacity. */
static int rect_subtract(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh,
                         int out[][4], int max) {
    int cx1 = ax > bx ? ax : bx;
    int cy1 = ay > by ? ay : by;
    int ax2 = ax + aw, ay2 = ay + ah;
    int bx2 = bx + bw, by2 = by + bh;
    int cx2 = ax2 < bx2 ? ax2 : bx2;
    int cy2 = ay2 < by2 ? ay2 : by2;
    if (cx2 <= cx1 || cy2 <= cy1) {
        if (max < 1) return 0;
        out[0][0] = ax; out[0][1] = ay; out[0][2] = aw; out[0][3] = ah;
        return 1;
    }
    int n = 0;
    if (cy1 > ay && n < max) {
        out[n][0] = ax; out[n][1] = ay; out[n][2] = aw; out[n][3] = cy1 - ay; n++;
    }
    if (cy2 < ay2 && n < max) {
        out[n][0] = ax; out[n][1] = cy2; out[n][2] = aw; out[n][3] = ay2 - cy2; n++;
    }
    if (cx1 > ax && n < max) {
        out[n][0] = ax; out[n][1] = cy1; out[n][2] = cx1 - ax; out[n][3] = cy2 - cy1; n++;
    }
    if (cx2 < ax2 && n < max) {
        out[n][0] = cx2; out[n][1] = cy1; out[n][2] = ax2 - cx2; out[n][3] = cy2 - cy1; n++;
    }
    return n;
}

/* True if two screen rectangles overlap. Forward decl since the
 * clipped paint helpers below use it; the body is further down. */
static int rect_overlap(int ax, int ay, int aw, int ah,
                        int bx, int by, int bw, int bh);

#define MAX_CLIP_RECTS 32

/* Fill (x,y,w,h) on the framebuffer with `color`, skipping any
 * pixels that fall inside a window above `handle` in z-order. Pass
 * handle = -1 to fill without z-order clipping. */
static void wm_fill_clipped(int handle, int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    int rects[MAX_CLIP_RECTS][4];
    rects[0][0] = x; rects[0][1] = y; rects[0][2] = w; rects[0][3] = h;
    int n = 1;
    int my_zi = handle >= 0 ? z_index_of(handle) : -1;
    if (my_zi > 0) {
        for (int zi = my_zi - 1; zi >= 0; zi--) {
            int other = g_z[zi];
            if (!g_wins[other].used) continue;
            struct win* O = &g_wins[other];
            int tmp[MAX_CLIP_RECTS][4];
            int tn = 0;
            for (int i = 0; i < n; i++) {
                tn += rect_subtract(rects[i][0], rects[i][1], rects[i][2], rects[i][3],
                                    O->x, O->y, O->w, O->h,
                                    &tmp[tn], MAX_CLIP_RECTS - tn);
                if (tn >= MAX_CLIP_RECTS) break;
            }
            for (int i = 0; i < tn; i++) {
                rects[i][0] = tmp[i][0]; rects[i][1] = tmp[i][1];
                rects[i][2] = tmp[i][2]; rects[i][3] = tmp[i][3];
            }
            n = tn;
            if (n == 0) return;
        }
    }
    for (int i = 0; i < n; i++) {
        fb_fill_rect(rects[i][0], rects[i][1], rects[i][2], rects[i][3], color);
    }
}

/* 1-pixel rectangle outline drawn with z-order clipping. */
static void wm_rect_clipped(int handle, int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    wm_fill_clipped(handle, x,         y,         w, 1, color);
    wm_fill_clipped(handle, x,         y + h - 1, w, 1, color);
    if (h > 2) {
        wm_fill_clipped(handle, x,         y + 1, 1, h - 2, color);
        wm_fill_clipped(handle, x + w - 1, y + 1, 1, h - 2, color);
    }
}

/* Is the 8x8 glyph at (gx, gy) covered by any window above `handle`
 * in z-order? Used to skip glyphs entirely rather than per-pixel —
 * good enough for the WM's needs and avoids touching gui_glyph. */
static int glyph_covered(int handle, int gx, int gy) {
    int my_zi = handle >= 0 ? z_index_of(handle) : -1;
    if (my_zi <= 0) return 0;
    for (int zi = my_zi - 1; zi >= 0; zi--) {
        int other = g_z[zi];
        if (!g_wins[other].used) continue;
        struct win* O = &g_wins[other];
        if (rect_overlap(gx, gy, 8, 8, O->x, O->y, O->w, O->h)) return 1;
    }
    return 0;
}

/* Text painted with z-order clipping at the glyph granularity. */
static void wm_text_clipped(int handle, int x, int y, const char* s,
                            uint8_t fg, uint8_t bg) {
    if (!s) return;
    while (*s) {
        if (!glyph_covered(handle, x, y)) gui_glyph(x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

/* Geometry of the close-button (X) tile inside the title bar. */
#define WIN_CLOSE_W 12
#define WIN_CLOSE_H WIN_TITLE_H

/* Paint just the chrome (frame + title bar + X) — does NOT touch
 * the content rect. Used both at window-open and during drag, so we
 * can restore the app's pixels in the content rect afterwards.
 *
 * Active-window highlight: the topmost window in z-order gets a
 * vivid blue title bar with white text; others get a muted grey
 * with dark text. Standard Win 3.1 behaviour. */
static void paint_chrome(struct win* W) {
    int h = (int)(W - g_wins);
    int active = (g_z_count > 0 && g_z[0] == h);
    uint8_t title_bg = active ? 1  : 8;       /* blue vs dark grey */
    uint8_t title_fg = active ? 15 : 7;       /* white vs light grey */
    wm_rect_clipped(h, W->x, W->y, W->w, W->h, 0);
    wm_fill_clipped(h, W->x + 1, W->y + 1, W->w - 2, WIN_TITLE_H, title_bg);
    wm_text_clipped(h, W->x + 4, W->y + 3, W->title, title_fg, title_bg);
    int bx = W->x + W->w - 2 - WIN_CLOSE_W;
    int by = W->y + 1;
    wm_fill_clipped(h, bx, by, WIN_CLOSE_W, WIN_CLOSE_H, 7);
    wm_rect_clipped(h, bx, by, WIN_CLOSE_W, WIN_CLOSE_H, 0);
    wm_text_clipped(h, bx + 2, by + 3, "X", 0, 7);
}

/* Initial content-area fill — only called on window OPEN (so apps see
 * a clean light-grey canvas to start drawing into). Drag does not
 * call this; it preserves the app's existing content. */
static void paint_chrome_open(struct win* W) {
    int h = (int)(W - g_wins);
    paint_chrome(W);
    wm_fill_clipped(h, W->cx, W->cy, W->cw, W->ch, 7);
}

/* Did (mx, my) land on a window's close button? Returns -1 or the
 * window handle. Walks the z-order from front to back so the
 * topmost window catches the click first. */
static int hit_close_button(int mx, int my) {
    for (int zi = 0; zi < g_z_count; zi++) {
        int i = g_z[zi];
        struct win* W = &g_wins[i];
        if (!W->used) continue;
        int bx = W->x + W->w - 2 - WIN_CLOSE_W;
        int by = W->y + 1;
        if (mx >= bx && mx < bx + WIN_CLOSE_W &&
            my >= by && my < by + WIN_CLOSE_H) return i;
    }
    return -1;
}

/* Hit-test: which window owns (mx, my)? Returns -1 if none. Used for
 * click-to-raise. Walks the z-order front-to-back. */
static int hit_window(int mx, int my) {
    for (int zi = 0; zi < g_z_count; zi++) {
        int i = g_z[zi];
        struct win* W = &g_wins[i];
        if (!W->used) continue;
        if (mx >= W->x && mx < W->x + W->w &&
            my >= W->y && my < W->y + W->h) return i;
    }
    return -1;
}

/* Public: the desktop / event loop calls this on every MOUSE_DOWN.
 * If the click landed on a window's X, we synthesise an Esc keypress
 * (every WIN app + DOOM/PLASMA quits on Esc) and tell the caller the
 * click was consumed. */
int wm_handle_close_click(int mx, int my) {
    if (hit_close_button(mx, my) < 0) return 0;
    keyboard_inject_key(27);   /* Esc */
    return 1;
}

/* ---- window drag ----------------------------------------------- */

/* Hit-test: is (mx, my) on a draggable title bar (not on its X)?
 * Walks the z-order front-to-back so the topmost title catches it. */
static int hit_title_drag(int mx, int my, int* dx_out, int* dy_out) {
    for (int zi = 0; zi < g_z_count; zi++) {
        int i = g_z[zi];
        struct win* W = &g_wins[i];
        if (!W->used) continue;
        int by = W->y + 1;
        int bx = W->x + W->w - 2 - WIN_CLOSE_W;
        /* Title bar = full width except the close-button tile. */
        if (my >= W->y && my < W->y + 1 + WIN_TITLE_H &&
            mx >= W->x && mx <  W->x + W->w &&
            !(mx >= bx && mx < bx + WIN_CLOSE_W &&
              my >= by && my < by + WIN_CLOSE_H)) {
            *dx_out = mx - W->x;
            *dy_out = my - W->y;
            return i;
        }
    }
    return -1;
}

/* Window dragging uses a Windows XP-style "outline drag": while the
 * mouse button is held, the actual window stays put and we draw a
 * 1-pixel wireframe rectangle at the proposed new position. On
 * button-up we erase the wireframe, move the window for real, and
 * post GUI_EV_PAINT to every window so they redraw their content
 * (the move may have exposed pixels nothing else knows how to
 * paint). This sidesteps the screenshot-and-restore approach, which
 * had no way to recover the pixels behind the dragged window. */
static void (*g_bg_repaint)(void) = 0;
void wm_set_bg_repaint(void (*fn)(void)) { g_bg_repaint = fn; }

/* Save-under buffers for the four thin strips that make up the
 * wireframe outline. Each strip is at most max(fb_width, fb_height)
 * pixels long * 1 pixel thick. 1024 covers the 640x480 framebuffer
 * with headroom. Lives in BSS. */
#define OUTLINE_MAX 1024
static uint8_t g_outline_top  [OUTLINE_MAX];
static uint8_t g_outline_bot  [OUTLINE_MAX];
static uint8_t g_outline_left [OUTLINE_MAX];
static uint8_t g_outline_right[OUTLINE_MAX];
static int g_outline_drawn = 0;
static int g_outline_x = 0, g_outline_y = 0;
static int g_outline_w = 0, g_outline_h = 0;

static void outline_erase(void) {
    if (!g_outline_drawn) return;
    int x = g_outline_x, y = g_outline_y;
    int w = g_outline_w, h = g_outline_h;
    if (w <= OUTLINE_MAX) {
        gui_restore_block(x, y,         w, 1, g_outline_top, w);
        gui_restore_block(x, y + h - 1, w, 1, g_outline_bot, w);
    }
    if (h > 2) {
        gui_restore_block(x,         y + 1, 1, h - 2, g_outline_left,  1);
        gui_restore_block(x + w - 1, y + 1, 1, h - 2, g_outline_right, 1);
    }
    g_outline_drawn = 0;
}

static void outline_draw(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || w > OUTLINE_MAX) return;
    /* Save the pixels under the four edges so we can put them back. */
    gui_save_block(x, y,         w, 1, g_outline_top, w);
    gui_save_block(x, y + h - 1, w, 1, g_outline_bot, w);
    if (h > 2) {
        gui_save_block(x,         y + 1, 1, h - 2, g_outline_left,  1);
        gui_save_block(x + w - 1, y + 1, 1, h - 2, g_outline_right, 1);
    }
    g_outline_x = x; g_outline_y = y;
    g_outline_w = w; g_outline_h = h;
    g_outline_drawn = 1;
    /* Black 1-pixel rectangle — same as window-frame colour, reads
     * well against the desktop wallpaper. */
    gui_rect(x, y, w, h, 0);
}

/* True if two screen rectangles overlap. */
static int rect_overlap(int ax, int ay, int aw, int ah,
                        int bx, int by, int bw, int bh) {
    return (ax < bx + bw) && (bx < ax + aw)
        && (ay < by + bh) && (by < ay + ah);
}

/* When the area at (x,y,w,h) just got "exposed" (a window above was
 * closed), tell every other window whose rect intersects to repaint
 * its content. We blanked the area to wallpaper already; the owner
 * is the only thing that knows how to put the right pixels back.
 * `exclude_handle` skips that handle (use -1 for "include all"). */
static void post_paint_to_exposed(int x, int y, int w, int h,
                                  int exclude_handle) {
    for (int zi = 0; zi < g_z_count; zi++) {
        int i = g_z[zi];
        if (i == exclude_handle || !g_wins[i].used) continue;
        struct win* O = &g_wins[i];
        if (!rect_overlap(x, y, w, h, O->x, O->y, O->w, O->h)) continue;
        task_id owner = task_lookup_owner(i);
        if (owner < 0) continue;
        struct gui_event ev = {0};
        ev.type = GUI_EV_PAINT;
        ev.arg1 = (uint32_t)i;
        task_post_event_to(owner, &ev);
    }
}

/* Buffer for the one-shot content blit performed on drag-end. Sized
 * for a full 640x480 framebuffer = 307 KiB in BSS; this is the
 * absolute upper bound on a window's content rect. */
#define DRAG_CONTENT_BYTES (640 * 480)
static uint8_t g_drag_content[DRAG_CONTENT_BYTES];

/* Commit a window move (called once on drag-end). The dragged window
 * is guaranteed z-top (wm_drag_begin raises it before we get here),
 * so its content pixels on screen are the real app content, not
 * something painted by a window above. We grab those pixels into a
 * BSS buffer, blank the old rect, repaint other windows' chrome that
 * lived inside it, stamp the moved window's chrome at the new
 * location, and blit the captured content into the new content rect.
 *
 * Doing the blit instead of asking the app to repaint means apps
 * that don't handle GUI_EV_PAINT (most of them) still keep their
 * details after a move — without it the content area would just be
 * the grey fill we put down, blanking out the calculator buttons,
 * paint canvas, clock face, etc. We still post GUI_EV_PAINT to the
 * OTHER windows so any of them that *do* handle it can redraw
 * content in regions we just blanked. */
static void window_commit_move(struct win* W, int new_x, int new_y) {
    int old_x  = W->x,  old_y  = W->y;
    int old_cx = W->cx, old_cy = W->cy;
    int dx = new_x - W->x;
    int dy = new_y - W->y;
    if (dx == 0 && dy == 0) return;
    int self_handle = (int)(W - g_wins);
    int cw = W->cw, ch = W->ch;

    cursor_hide();

    /* Capture old content if it fits in the BSS buffer. */
    int can_blit = (cw > 0 && ch > 0 &&
                    (uint32_t)cw * (uint32_t)ch <= DRAG_CONTENT_BYTES);
    if (can_blit) {
        gui_save_block(old_cx, old_cy, cw, ch, g_drag_content, cw);
    }

    W->x = new_x;
    W->y = new_y;
    W->cx += dx;
    W->cy += dy;

    fb_fill_rect(old_x, old_y, W->w, W->h, theme_active()->status_bg);

    for (int zi = g_z_count - 1; zi >= 0; zi--) {
        int i = g_z[zi];
        if (i == self_handle || !g_wins[i].used) continue;
        struct win* O = &g_wins[i];
        if (rect_overlap(old_x, old_y, W->w, W->h,
                         O->x,  O->y,  O->w, O->h)) {
            paint_chrome(O);
        }
    }
    paint_chrome(W);

    if (can_blit) {
        gui_restore_block(W->cx, W->cy, cw, ch, g_drag_content, cw);
    } else {
        wm_fill_clipped(self_handle, W->cx, W->cy, cw, ch, 7);
    }
    cursor_show();

    for (int zi = 0; zi < g_z_count; zi++) {
        int i = g_z[zi];
        if (i == self_handle || !g_wins[i].used) continue;
        task_id owner = task_lookup_owner(i);
        if (owner < 0) continue;
        struct gui_event ev = {0};
        ev.type = GUI_EV_PAINT;
        ev.arg1 = (uint32_t)i;
        task_post_event_to(owner, &ev);
    }
}

/* Public: returns the (x, y) of the window for an app to translate
 * mouse coordinates. */
int wm_window_origin(int handle, int* out_x, int* out_y) {
    if (handle < 0 || handle >= MAX_WINS || !g_wins[handle].used) return -1;
    if (out_x) *out_x = g_wins[handle].x;
    if (out_y) *out_y = g_wins[handle].y;
    return 0;
}

/* drag state — only one window can be dragged at a time. */
static int g_drag_win = -1;
static int g_drag_dx, g_drag_dy;

int wm_drag_begin(int mx, int my) {
    int dxo, dyo;
    int wi = hit_title_drag(mx, my, &dxo, &dyo);
    if (wi < 0) return 0;
    /* Raise the dragged window to z-top before drawing the outline.
     * The click-to-raise in wm_drain_hardware uses hit_window (a
     * full-rect test) while we used hit_title_drag (a title-bar
     * test); the two can disagree when the click lands on a window's
     * title bar that's at the same y as another window's content
     * area. Raising here guarantees the dragged window is z-top, so
     * the z-order clipping in wm_fill_clipped doesn't strip parts of
     * its own content during the post-drop repaint. */
    if (g_z_count > 0 && g_z[0] != wi) {
        wm_raise_window(wi);
    }
    g_drag_win = wi;
    g_drag_dx = dxo;
    g_drag_dy = dyo;
    /* Draw the initial wireframe over the window's current position.
     * The window itself stays put — only the outline tracks the
     * cursor — so the OS never has to recover pixels behind the
     * window during the drag. */
    struct win* W = &g_wins[wi];
    cursor_hide();
    outline_draw(W->x, W->y, W->w, W->h);
    cursor_show();
    return 1;
}

int wm_drag_active(void)  { return g_drag_win >= 0; }

void wm_drag_end(void) {
    int self_handle = g_drag_win;
    g_drag_win = -1;
    if (self_handle < 0) return;
    struct win* W = &g_wins[self_handle];
    /* Read the wireframe's final position, then erase it. */
    int new_x = g_outline_drawn ? g_outline_x : W->x;
    int new_y = g_outline_drawn ? g_outline_y : W->y;
    cursor_hide();
    outline_erase();
    cursor_show();
    /* Commit the move (no-op if the wireframe never left the
     * original position). */
    window_commit_move(W, new_x, new_y);
}

void wm_drag_to(int mx, int my) {
    if (g_drag_win < 0) return;
    struct win* W = &g_wins[g_drag_win];
    int nx = mx - g_drag_dx;
    int ny = my - g_drag_dy;
    /* Keep the window on-screen and below the menu bar. */
    if (ny < MENU_H) ny = MENU_H;
    if (nx < 0) nx = 0;
    const struct boot_info* bi = fb_bootinfo();
    /* Keep windows above the bottom taskbar so it stays visible. */
    int max_y = bi->fb_height - TASKBAR_H;
    if (nx + W->w > bi->fb_width) nx = bi->fb_width - W->w;
    if (ny + W->h > max_y)        ny = max_y - W->h;
    if (g_outline_drawn && nx == g_outline_x && ny == g_outline_y) return;
    cursor_hide();
    outline_erase();
    outline_draw(nx, ny, W->w, W->h);
    cursor_show();
}

int wm_open_window(const char* title, int x, int y, int w, int h) {
    if (w < 32 || h < (WIN_TITLE_H + 8)) return -1;
    for (int i = 0; i < MAX_WINS; i++) {
        if (g_wins[i].used) continue;
        struct win* W = &g_wins[i];
        W->used = 1;
        W->x = x; W->y = y; W->w = w; W->h = h;
        W->cx = x + WIN_BORDER;
        W->cy = y + WIN_BORDER + WIN_TITLE_H;
        W->cw = w - 2 * WIN_BORDER;
        W->ch = h - 2 * WIN_BORDER - WIN_TITLE_H;
        int n = 0;
        while (title && title[n] && n < (int)sizeof(W->title) - 1) {
            W->title[n] = title[n]; n++;
        }
        W->title[n] = 0;
        z_push_top(i);
        /* Tag this window with the current task as its owner so the
         * input router can deliver events to the right queue once
         * Phase 2 has apps running on their own tasks. */
        task_register_window(task_current_id(), i);
        /* Hide the cursor across the chrome paint so its save-under
         * buffer doesn't go stale and start ghost-stamping on the
         * next mouse move. */
        cursor_hide();
        paint_chrome_open(W);
        cursor_show();
        return i;
    }
    return -1;
}

void wm_close_window(int h) {
    if (h < 0 || h >= MAX_WINS || !g_wins[h].used) return;
    struct win* W = &g_wins[h];
    int x = W->x, y = W->y, w = W->w, ww_h = W->h;
    /* Hide cursor across the area-clearing paint below — same
     * reason as open: keep cursor save-under in sync. */
    cursor_hide();
    /* Fill the closed window's area with the desktop wallpaper
     * colour (not hard-coded 8 — that left a dark grey stamp on
     * top of the theme's wallpaper). */
    fb_fill_rect(x, y, w, ww_h, theme_active()->status_bg);
    W->used = 0;
    z_remove(h);
    task_unregister_window(h);
    /* Re-stamp chrome of any other window that overlapped — we
     * just blanked over its frame. */
    for (int zi = g_z_count - 1; zi >= 0; zi--) {
        int i = g_z[zi];
        if (!g_wins[i].used) continue;
        struct win* O = &g_wins[i];
        if (rect_overlap(x, y, w, ww_h, O->x, O->y, O->w, O->h)) {
            paint_chrome(O);
        }
    }
    cursor_show();
    /* Ask each exposed window's owner to re-render its content. */
    post_paint_to_exposed(x, y, w, ww_h, -1);
}

void wm_raise_window(int h) {
    if (h < 0 || h >= MAX_WINS || !g_wins[h].used) return;
    if (g_z_count > 0 && g_z[0] == h) return;   /* already on top */
    int prev_top = (g_z_count > 0) ? g_z[0] : -1;
    z_push_top(h);
    cursor_hide();
    /* Re-paint the previously-active window's chrome so its title bar
     * flips back to the inactive grey. */
    if (prev_top >= 0 && prev_top != h && g_wins[prev_top].used) {
        paint_chrome(&g_wins[prev_top]);
    }
    /* Repaint the raised window's chrome on top of everything else. */
    paint_chrome(&g_wins[h]);
    cursor_show();

    /* The raised window's CONTENT area in the framebuffer is still
     * whatever was painted there before — possibly pixels from a
     * window that used to be on top. Tell the owner to repaint and
     * yield enough times that its main loop has a turn before the
     * caller (typically wm_drag_begin) captures the content rect. */
    task_id owner = task_lookup_owner(h);
    if (owner >= 0 && owner != task_current_id()) {
        struct gui_event ev = {0};
        ev.type = GUI_EV_PAINT;
        ev.arg1 = (uint32_t)h;
        task_post_event_to(owner, &ev);
        for (int i = 0; i < 8; i++) yield();
    }
}

static int win_clip(struct win* W, int* x, int* y, int* w, int* h) {
    int sx = W->cx + *x;
    int sy = W->cy + *y;
    int sx1 = sx + *w, sy1 = sy + *h;
    int cx1 = W->cx + W->cw, cy1 = W->cy + W->ch;
    if (sx  < W->cx) sx  = W->cx;
    if (sy  < W->cy) sy  = W->cy;
    if (sx1 > cx1)   sx1 = cx1;
    if (sy1 > cy1)   sy1 = cy1;
    if (sx1 <= sx || sy1 <= sy) return 0;
    *x = sx; *y = sy;
    *w = sx1 - sx;
    *h = sy1 - sy;
    return 1;
}

void wm_w_fill_rect(int handle, int x, int y, int w, int h, uint8_t color) {
    if (handle < 0 || handle >= MAX_WINS || !g_wins[handle].used) return;
    struct win* W = &g_wins[handle];
    if (!win_clip(W, &x, &y, &w, &h)) return;
    wm_fill_clipped(handle, x, y, w, h, color);
}

void wm_w_text(int handle, int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    if (handle < 0 || handle >= MAX_WINS || !g_wins[handle].used) return;
    if (!s) return;
    struct win* W = &g_wins[handle];
    int sx = W->cx + x, sy = W->cy + y;
    while (*s) {
        if (sx + 8 > W->cx + W->cw || sy + 8 > W->cy + W->ch) break;
        if (sx >= W->cx && sy >= W->cy && !glyph_covered(handle, sx, sy)) {
            gui_glyph(sx, sy, *s, fg, bg);
        }
        sx += 8;
        s++;
    }
}

void wm_w_size(int handle, int* out_w, int* out_h) {
    if (handle < 0 || handle >= MAX_WINS || !g_wins[handle].used) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    struct win* W = &g_wins[handle];
    if (out_w) *out_w = W->cw;
    if (out_h) *out_h = W->ch;
}

/* ---- event polling --------------------------------------------- */

static int      g_last_x = -1;
static int      g_last_y = -1;
static uint32_t g_last_btns = 0;

/* "Focused" task = owner of the topmost window. Fallback to task 0
 * (the kernel) when no window is on the stack. */
static task_id focused_task(void) {
    if (g_z_count > 0) {
        task_id o = task_lookup_owner(g_z[0]);
        if (o >= 0) return o;
    }
    return 0;
}

/* Find the task that owns the window under (mx, my); if no window
 * is hit, the event belongs to the kernel task (the desktop). */
static task_id task_for_point(int mx, int my) {
    int h = hit_window(mx, my);
    if (h >= 0) {
        task_id o = task_lookup_owner(h);
        if (o >= 0) return o;
    }
    return 0;
}

/* Drain pending hardware events and post them to the appropriate
 * task queues. The actual gui_poll_event then just pops one from
 * the current task's queue. Splitting the work this way is what
 * lets multiple tasks (= multiple apps) each get their own input
 * stream without trampling on each other. */
static void wm_drain_hardware(void) {
    /* 1. Keyboard transitions → focused task. */
    uint16_t k = keyboard_try_get_event();
    if (k) {
        struct gui_event ev = {0};
        ev.type = GUI_EV_KEY;
        ev.arg1 = k;
        ev.x = cursor_x();
        ev.y = cursor_y();
        task_post_event_to(focused_task(), &ev);
    }

    /* 2. Cursor moved? */
    int cx = cursor_x();
    int cy = cursor_y();
    if (g_last_x < 0) { g_last_x = cx; g_last_y = cy; }
    if (cx != g_last_x || cy != g_last_y) {
        g_last_x = cx;
        g_last_y = cy;
        if (wm_drag_active()) {
            wm_drag_to(cx, cy);
        } else {
            struct gui_event ev = {0};
            ev.type = GUI_EV_MOUSE_MOVE;
            ev.arg1 = g_last_btns;
            ev.x = cx;
            ev.y = cy;
            task_post_event_to(task_for_point(cx, cy), &ev);
        }
    }

    /* 3. Button transitions. */
    uint32_t btns = mouse_buttons();
    if (btns != g_last_btns) {
        uint32_t down = btns & ~g_last_btns;
        uint32_t up   = ~btns &  g_last_btns;
        g_last_btns = btns;
        if (down) {
            /* Click-to-raise first, so close/drag/event run against
             * the now-topmost window. */
            if ((down & 1)) {
                int hw = hit_window(cx, cy);
                if (hw >= 0 && g_z_count > 0 && g_z[0] != hw) {
                    wm_raise_window(hw);
                }
            }
            /* Title-bar X → inject Esc into the focused task. */
            if ((down & 1) && hit_close_button(cx, cy) >= 0) {
                keyboard_inject_key(27);
                uint16_t kev = keyboard_try_get_event();
                if (kev) {
                    struct gui_event ev = {0};
                    ev.type = GUI_EV_KEY;
                    ev.arg1 = kev;
                    ev.x = cx;
                    ev.y = cy;
                    task_post_event_to(focused_task(), &ev);
                }
                return;
            }
            /* Title-bar drag — swallow the click entirely. */
            if ((down & 1) && wm_drag_begin(cx, cy)) {
                return;
            }
            /* Regular mouse-down event → window owner. */
            struct gui_event ev = {0};
            ev.type = GUI_EV_MOUSE_DOWN;
            ev.arg1 = down;
            ev.arg2 = btns;
            ev.x = cx;
            ev.y = cy;
            task_post_event_to(task_for_point(cx, cy), &ev);
            return;
        }
        if (up) {
            if (wm_drag_active()) {
                wm_drag_end();
                return;
            }
            struct gui_event ev = {0};
            ev.type = GUI_EV_MOUSE_UP;
            ev.arg1 = up;
            ev.arg2 = btns;
            ev.x = cx;
            ev.y = cy;
            task_post_event_to(task_for_point(cx, cy), &ev);
        }
    }
}

int gui_poll_event(struct gui_event* out) {
    if (!out) return 0;
    /* Suspend the whole event loop while a fullscreen gfx app owns
     * the screen — drain_hardware would forward WM_PAINT events to
     * other apps and they'd happily paint right into DOOM's
     * framebuffer. Apps just sleep_ms in their main loops until we
     * come back. */
    if (gfx_in_app_mode()) {
        out->type = GUI_EV_NONE;
        return 0;
    }
    wm_drain_hardware();
    if (task_get_event(out)) return 1;
    out->type = GUI_EV_NONE;
    return 0;
}

/* Full repaint of everything the WM owns. Used after a fullscreen
 * gfx app (DOOM/PLASMA) exits — the framebuffer is whatever pixels
 * it left, so wallpaper + every window's chrome must be redrawn,
 * and each app gets a WM_PAINT so its content comes back too. */
void wm_repaint_all(void) {
    if (!fb_present()) return;
    wm_init();   /* paints wallpaper + taskbar */
    cursor_hide();
    for (int zi = g_z_count - 1; zi >= 0; zi--) {
        int i = g_z[zi];
        if (g_wins[i].used) paint_chrome(&g_wins[i]);
    }
    cursor_show();
    for (int zi = 0; zi < g_z_count; zi++) {
        int i = g_z[zi];
        if (!g_wins[i].used) continue;
        task_id owner = task_lookup_owner(i);
        if (owner < 0) continue;
        struct gui_event ev = {0};
        ev.type = GUI_EV_PAINT;
        ev.arg1 = (uint32_t)i;
        task_post_event_to(owner, &ev);
    }
}
