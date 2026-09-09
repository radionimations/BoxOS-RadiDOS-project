/* WINFILES — File Manager for BoxOS 2.0.
 *
 * A windowed take on the v1.0 Executive: scrollable file list with
 * coloured row icons, current path in the toolbar, and full
 * directory navigation. Click a row to select, double-click (or
 * Enter) to descend into a folder, Backspace (or click "..") to go
 * back up, Esc to close. */

#include "boxos_app.h"

#define WIN_W   560
#define WIN_H   400
#define WIN_X   ((640 - WIN_W) / 2)
#define WIN_Y   30

#define TOOLBAR_H   18
#define HEADER_H    14
#define FOOTER_H    14
#define ROW_H       14
#define ROW_TOP     (TOOLBAR_H + HEADER_H)
#define MAX_ENTRIES 128
#define CWD_DEPTH   16

static int  win;
static int  cw, ch;
static int  win_x = WIN_X, win_y = WIN_Y;

static struct dir_entry entries[MAX_ENTRIES];
static int n_entries = 0;
static int sel = -1;
static int scroll_top = 0;

struct cwd_frame {
    uint16_t cluster;
    char     name[32];
};
static struct cwd_frame cwd_stack[CWD_DEPTH];
static int cwd_depth = 0;     /* 0 == root */

static uint64_t last_click_ms  = 0;
static int      last_click_idx = -1;

/* ---- helpers --------------------------------------------------- */

static int strlen_l(const char* s) { int n = 0; while (s && s[n]) n++; return n; }

static void int_to_str(uint64_t n, char* out) {
    char tmp[24]; int t = 0;
    if (n == 0) tmp[t++] = '0';
    while (n) { tmp[t++] = '0' + (n % 10); n /= 10; }
    int o = 0;
    while (t) out[o++] = tmp[--t];
    out[o] = 0;
}

static uint16_t cwd_cluster(void) {
    return cwd_depth == 0 ? 0 : cwd_stack[cwd_depth - 1].cluster;
}

static void cwd_path(char* out, int cap) {
    int o = 0;
    if (cap < 4) { out[0] = 0; return; }
    out[o++] = 'C'; out[o++] = ':'; out[o++] = '\\';
    for (int i = 0; i < cwd_depth && o < cap - 1; i++) {
        const char* n = cwd_stack[i].name;
        for (int j = 0; n[j] && o < cap - 1; j++) out[o++] = n[j];
        if (i < cwd_depth - 1 && o < cap - 1) out[o++] = '\\';
    }
    out[o] = 0;
}

static void load_dir(void) {
    n_entries = bos_list_dir(cwd_cluster(), entries, MAX_ENTRIES);
    if (n_entries < 0) n_entries = 0;
    sel = -1;
    scroll_top = 0;
    last_click_idx = -1;
}

static int rows_visible(void) {
    int n = (ch - ROW_TOP - FOOTER_H) / ROW_H;
    return n < 0 ? 0 : n;
}

static int ends_with(const char* s, const char* suffix) {
    int sl = strlen_l(s), su = strlen_l(suffix);
    if (su > sl) return 0;
    const char* tail = s + sl - su;
    for (int i = 0; i < su; i++) {
        char a = tail[i], b = suffix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return 1;
}

static char icon_for(const struct dir_entry* e) {
    if (e->attr & 0x10) return 'D';
    if (ends_with(e->name, ".BIN")) return 'A';
    if (ends_with(e->name, ".TXT")) return 'T';
    return 'F';
}

static uint8_t icon_bg_for(char ic) {
    switch (ic) {
        case 'D': return 14;    /* yellow */
        case 'A': return 2;     /* green  */
        case 'T': return 11;    /* cyan   */
        default:  return 8;     /* grey   */
    }
}

static const char* type_str(const struct dir_entry* e) {
    if (e->attr & 0x10) return "DIR";
    char ic = icon_for(e);
    return ic == 'A' ? "APP" : ic == 'T' ? "TEXT" : "FILE";
}

/* ---- painting -------------------------------------------------- */

static void paint_chrome(void) {
    /* Toolbar with C: badge + path. */
    gui_fill_rect(win, 0, 0, cw, TOOLBAR_H, 7);
    gui_fill_rect(win, 4, 2, 20, 14, 1);
    gui_text(win, 7, 5, "C:", 15, 1);
    char path[80];
    cwd_path(path, sizeof(path));
    gui_text(win, 30, 5, path, 0, 7);
    /* Column headers. */
    int hy = TOOLBAR_H;
    gui_fill_rect(win, 0, hy, cw, HEADER_H, 8);
    gui_text(win, 4 + 22,             hy + 3, "NAME", 15, 8);
    gui_text(win, 4 + 22 + 14 * 8,    hy + 3, "TYPE", 15, 8);
    gui_text(win, cw - 12 * 8,        hy + 3, "SIZE", 15, 8);
}

static void paint_row(int idx, int y, int selected) {
    if (idx < 0 || idx >= n_entries) {
        gui_fill_rect(win, 0, y, cw, ROW_H, 7);
        return;
    }
    struct dir_entry* e = &entries[idx];
    uint8_t bg = selected ? 9 : 7;
    uint8_t fg = selected ? 15 : 0;
    gui_fill_rect(win, 0, y, cw, ROW_H, bg);

    char ic = icon_for(e);
    char ic_s[2] = { ic, 0 };
    uint8_t ib = icon_bg_for(ic);
    gui_fill_rect(win, 4, y + 1, 14, ROW_H - 2, ib);
    gui_text(win, 7, y + 3, ic_s, 0, ib);

    gui_text(win, 4 + 22,          y + 3, e->name,      fg, bg);
    gui_text(win, 4 + 22 + 14 * 8, y + 3, type_str(e),  fg, bg);

    char sbuf[16];
    if (e->attr & 0x10) {
        const char* d = "<DIR>";
        int n = 0; while (d[n]) { sbuf[n] = d[n]; n++; } sbuf[n] = 0;
    } else {
        int_to_str(e->size, sbuf);
    }
    int sl = strlen_l(sbuf);
    int size_x = cw - 4 - sl * 8;
    gui_text(win, size_x, y + 3, sbuf, fg, bg);
}

static void paint_list(void) {
    int rv = rows_visible();
    for (int i = 0; i < rv; i++) {
        int idx = scroll_top + i;
        int y   = ROW_TOP + i * ROW_H;
        paint_row(idx, y, idx == sel);
    }
}

static void paint_status(void) {
    int fy = ch - FOOTER_H;
    gui_fill_rect(win, 0, fy, cw, FOOTER_H, 8);
    char msg[80];
    int o = 0;
    if (sel >= 0 && sel < n_entries) {
        const char* nm = entries[sel].name;
        for (int i = 0; nm[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = nm[i];
        const char* sep = "  -  ";
        for (int i = 0; sep[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = sep[i];
        if (entries[sel].attr & 0x10) {
            const char* d = "directory";
            for (int i = 0; d[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = d[i];
        } else {
            char num[16]; int_to_str(entries[sel].size, num);
            for (int i = 0; num[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = num[i];
            const char* tail = " bytes";
            for (int i = 0; tail[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = tail[i];
        }
    } else {
        const char* h = "DBL=open  BKSP=up  Esc=close";
        for (int i = 0; h[i] && o < (int)sizeof(msg) - 1; i++) msg[o++] = h[i];
    }
    msg[o] = 0;
    gui_text(win, 4, fy + 3, msg, 15, 8);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_chrome();
    paint_list();
    paint_status();
}

/* ---- navigation ----------------------------------------------- */

static int hit_row(int rx, int ry) {
    if (ry < ROW_TOP || ry >= ch - FOOTER_H) return -1;
    int row = (ry - ROW_TOP) / ROW_H;
    int idx = scroll_top + row;
    if (idx < 0 || idx >= n_entries) return -1;
    return idx;
}

static void go_up(void) {
    if (cwd_depth > 0) cwd_depth--;
    load_dir();
    paint_full();
}

static void enter_dir(struct dir_entry* e) {
    if (cwd_depth >= CWD_DEPTH) return;
    cwd_stack[cwd_depth].cluster = e->first_cluster;
    int n = 0;
    while (e->name[n] && n < 31) {
        cwd_stack[cwd_depth].name[n] = e->name[n];
        n++;
    }
    cwd_stack[cwd_depth].name[n] = 0;
    cwd_depth++;
    load_dir();
    paint_full();
}

static void open_entry(int idx) {
    if (idx < 0 || idx >= n_entries) return;
    struct dir_entry* e = &entries[idx];
    if (e->attr & 0x10) {
        if (e->name[0] == '.' && e->name[1] == 0) return;
        if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == 0) {
            go_up();
            return;
        }
        enter_dir(e);
        return;
    }
    /* Executables: launch directly. bos_launch_app searches /APPS
     * and /GAMES for the binary so a bare 8.3 name works regardless
     * of which folder WinFiles is currently viewing. */
    if (ends_with(e->name, ".BIN")) {
        (void)bos_launch_app(e->name, "");
        return;
    }
    /* Image files: open in WinImg, with its cwd pinned to whatever
     * folder WinFiles is currently viewing (so its bos_read_file of
     * the bare 8.3 name finds the file). */
    if (ends_with(e->name, ".BMP") || ends_with(e->name, ".JPG") ||
        ends_with(e->name, ".JPEG") || ends_with(e->name, ".ANI")) {
        (void)bos_launch_app_at("WINIMG.BIN", e->name, cwd_cluster());
        return;
    }
    /* Anything else: a soft beep so the click is acknowledged. */
    sound_beep(440, 25);
}

static void scroll_into_view(void) {
    int rv = rows_visible();
    if (sel < scroll_top) scroll_top = sel;
    if (sel >= scroll_top + rv) scroll_top = sel - rv + 1;
    if (scroll_top < 0) scroll_top = 0;
}

/* ---- main ----------------------------------------------------- */

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Files", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINFILES: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    load_dir();
    paint_full();

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(15); continue; }

        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == 8) { go_up(); continue; }                /* BkSp */
            if (a == 0x80) {                                  /* Up   */
                if (sel <= 0) sel = 0;
                else { sel--; scroll_into_view(); }
                paint_list(); paint_status();
                continue;
            }
            if (a == 0x81) {                                  /* Down */
                if (sel + 1 < n_entries) {
                    sel++; scroll_into_view();
                    paint_list(); paint_status();
                }
                continue;
            }
            if (a == '\n' || a == '\r') {
                if (sel >= 0) open_entry(sel);
                continue;
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            int idx = hit_row(rx, ry);
            if (idx >= 0) {
                uint64_t now = ticks_ms();
                if (idx == last_click_idx && now - last_click_ms < 500) {
                    open_entry(idx);
                    last_click_idx = -1;
                } else {
                    sel = idx;
                    last_click_idx = idx;
                    last_click_ms = now;
                    paint_list();
                    paint_status();
                }
            }
        }
    }
}
