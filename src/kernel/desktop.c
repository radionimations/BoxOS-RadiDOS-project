/* BoxOS 2.0 splash + Executive (Windows 1.0 MS-DOS Executive style).
 *
 * splash_show()  -- big "BoxOS 2.0" banner over a blue background.
 *                   Holds up to 2s, dismisses early on click or key.
 *
 * desktop_loop() -- the OS shell: a file-explorer window covering the
 *                   whole desktop. Single-click selects, double-click
 *                   launches (binaries) / navigates (dirs) / views
 *                   (text). Title bar shows "BoxOS Executive — <path>".
 *                   Menu bar has File / View / Special (File→About). */

#include "boxos.h"
#include "task.h"

extern const unsigned char font_8x8[96][8];

int loader_run(const char* name, const char* args);

/* --- big-text helper for splash + About ------------------------- */

static void glyph_scaled(int x, int y, char ch, int scale,
                         uint8_t fg, uint8_t bg) {
    int gi = (unsigned char)ch - 32;
    if (gi < 0 || gi >= 96) gi = 0;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = font_8x8[gi][row];
        for (int col = 0; col < 8; col++) {
            int lit = bits & (0x80 >> col);
            int x0 = x + col * scale;
            int y0 = y + row * scale;
            if (lit) gui_fill_rect(x0, y0, scale, scale, fg);
            else if (bg != 0xFF) gui_fill_rect(x0, y0, scale, scale, bg);
        }
    }
}

static void big_text(int x, int y, const char* s, int scale,
                     uint8_t fg, uint8_t bg) {
    int cx = x;
    while (*s) {
        glyph_scaled(cx, y, *s, scale, fg, bg);
        cx += 8 * scale;
        s++;
    }
}

/* --- BoxOS 2.0 brand mark --------------------------------------- *
 * Hand-drawn approximation of BoxOS2logo.PNG. 280x140 px rectangle
 * with the orange-on-brown body, the box-with-pillar mark, the
 * "BoxOS / 2.0" wordmark, and the RadiDOS subtitle. The palette
 * indices below assume fb_reload_palette()'s 6x6x6 RGB cube: 214 is
 * pure orange, 178 darker orange, 94 dark-brown frame, 221 amber
 * highlight (see fb.c). */

/* Palette indices for the brand colours. */
#define LOGO_FRAME    94   /* dark brown */
#define LOGO_BODY    214   /* orange */
#define LOGO_HIGH    221   /* amber highlight */
#define LOGO_COL     178   /* darker orange (the box columns) */

void paint_boxos_logo(int x, int y) {
    /* Outer black outline + brown frame + orange body. */
    fb_fill_rect(x,     y,     280, 140, 0);
    fb_fill_rect(x + 1, y + 1, 278, 138, LOGO_FRAME);
    fb_fill_rect(x + 8, y + 8, 264, 124, LOGO_BODY);
    /* Faint horizontal highlight band approximating the original's
     * radial amber gradient. */
    fb_fill_rect(x + 60, y + 14, 160, 14, LOGO_HIGH);

    /* Box icon: two darker-orange columns with a white pillar
     * between them and a black 1-px outline on each. */
    int bx = x + 22, by = y + 30;
    fb_fill_rect(bx,      by, 28, 68, LOGO_COL);
    fb_fill_rect(bx + 50, by, 28, 68, LOGO_COL);
    fb_fill_rect(bx + 28, by, 22, 68, 15);
    gui_rect(bx,        by, 28, 68, 0);
    gui_rect(bx + 50,   by, 28, 68, 0);
    gui_rect(bx + 28,   by, 22, 68, 0);

    /* "BoxOS" / "Arise" wordmark, scale 3 = 24 px tall, transparent
     * background (bg=0xFF) so the orange shows through. */
    big_text(x + 120, y + 28, "BoxOS", 3, 0, 0xFF);
    big_text(x + 120, y + 60, "Arise", 3, 0, 0xFF);

    /* Subtitle centred across the badge. */
    int sub_x = x + (280 - 16 * 8) / 2;
    gui_text(sub_x, y + 118, "version 3.0 (RadiDOS)", 0, LOGO_BODY);
}

/* --- splash ----------------------------------------------------- */

void splash_show(void) {
    if (!fb_present()) return;
    fb_fill_rect(0, 0, 640, 480, 1);

    paint_boxos_logo((640 - 280) / 2, 130);
    gui_text(216, 304, "64-bit hand-rolled hobby OS",  11, 1);
    gui_text(184, 336, "click or press any key to start", 7, 1);

    uint64_t deadline = timer_ms() + 2000;
    while (timer_ms() < deadline) {
        struct gui_event ev;
        while (gui_poll_event(&ev)) {
            if (ev.type == GUI_EV_KEY ||
                ev.type == GUI_EV_MOUSE_DOWN) return;
        }
        timer_sleep_ms(20);
    }
}

/* --- executive -------------------------------------------------- */

#define EX_W            640
#define EX_H            480
#define TITLE_H          16    /* "BoxOS Executive — \PATH" bar      */
#define MENUBAR_H        16    /* File View Special row              */
#define TOOLBAR_H        16    /* Drive selector row                 */
#define STATUS_H         16    /* status line at bottom              */
#define LIST_TOP        (TITLE_H + MENUBAR_H + TOOLBAR_H)
#define LIST_BOTTOM     (EX_H - STATUS_H)
#define ROW_H            16    /* one 8x16 line per file             */
#define MAX_VISIBLE     ((LIST_BOTTOM - LIST_TOP) / ROW_H)
#define MAX_ENTRIES     128

static struct fat12_entry g_entries[MAX_ENTRIES];
static int      g_n_entries = 0;
static int      g_sel = 0;
static int      g_scroll = 0;

/* CWD: small stack of (cluster, name) so we can navigate. */
#define CWD_DEPTH_MAX 8
static struct {
    uint16_t cluster;
    char     name[FAT12_MAX_NAME];
} g_cwd_stack[CWD_DEPTH_MAX];
static int g_cwd_depth = 0;

static uint16_t cwd_cluster(void) {
    return g_cwd_depth == 0 ? 0 : g_cwd_stack[g_cwd_depth - 1].cluster;
}

static void cwd_path_str(char* out, int cap) {
    int o = 0;
    if (cap < 4) { if (cap) out[0] = 0; return; }
    out[o++] = 'C';
    out[o++] = ':';
    out[o++] = '\\';
    for (int i = 0; i < g_cwd_depth; i++) {
        int n = 0; while (g_cwd_stack[i].name[n]) n++;
        if (o + n + 1 >= cap) break;
        for (int k = 0; k < n; k++) out[o++] = g_cwd_stack[i].name[k];
        if (i + 1 < g_cwd_depth) out[o++] = '\\';
    }
    out[o] = 0;
}

static void uint_to_str(uint64_t v, char* out) {
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
    int o = 0;
    while (t) out[o++] = tmp[--t];
    out[o] = 0;
}

static int strlen_local(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int strcmp_local(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int has_suffix_icase(const char* s, const char* suf) {
    int sn = 0; while (s[sn]) sn++;
    int fn = 0; while (suf[fn]) fn++;
    if (sn < fn) return 0;
    for (int i = 0; i < fn; i++) {
        char a = s[sn - fn + i]; if (a >= 'a' && a <= 'z') a -= 32;
        char b = suf[i];        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

static const char* type_label(const struct fat12_entry* e) {
    if (e->attr & 0x10) return "<DIR>";
    if (has_suffix_icase(e->name, ".BIN")) return "App";
    if (has_suffix_icase(e->name, ".EXE")) return "EXE";
    if (has_suffix_icase(e->name, ".TXT")) return "Text";
    if (has_suffix_icase(e->name, ".WAD")) return "Data";
    if (has_suffix_icase(e->name, ".IMG")) return "Disk";
    if (has_suffix_icase(e->name, ".SYS")) return "Sys";
    if (has_suffix_icase(e->name, ".BMP")) return "Image";
    if (has_suffix_icase(e->name, ".JPG") ||
        has_suffix_icase(e->name, ".JPEG")) return "Image";
    if (has_suffix_icase(e->name, ".MP3")) return "Audio";
    if (has_suffix_icase(e->name, ".MP4")) return "Video";
    if (has_suffix_icase(e->name, ".ANI")) return "Anim";
    return "File";
}

static int g_sort_mode = 0;     /* 0 = name, 1 = size */

static void load_dir(void) {
    g_n_entries = fs_list_dir(cwd_cluster(), g_entries, MAX_ENTRIES);
    if (g_n_entries < 0) g_n_entries = 0;
    /* Insertion sort: dirs first, then by current sort mode. */
    for (int i = 1; i < g_n_entries; i++) {
        struct fat12_entry e = g_entries[i];
        int j = i - 1;
        while (j >= 0) {
            int aD = (g_entries[j].attr & 0x10) ? 1 : 0;
            int bD = (e.attr & 0x10) ? 1 : 0;
            int cmp;
            if (aD != bD) cmp = bD - aD;
            else if (g_sort_mode == 1)
                cmp = (int)g_entries[j].size - (int)e.size;
            else
                cmp = strcmp_local(g_entries[j].name, e.name);
            if (cmp <= 0) break;
            g_entries[j + 1] = g_entries[j];
            j--;
        }
        g_entries[j + 1] = e;
    }
    g_sel = 0;
    g_scroll = 0;
}

/* ---- painting -------------------------------------------------- */

static void paint_title_bar(void) {
    const struct os_theme* T = theme_active();
    fb_fill_rect(0, 0, EX_W, TITLE_H, T->title_bg);
    const char* hdr = "BoxOS 2.0";
    int hw = 9 * 8;
    gui_text((EX_W - hw) / 2, 4, hdr, T->title_fg, T->title_bg);
}

static void paint_menu_bar(void) {
    const struct os_theme* T = theme_active();
    fb_fill_rect(0, TITLE_H, EX_W, MENUBAR_H, T->menu_bg);
    gui_text(8,  TITLE_H + 4, "File",    T->menu_fg, T->menu_bg);
    gui_text(48, TITLE_H + 4, "View",    T->menu_fg, T->menu_bg);
    gui_text(96, TITLE_H + 4, "Special", T->menu_fg, T->menu_bg);
    gui_hline(0, TITLE_H + MENUBAR_H - 1, EX_W, 0);
}

static void paint_toolbar(void) {
    const struct os_theme* T = theme_active();
    int y = TITLE_H + MENUBAR_H;
    fb_fill_rect(0, y, EX_W, TOOLBAR_H, 7);
    fb_fill_rect(8, y + 2, 24, TOOLBAR_H - 4, T->accent);
    gui_text(13, y + 4, "C:", 15, T->accent);
    char path[80];
    cwd_path_str(path, sizeof(path));
    gui_text(40, y + 4, path, 0, 7);
    char buf[32];
    char num[12]; uint_to_str((uint64_t)g_n_entries, num);
    int b = 0; const char* p = num; while (*p) buf[b++] = *p++;
    const char* tail = " files";
    int t = 0; while (tail[t]) buf[b++] = tail[t++];
    buf[b] = 0;
    gui_text(EX_W - b * 8 - 8, y + 4, buf, 0, 7);
    gui_hline(0, y + TOOLBAR_H - 1, EX_W, 0);
}

static void paint_row(int idx, int row_y, int selected) {
    const struct os_theme* T = theme_active();
    if (idx < 0 || idx >= g_n_entries) {
        fb_fill_rect(0, row_y, EX_W, ROW_H, 7);
        return;
    }
    struct fat12_entry* e = &g_entries[idx];
    uint8_t bg = selected ? T->accent : 7;
    uint8_t fg = selected ? 15 : 0;
    fb_fill_rect(0, row_y, EX_W, ROW_H, bg);

    /* Icon glyph: D for dir, A for app, T for text, F for file. */
    char icon = (e->attr & 0x10) ? 'D' :
                has_suffix_icase(e->name, ".BIN") ? 'A' :
                has_suffix_icase(e->name, ".TXT") ? 'T' : 'F';
    uint8_t icon_bg = (e->attr & 0x10) ? 14 :
                      has_suffix_icase(e->name, ".BIN") ? 2 :
                      has_suffix_icase(e->name, ".TXT") ? 11 : 8;
    fb_fill_rect(8, row_y + 1, 14, ROW_H - 2, icon_bg);
    char ic[2] = { icon, 0 };
    gui_text(11, row_y + 4, ic, 0, icon_bg);

    /* Filename (left-aligned, max 14 chars displayed). */
    gui_text(32, row_y + 4, e->name, fg, bg);

    /* Type column (around x=240). */
    gui_text(280, row_y + 4, type_label(e), fg, bg);

    /* Size or <DIR> column (right-aligned around x=560). */
    char sz[16];
    if (e->attr & 0x10) {
        const char* d = "<DIR>";
        int n = 0; while (d[n]) { sz[n] = d[n]; n++; } sz[n] = 0;
    } else {
        uint_to_str((uint64_t)e->size, sz);
    }
    int sn = 0; while (sz[sn]) sn++;
    gui_text(560 - sn * 8, row_y + 4, sz, fg, bg);
}

static void paint_list(void) {
    fb_fill_rect(0, LIST_TOP, EX_W, LIST_BOTTOM - LIST_TOP, 7);
    for (int i = 0; i < MAX_VISIBLE; i++) {
        int idx = g_scroll + i;
        int y = LIST_TOP + i * ROW_H;
        paint_row(idx, y, idx == g_sel);
    }
}

static void paint_status(void) {
    const struct os_theme* T = theme_active();
    int y = LIST_BOTTOM;
    fb_fill_rect(0, y, EX_W, STATUS_H, T->status_bg);
    /* Left half: selection info if anything is selected. */
    if (g_sel >= 0 && g_sel < g_n_entries) {
        struct fat12_entry* e = &g_entries[g_sel];
        char info[80]; int n = 0;
        int i = 0;
        while (e->name[i] && n < (int)sizeof(info) - 1) info[n++] = e->name[i++];
        const char* sep = "  -  ";
        i = 0; while (sep[i] && n < (int)sizeof(info) - 1) info[n++] = sep[i++];
        if (e->attr & 0x10) {
            const char* d = "directory";
            i = 0; while (d[i] && n < (int)sizeof(info) - 1) info[n++] = d[i++];
        } else {
            char num[16]; uint_to_str((uint64_t)e->size, num);
            i = 0; while (num[i] && n < (int)sizeof(info) - 1) info[n++] = num[i++];
            const char* tail = " bytes";
            i = 0; while (tail[i] && n < (int)sizeof(info) - 1) info[n++] = tail[i++];
        }
        info[n] = 0;
        gui_text(8, y + 4, info, T->status_fg, T->status_bg);
    } else {
        gui_text(8, y + 4, "no selection", T->status_fg, T->status_bg);
    }
    /* Right side: shortcut hint. */
    const char* hint = "dbl=open  r=run  d=del  n=ren  i=info  Bksp=up";
    int hl = 0; while (hint[hl]) hl++;
    int hx = EX_W - hl * 8 - 8;
    if (hx < 240) hx = 240;
    gui_text(hx, y + 4, hint, T->status_fg, T->status_bg);
}

/* The "desktop" paint: top menu bar over a solid wallpaper region.
 * No file listing — the file browser is its own program (WINFILES)
 * launched from Program Manager. paint_executive keeps its name for
 * compatibility with wm_set_bg_repaint and the menu dropdowns, but
 * it now just lays down the BoxOS 2.0 desktop background. */
static void paint_executive(void) {
    /* Arise: the desktop is just wallpaper + a bottom taskbar. The
     * old v1.0/v2.0 title bar and "FILE VIEW SPECIAL" menu strip are
     * gone — wm_init paints both the wallpaper (the theme's
     * status_bg) and the taskbar with its Start button. */
    (void)theme_active();
    wm_init();
    cursor_show();
}

/* Forward declarations so the menu plumbing below can reference
 * about_dialog and paint_executive without re-ordering the file. */
static void about_dialog(void);
static void paint_executive(void);
static int  input_dialog(const char* title, const char* prompt,
                         char* out, int cap);
static int  confirm_dialog(const char* title, const char* msg);
static void info_dialog(const struct fat12_entry* e);
static void editor_open(const char* name);
static void exe_info_dialog(const char* name);

/* ---- clipboard for Copy / Paste -------------------------------- */

#define CLIP_MAX_SIZE 16384
static char  g_clip_buf[CLIP_MAX_SIZE];
static char  g_clip_name[FAT12_MAX_NAME];
static int   g_clip_size = -1;       /* -1 = nothing copied yet     */

/* ---- File menu drop-down --------------------------------------- */

#define MENU_FILE_X       8
#define MENU_FILE_W      40
#define MENU_VIEW_X      48
#define MENU_VIEW_W      40
#define MENU_SPECIAL_X   96
#define MENU_SPECIAL_W   60

#define DROPDOWN_W      168
#define DROPDOWN_ROW_H   16

struct menu_item {
    const char* label;
    int         id;
};

#define MI_NEW_TXT 1
#define MI_NEW_BIN 2  /* (skipped — apps live as compiled binaries) */
#define MI_OPEN_TERMINAL 10
#define MI_REFRESH 20
#define MI_ABOUT   30
#define MI_LOG_OUT 40

static const struct menu_item file_menu[] = {
    { "New TXT file",   MI_NEW_TXT       },
    { "Open Terminal",  MI_OPEN_TERMINAL },
    { "Refresh",        MI_REFRESH       },
    { "About BoxOS",    MI_ABOUT         },
    { "Log Out",        MI_LOG_OUT       },
};
#define FILE_MENU_N ((int)(sizeof(file_menu) / sizeof(file_menu[0])))

static int dropdown_h(void) { return FILE_MENU_N * DROPDOWN_ROW_H + 4; }

static void paint_dropdown(int hilight) {
    int x = MENU_FILE_X - 4;
    int y = TITLE_H + MENUBAR_H;
    int h = dropdown_h();
    /* Drop shadow */
    fb_fill_rect(x + 4, y + 4, DROPDOWN_W, h, 0);
    /* Body */
    fb_fill_rect(x, y, DROPDOWN_W, h, 7);
    gui_rect    (x, y, DROPDOWN_W, h, 0);
    for (int i = 0; i < FILE_MENU_N; i++) {
        int row_y = y + 2 + i * DROPDOWN_ROW_H;
        uint8_t bg = (i == hilight) ? 9 : 7;
        uint8_t fg = (i == hilight) ? 15 : 0;
        if (i == hilight) fb_fill_rect(x + 2, row_y, DROPDOWN_W - 4, DROPDOWN_ROW_H, bg);
        gui_text(x + 8, row_y + 4, file_menu[i].label, fg, bg);
    }
    cursor_show();
}

static int hit_dropdown(int mx, int my) {
    int x = MENU_FILE_X - 4;
    int y = TITLE_H + MENUBAR_H;
    if (mx < x || mx >= x + DROPDOWN_W) return -1;
    if (my < y + 2 || my >= y + 2 + FILE_MENU_N * DROPDOWN_ROW_H) return -1;
    return (my - y - 2) / DROPDOWN_ROW_H;
}

/* Modal text-input dialog. Returns 1 if user pressed Enter (out is
 * filled), 0 if Esc was pressed. */
static int input_dialog(const char* title, const char* prompt,
                        char* out, int cap) {
    int dw = 360, dh = 96;
    int dx = (EX_W - dw) / 2;
    int dy = (EX_H - dh) / 2;
    fb_fill_rect(dx + 4, dy + 4, dw, dh, 0);
    fb_fill_rect(dx, dy, dw, dh, 7);
    gui_rect    (dx, dy, dw, dh, 0);
    fb_fill_rect(dx + 1, dy + 1, dw - 2, 14, 1);
    gui_text(dx + (dw - 8 * (int)strlen_local(title)) / 2, dy + 4,
             title, 15, 1);
    gui_text(dx + 12, dy + 28, prompt, 0, 7);
    /* input box */
    int ix = dx + 12, iy = dy + 48, iw = dw - 24, ih = 18;
    fb_fill_rect(ix, iy, iw, ih, 15);
    gui_rect    (ix, iy, iw, ih, 0);
    cursor_hide();
    int n = 0;
    out[0] = 0;
    for (;;) {
        char c = keyboard_getc();
        if (c == 27) { return 0; }
        if (c == '\r' || c == '\n') {
            if (n > 0) { out[n] = 0; cursor_show(); return 1; }
            continue;
        }
        if (c == 8) {           /* backspace */
            if (n > 0) {
                n--;
                fb_fill_rect(ix + 4 + n * 8, iy + 5, 8, 8, 15);
            }
            continue;
        }
        if (c >= ' ' && c < 127 && n + 1 < cap && (4 + (n + 1) * 8) < iw) {
            out[n++] = c;
            char s[2] = { c, 0 };
            gui_text(ix + 4 + (n - 1) * 8, iy + 5, s, 0, 15);
        }
    }
}

static void run_menu_action(int id) {
    if (id == MI_NEW_TXT) {
        char name[FAT12_MAX_NAME];
        if (input_dialog("New TXT file", "Filename (e.g. NOTES.TXT):",
                         name, sizeof(name))) {
            /* Auto-suffix .TXT if the user didn't include a dot. */
            int has_dot = 0; int n = 0;
            while (name[n]) { if (name[n] == '.') has_dot = 1; n++; }
            if (!has_dot && n + 4 < FAT12_MAX_NAME) {
                const char* sfx = ".TXT";
                for (int i = 0; sfx[i]; i++) name[n++] = sfx[i];
                name[n] = 0;
            }
            int rc = fs_create_file(cwd_cluster(), name, "", 0);
            paint_executive();
            char msg[80];
            int m = 0;
            if (rc < 0) {
                const char* p = "create failed: ";
                while (*p) msg[m++] = *p++;
            } else {
                const char* p = "created ";
                while (*p) msg[m++] = *p++;
            }
            int j = 0;
            while (name[j] && m < (int)sizeof(msg) - 1) msg[m++] = name[j++];
            msg[m] = 0;
            (void)msg;
            load_dir();
            paint_executive();
        } else {
            paint_executive();
        }
    } else if (id == MI_OPEN_TERMINAL) {
        cursor_hide();
        wm_open_terminal();
        paint_executive();
    } else if (id == MI_REFRESH) {
        load_dir();
        paint_executive();
    } else if (id == MI_ABOUT) {
        about_dialog();
    } else if (id == MI_LOG_OUT) {
        /* Drop out of desktop_loop — kernel.c falls through to the
         * bare shell as a recovery shell. */
    }
}

/* ---- View menu drop-down --------------------------------------- */

#define MI_SORT_NAME 50
#define MI_SORT_SIZE 51

static const struct menu_item view_menu[] = {
    { "Sort by Name", MI_SORT_NAME },
    { "Sort by Size", MI_SORT_SIZE },
    { "Refresh",      MI_REFRESH   },
};
#define VIEW_MENU_N ((int)(sizeof(view_menu) / sizeof(view_menu[0])))

static void paint_view_dropdown(int hilight) {
    int x = MENU_VIEW_X - 4;
    int y = TITLE_H + MENUBAR_H;
    int h = VIEW_MENU_N * DROPDOWN_ROW_H + 4;
    fb_fill_rect(x + 4, y + 4, DROPDOWN_W, h, 0);
    fb_fill_rect(x, y, DROPDOWN_W, h, 7);
    gui_rect    (x, y, DROPDOWN_W, h, 0);
    for (int i = 0; i < VIEW_MENU_N; i++) {
        int row_y = y + 2 + i * DROPDOWN_ROW_H;
        uint8_t bg = (i == hilight) ? 9 : 7;
        uint8_t fg = (i == hilight) ? 15 : 0;
        if (i == hilight) fb_fill_rect(x + 2, row_y, DROPDOWN_W - 4,
                                        DROPDOWN_ROW_H, bg);
        gui_text(x + 8, row_y + 4, view_menu[i].label, fg, bg);
        /* Active sort mode tagged "*". */
        if (view_menu[i].id == MI_SORT_NAME && g_sort_mode == 0)
            gui_text(x + DROPDOWN_W - 16, row_y + 4, "*", 4, bg);
        if (view_menu[i].id == MI_SORT_SIZE && g_sort_mode == 1)
            gui_text(x + DROPDOWN_W - 16, row_y + 4, "*", 4, bg);
    }
    cursor_show();
}

static int hit_view_dropdown(int mx, int my) {
    int x = MENU_VIEW_X - 4;
    int y = TITLE_H + MENUBAR_H;
    if (mx < x || mx >= x + DROPDOWN_W) return -1;
    if (my < y + 2 || my >= y + 2 + VIEW_MENU_N * DROPDOWN_ROW_H) return -1;
    return (my - y - 2) / DROPDOWN_ROW_H;
}

static int view_menu_loop(void) {
    int hover = -1;
    paint_view_dropdown(hover);
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { timer_sleep_ms(15); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            if ((ev.arg1 & 0xFF) == 27) return -1;
        } else if (ev.type == GUI_EV_MOUSE_MOVE) {
            int h = hit_view_dropdown(ev.x, ev.y);
            if (h != hover) { hover = h; paint_view_dropdown(hover); }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int h = hit_view_dropdown(ev.x, ev.y);
            if (h >= 0) return view_menu[h].id;
            return -1;
        }
    }
}

/* Run the File-menu drop-down: paint, poll events until user picks
 * an item or clicks outside / presses Esc. Returns the chosen menu
 * id (or -1 if cancelled). */
static int file_menu_loop(void) {
    int hover = -1;
    paint_dropdown(hover);
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { timer_sleep_ms(15); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned ascii = ev.arg1 & 0xFF;
            if (ascii == 27) return -1;
        } else if (ev.type == GUI_EV_MOUSE_MOVE) {
            int h = hit_dropdown(ev.x, ev.y);
            if (h != hover) {
                hover = h;
                paint_dropdown(hover);
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int h = hit_dropdown(ev.x, ev.y);
            if (h >= 0) return file_menu[h].id;
            /* clicked outside → close */
            return -1;
        }
    }
}

/* ---- About dialog ---------------------------------------------- */

static void about_dialog(void) {
    /* Modal-ish: paint a centred dialog with the BoxOS brand mark on
     * top, system stats below, wait for click/key, then repaint
     * the desktop. */
    int dw = 360, dh = 264;
    int dx = (EX_W - dw) / 2;
    int dy = (EX_H - dh) / 2;
    /* Drop-shadow */
    fb_fill_rect(dx + 4, dy + 4, dw, dh, 0);
    /* Body */
    fb_fill_rect(dx, dy, dw, dh, 7);
    gui_rect    (dx, dy, dw, dh, 0);
    /* Title bar */
    fb_fill_rect(dx + 1, dy + 1, dw - 2, 14, 1);
    gui_text(dx + (dw - 8 * 5) / 2, dy + 4, "About", 15, 1);

    /* Brand mark, centred horizontally just below the title bar. */
    paint_boxos_logo(dx + (dw - 280) / 2, dy + 22);

    /* Heap stat (the one piece of "real" info worth surfacing). */
    char heap[80]; int n = 0;
    const char* h = "Heap free: ";
    while (h[n]) { heap[n] = h[n]; n++; }
    char num[24];
    uint_to_str((uint64_t)(heap_total() - heap_used()), num);
    int p = 0; while (num[p]) heap[n++] = num[p++];
    const char* tail = " bytes";
    p = 0; while (tail[p]) heap[n++] = tail[p++];
    heap[n] = 0;
    gui_text(dx + 16, dy + 188, heap, 0, 7);
    gui_text(dx + 16, dy + 208, "64-bit hand-rolled hobby OS.",  0, 7);
    gui_text(dx + 16, dy + 224, "kernel + apps in C+asm.",       0, 7);
    gui_text(dx + 16, dy + 244,
             "Click or press any key to dismiss.", 0, 7);
    cursor_show();
    /* Wait for a click or key. */
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { timer_sleep_ms(20); continue; }
        if (ev.type == GUI_EV_KEY ||
            ev.type == GUI_EV_MOUSE_DOWN) break;
    }
    paint_executive();
}

/* ---- launch ---------------------------------------------------- */

/* True for apps that paint their own pixels and don't expect a
 * terminal context (graphics-mode apps and windowed apps). */
/* ---- confirm + info dialogs ------------------------------------ */

static int confirm_dialog(const char* title, const char* msg) {
    int dw = 360, dh = 96;
    int dx = (EX_W - dw) / 2;
    int dy = (EX_H - dh) / 2;
    fb_fill_rect(dx + 4, dy + 4, dw, dh, 0);
    fb_fill_rect(dx, dy, dw, dh, 7);
    gui_rect    (dx, dy, dw, dh, 0);
    fb_fill_rect(dx + 1, dy + 1, dw - 2, 14, 1);
    gui_text(dx + (dw - 8 * strlen_local(title)) / 2, dy + 4, title, 15, 1);
    gui_text(dx + 16, dy + 28, msg, 0, 7);
    gui_text(dx + 16, dy + 64, "[Y]es / [N]o (Esc to cancel)", 0, 7);
    cursor_show();
    for (;;) {
        char c = keyboard_getc();
        if (c == 'Y' || c == 'y' || c == '\n' || c == '\r') return 1;
        if (c == 'N' || c == 'n' || c == 27) return 0;
    }
}

static void info_dialog(const struct fat12_entry* e) {
    int dw = 380, dh = 168;
    int dx = (EX_W - dw) / 2;
    int dy = (EX_H - dh) / 2;
    fb_fill_rect(dx + 4, dy + 4, dw, dh, 0);
    fb_fill_rect(dx, dy, dw, dh, 7);
    gui_rect    (dx, dy, dw, dh, 0);
    fb_fill_rect(dx + 1, dy + 1, dw - 2, 14, 1);
    gui_text(dx + (dw - 8 * 4) / 2, dy + 4, "Info", 15, 1);

    char line[80]; int n;
    /* Name */
    n = 0;
    const char* p = "Name:    ";
    while (*p) line[n++] = *p++;
    int j = 0; while (e->name[j] && n < (int)sizeof(line) - 1) line[n++] = e->name[j++];
    line[n] = 0;
    gui_text(dx + 16, dy + 28, line, 0, 7);

    /* Type */
    n = 0;
    p = "Type:    ";
    while (*p) line[n++] = *p++;
    const char* t = type_label(e);
    j = 0; while (t[j] && n < (int)sizeof(line) - 1) line[n++] = t[j++];
    line[n] = 0;
    gui_text(dx + 16, dy + 48, line, 0, 7);

    /* Size */
    n = 0;
    p = "Size:    ";
    while (*p) line[n++] = *p++;
    if (e->attr & 0x10) {
        const char* d = "(directory)";
        while (*d) line[n++] = *d++;
    } else {
        char num[12]; uint_to_str((uint64_t)e->size, num);
        j = 0; while (num[j]) line[n++] = num[j++];
        const char* tail = " bytes";
        j = 0; while (tail[j]) line[n++] = tail[j++];
    }
    line[n] = 0;
    gui_text(dx + 16, dy + 68, line, 0, 7);

    /* Cluster */
    n = 0;
    p = "Cluster: ";
    while (*p) line[n++] = *p++;
    char num[12]; uint_to_str((uint64_t)e->first_cluster, num);
    j = 0; while (num[j]) line[n++] = num[j++];
    line[n] = 0;
    gui_text(dx + 16, dy + 88, line, 0, 7);

    /* Attr flags */
    n = 0;
    p = "Attr:    ";
    while (*p) line[n++] = *p++;
    line[n++] = (e->attr & 0x01) ? 'R' : '-';
    line[n++] = (e->attr & 0x02) ? 'H' : '-';
    line[n++] = (e->attr & 0x04) ? 'S' : '-';
    line[n++] = (e->attr & 0x08) ? 'V' : '-';
    line[n++] = (e->attr & 0x10) ? 'D' : '-';
    line[n++] = (e->attr & 0x20) ? 'A' : '-';
    line[n] = 0;
    gui_text(dx + 16, dy + 108, line, 0, 7);

    gui_text(dx + 16, dy + 138, "Click or press any key to dismiss.", 0, 7);
    cursor_show();
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { timer_sleep_ms(20); continue; }
        if (ev.type == GUI_EV_KEY ||
            ev.type == GUI_EV_MOUSE_DOWN) break;
    }
}

/* ---- Print Screen — capture FB to /DOCS/SCREENn.BMP ----------- */

static void print_screen_save(void) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    int W = bi->fb_width, H = bi->fb_height;
    int pitch = bi->fb_pitch;
    /* 8-bit indexed BMP: 14-byte file hdr + 40-byte DIB hdr +
     * 256*4 palette + W*H bytes of pixels (W is multiple of 4 on us). */
    static uint8_t shot[640 * 480 + 14 + 40 + 256 * 4];
    if ((unsigned)(W * H) > sizeof(shot) - (14 + 40 + 256 * 4)) return;

    int hdr_bytes = 14 + 40 + 256 * 4;
    int pix_bytes = W * H;
    int total = hdr_bytes + pix_bytes;
    uint8_t* p = shot;
    /* file header */
    p[0] = 'B'; p[1] = 'M';
    p[2] = (uint8_t)total; p[3] = (uint8_t)(total >> 8);
    p[4] = (uint8_t)(total >> 16); p[5] = (uint8_t)(total >> 24);
    p[6] = p[7] = p[8] = p[9] = 0;
    p[10] = (uint8_t)hdr_bytes; p[11] = (uint8_t)(hdr_bytes >> 8);
    p[12] = (uint8_t)(hdr_bytes >> 16); p[13] = (uint8_t)(hdr_bytes >> 24);
    /* DIB header */
    p += 14;
    p[0] = 40; p[1] = p[2] = p[3] = 0;
    p[4] = (uint8_t)W; p[5] = (uint8_t)(W >> 8); p[6] = (uint8_t)(W >> 16); p[7] = (uint8_t)(W >> 24);
    p[8] = (uint8_t)H; p[9] = (uint8_t)(H >> 8); p[10] = (uint8_t)(H >> 16); p[11] = (uint8_t)(H >> 24);
    p[12] = 1; p[13] = 0;             /* planes */
    p[14] = 8; p[15] = 0;             /* bpp */
    p[16] = p[17] = p[18] = p[19] = 0;
    p[20] = (uint8_t)pix_bytes; p[21] = (uint8_t)(pix_bytes >> 8);
    p[22] = (uint8_t)(pix_bytes >> 16); p[23] = (uint8_t)(pix_bytes >> 24);
    for (int i = 24; i < 32; i++) p[i] = 0;
    p[32] = 0; p[33] = 1; p[34] = p[35] = 0;     /* 256 colours */
    p[36] = p[37] = p[38] = p[39] = 0;
    /* Palette: read DAC, convert 6-bit → 8-bit BGRA. */
    uint8_t* pal = p + 40;
    outb(0x3C7, 0);
    for (int i = 0; i < 256; i++) {
        uint8_t r = inb(0x3C9);
        uint8_t g = inb(0x3C9);
        uint8_t b = inb(0x3C9);
        r = (uint8_t)((r << 2) | (r >> 4));
        g = (uint8_t)((g << 2) | (g >> 4));
        b = (uint8_t)((b << 2) | (b >> 4));
        pal[i * 4 + 0] = b;
        pal[i * 4 + 1] = g;
        pal[i * 4 + 2] = r;
        pal[i * 4 + 3] = 0;
    }
    /* Pixels — bottom-up rows. */
    uint8_t* px = shot + hdr_bytes;
    for (int y = 0; y < H; y++) {
        const uint8_t* src = fb_pixels() + (uint64_t)(H - 1 - y) * pitch;
        uint8_t* dst = px + y * W;
        for (int x = 0; x < W; x++) dst[x] = src[x];
    }
    /* Save into /DOCS with an auto-incrementing name SCREEN1.BMP,
     * SCREEN2.BMP, … so consecutive PrtScns don't overwrite. */
    uint16_t docs = fs_find_dir(0, "DOCS");
    if (docs == 0xFFFF) return;
    char name[16];
    for (int n = 1; n < 100; n++) {
        int p = 0;
        const char* pre = "SCREEN";
        while (pre[p]) { name[p] = pre[p]; p++; }
        if (n < 10) { name[p++] = (char)('0' + n); }
        else        { name[p++] = (char)('0' + n / 10); name[p++] = (char)('0' + n % 10); }
        const char* ext = ".BMP";
        for (int j = 0; ext[j]; j++) name[p++] = ext[j];
        name[p] = 0;
        if (fs_read_in(docs, name, 0, 0) < 0) {
            fs_create_file(docs, name, shot, (uint32_t)total);
            return;
        }
    }
}

/* ---- EXE inspector dialog (double-click on .EXE) --------------- */

static void hex_to_str(uint32_t v, int width, char* out) {
    /* Lower-case hex with leading zeros to `width`. */
    int n = 0;
    char tmp[12];
    if (v == 0) tmp[n++] = '0';
    while (v) {
        int d = (int)(v & 0xF);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
    }
    int o = 0;
    out[o++] = '0';
    out[o++] = 'x';
    while (n < width) { out[o++] = '0'; width--; }
    while (n) out[o++] = tmp[--n];
    out[o] = 0;
}

/* Append the C string `s` to `dst` at position `*p`, capped at `cap`. */
static void append(char* dst, int* p, int cap, const char* s) {
    while (*s && *p < cap - 1) dst[(*p)++] = *s++;
    dst[*p] = 0;
}

static void exe_info_dialog(const char* name) {
    struct exe_info info;
    int rc = exe_inspect(name, &info);

    int dw = 460, dh = 220;
    int dx = (EX_W - dw) / 2;
    int dy = (EX_H - dh) / 2;
    fb_fill_rect(dx + 4, dy + 4, dw, dh, 0);
    fb_fill_rect(dx, dy, dw, dh, 7);
    gui_rect    (dx, dy, dw, dh, 0);
    fb_fill_rect(dx + 1, dy + 1, dw - 2, 14, 1);
    gui_text(dx + (dw - 8 * 8) / 2, dy + 4, "EXE Info", 15, 1);

    if (rc < 0) {
        gui_text(dx + 16, dy + 32, name, 0, 7);
        gui_text(dx + 16, dy + 56, "Could not parse this file as an EXE.", 0, 7);
        gui_text(dx + 16, dy + 76, "(Truncated, missing, or unknown format.)", 0, 7);
    } else {
        char line[120]; int p;

        /* Filename */
        gui_text(dx + 16, dy + 28, name, 0, 7);

        /* Format kind */
        p = 0;
        append(line, &p, sizeof(line), "Format:  ");
        append(line, &p, sizeof(line), exe_kind_label(&info));
        gui_text(dx + 16, dy + 48, line, 0, 7);

        /* Header / size */
        p = 0;
        append(line, &p, sizeof(line), "Header offset: ");
        char buf[16]; hex_to_str(info.hdr_offset, 4, buf);
        append(line, &p, sizeof(line), buf);
        append(line, &p, sizeof(line), "    Image size: ");
        char num[16]; uint_to_str((uint64_t)info.image_size, num);
        append(line, &p, sizeof(line), num);
        gui_text(dx + 16, dy + 68, line, 0, 7);

        if (info.kind == EXE_KIND_MZ || info.kind == EXE_KIND_NE) {
            p = 0;
            append(line, &p, sizeof(line), "Entry CS:IP = ");
            hex_to_str(info.mz_cs, 4, buf); append(line, &p, sizeof(line), buf);
            append(line, &p, sizeof(line), ":");
            hex_to_str(info.mz_ip, 4, buf); append(line, &p, sizeof(line), buf);
            gui_text(dx + 16, dy + 88, line, 0, 7);
        }

        if (info.kind == EXE_KIND_NE) {
            p = 0;
            append(line, &p, sizeof(line), "NE segments: ");
            uint_to_str((uint64_t)info.ne_n_segments, num);
            append(line, &p, sizeof(line), num);
            append(line, &p, sizeof(line), "    Target: ");
            const char* os = (info.ne_target_os == 1) ? "OS/2" :
                             (info.ne_target_os == 2) ? "Windows" :
                             (info.ne_target_os == 4) ? "Win386" : "(unknown)";
            append(line, &p, sizeof(line), os);
            gui_text(dx + 16, dy + 108, line, 0, 7);
        }

        if (info.kind == EXE_KIND_PE) {
            p = 0;
            append(line, &p, sizeof(line), "Machine: ");
            append(line, &p, sizeof(line), exe_pe_machine_label(info.pe_machine));
            gui_text(dx + 16, dy + 88, line, 0, 7);

            p = 0;
            append(line, &p, sizeof(line), "Subsystem: ");
            append(line, &p, sizeof(line), exe_pe_subsystem_label(info.pe_subsystem));
            if (info.pe_dll) append(line, &p, sizeof(line), "  (DLL)");
            gui_text(dx + 16, dy + 108, line, 0, 7);

            p = 0;
            append(line, &p, sizeof(line), "Entry RVA: ");
            hex_to_str(info.pe_entry_rva, 8, buf);
            append(line, &p, sizeof(line), buf);
            gui_text(dx + 16, dy + 128, line, 0, 7);
        }

        /* Run-status — important: tell the user we can't run it. */
        gui_text(dx + 16, dy + 158, exe_runnable_message(&info), 4, 7);
    }

    gui_text(dx + 16, dy + dh - 24, "Click or press any key to dismiss.",
             0, 7);
    cursor_show();
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { timer_sleep_ms(20); continue; }
        if (ev.type == GUI_EV_KEY ||
            ev.type == GUI_EV_MOUSE_DOWN) break;
    }
}

/* ---- in-kernel text editor (used by Edit) ---------------------- */

/* Big static — lives in BSS, doesn't bloat kernel.bin on disk.
 * Picked so most BoxOS-built binaries (kernel ~96 KB, apps <12 KB)
 * fit; bigger files (DOOM1.WAD = 4 MB) trigger the read-only path. */
#define EDITOR_BUF_BYTES (256 * 1024)
static char g_editor_buf[EDITOR_BUF_BYTES];

static void editor_open(const char* name) {
    if (!fb_present()) return;
    const struct boot_info* bi = fb_bootinfo();
    int W = bi->fb_width, H = bi->fb_height;
    int term_w = 80 * 8 + 2;
    int term_h = 25 * 16 + 14;
    int term_x = (W - term_w) / 2;
    int term_y = 16 + ((H - 16) - term_h) / 2;
    if (term_y < 18) term_y = 18;

    char title[64];
    int tn = 0;
    const char* tp = "Editor — ";
    while (*tp && tn < 60) title[tn++] = *tp++;
    int j = 0; while (name[j] && tn < 60) title[tn++] = name[j++];
    title[tn] = 0;
    int win = wm_open_window(title, term_x, term_y, term_w, term_h);
    if (win < 0) return;

    cursor_hide();
    int cx = term_x + 1;
    int cy = term_y + 1 + 12;
    fb_fill_rect(cx, cy, 80 * 8, 25 * 16, 0);

    fbcon_set_origin(cx, cy);
    fbcon_set_visible(true);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    /* Probe file size before reading: if it's bigger than our buffer
     * we go read-only so we don't save a truncated copy back over a
     * 4 MB WAD. */
    int file_size = fs_read_in(cwd_cluster(), name, 0, 0);
    int read_only = 0;
    int sz = 0;
    if (file_size < 0) {
        /* file gone? */
        sz = 0;
    } else if ((unsigned)file_size + 1 > sizeof(g_editor_buf)) {
        read_only = 1;
        sz = 0;        /* can't load; show banner only */
    } else {
        sz = fs_read_in(cwd_cluster(), name,
                        g_editor_buf, sizeof(g_editor_buf) - 1);
        if (sz < 0) sz = 0;
    }
    g_editor_buf[sz] = 0;

    /* Detect "is this likely text?" by counting non-printable bytes
     * in the first 256 bytes. >25% non-printable = treat as binary
     * (warn the user, allow viewing but discourage editing). */
    int probe = sz < 256 ? sz : 256;
    int non_printable = 0;
    for (int i = 0; i < probe; i++) {
        unsigned char c = (unsigned char)g_editor_buf[i];
        if (c == 0 || (c < 9) || (c > 13 && c < 32) || c == 127) non_printable++;
    }
    int is_binary = probe > 0 && non_printable * 4 > probe;

    /* Banner. */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_printf("Editor: %s   size=%d", name, file_size < 0 ? 0 : file_size);
    if (read_only)  vga_puts("   [READ-ONLY: file > 256 KB]");
    else if (is_binary) vga_puts("   [BINARY -- save will rewrite as bytes]");
    vga_puts("\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("Esc = save+exit   Ctrl+X = cancel\n\n");

    /* Render: convert any non-printable byte to '.' so the screen
     * doesn't try to render control codes (vga_putc treats them as
     * cursor moves which would scramble layout). */
    for (int i = 0; i < sz; i++) {
        unsigned char c = (unsigned char)g_editor_buf[i];
        if (c == '\n' || c == '\t' || (c >= 32 && c < 127)) vga_putc((char)c);
        else                                                 vga_putc('.');
    }
    fbcon_repaint();

    int cancelled = 0;
    int len = sz;
    for (;;) {
        char c = keyboard_getc();
        if (c == 27) break;
        if (c == 24) { cancelled = 1; break; }
        if (read_only) continue;
        if (c == 8) {
            if (len > 0) { len--; vga_putc('\b'); }
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (len + 1 < (int)sizeof(g_editor_buf)) {
                g_editor_buf[len++] = '\n';
                vga_putc('\n');
            }
            continue;
        }
        if (c >= ' ' && c < 127) {
            if (len + 1 < (int)sizeof(g_editor_buf)) {
                g_editor_buf[len++] = c;
                vga_putc(c);
            }
        }
    }

    fbcon_set_visible(false);
    fbcon_set_origin(0, 0);
    wm_close_window(win);

    if (!cancelled && !read_only) {
        fs_delete(cwd_cluster(), name);
        fs_create_file(cwd_cluster(), name, g_editor_buf, (uint32_t)len);
    }
}

/* ---- right-click context menu ---------------------------------- */

#define CTX_ROW_H  16
#define CTX_W     160

#define CTX_EDIT     1
#define CTX_RENAME   2
#define CTX_DELETE   3
#define CTX_INFO     4
#define CTX_COPY     5
#define CTX_PASTE    6

struct ctx_item { const char* label; int id; int enabled; };

static int ctx_items_for(const struct fat12_entry* e, struct ctx_item* out) {
    int n = 0;
    int is_dir = (e->attr & 0x10) ? 1 : 0;
    int is_dot = (e->name[0] == '.' && (e->name[1] == 0 ||
                  (e->name[1] == '.' && e->name[2] == 0)));
    int is_text = has_suffix_icase(e->name, ".TXT");
    if (!is_dot) {
        /* Edit is enabled for any non-directory file; the editor will
         * happily show the raw bytes of binaries (the BIOS 8x16 font
         * has glyphs for all 256 byte values, so binary content
         * displays as text-with-gibberish — useful for "what's
         * actually in this file"). For pure text files, what you see
         * is the source; for binaries, you see the bytes. */
        (void)is_text;
        out[n++] = (struct ctx_item){ "Edit",     CTX_EDIT,   !is_dir };
        out[n++] = (struct ctx_item){ "Rename",   CTX_RENAME, !is_dir };
        out[n++] = (struct ctx_item){ "Delete",   CTX_DELETE, !is_dot };
        out[n++] = (struct ctx_item){ "Info",     CTX_INFO,   1 };
        if (!is_dir)
            out[n++] = (struct ctx_item){ "Copy",  CTX_COPY,  1 };
    }
    if (g_clip_size >= 0)
        out[n++] = (struct ctx_item){ "Paste",    CTX_PASTE, 1 };
    return n;
}

static void paint_ctx(int x, int y, struct ctx_item* items, int n,
                      int hilight) {
    int h = n * CTX_ROW_H + 4;
    fb_fill_rect(x + 4, y + 4, CTX_W, h, 0);    /* shadow */
    fb_fill_rect(x, y, CTX_W, h, 7);
    gui_rect    (x, y, CTX_W, h, 0);
    for (int i = 0; i < n; i++) {
        int row_y = y + 2 + i * CTX_ROW_H;
        uint8_t bg = (i == hilight) ? 9 : 7;
        uint8_t fg = items[i].enabled ? ((i == hilight) ? 15 : 0) : 8;
        if (i == hilight) fb_fill_rect(x + 2, row_y, CTX_W - 4, CTX_ROW_H, bg);
        gui_text(x + 8, row_y + 4, items[i].label, fg, bg);
    }
    cursor_show();
}

/* Modal poll for the context menu. Returns the chosen id, or 0 if
 * cancelled. (mx, my) is the click anchor; menu opens to the right. */
static int context_menu_loop(int mx, int my, struct ctx_item* items, int n) {
    int x = mx;
    int y = my;
    if (x + CTX_W > EX_W) x = EX_W - CTX_W - 4;
    int h = n * CTX_ROW_H + 4;
    if (y + h > EX_H) y = EX_H - h - 4;
    int hover = -1;
    paint_ctx(x, y, items, n, hover);
    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { timer_sleep_ms(15); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            if ((ev.arg1 & 0xFF) == 27) return 0;
        } else if (ev.type == GUI_EV_MOUSE_MOVE) {
            int hi = -1;
            if (ev.x >= x && ev.x < x + CTX_W) {
                int row = (ev.y - y - 2) / CTX_ROW_H;
                if (row >= 0 && row < n) hi = row;
            }
            if (hi != hover) { hover = hi; paint_ctx(x, y, items, n, hover); }
        } else if (ev.type == GUI_EV_MOUSE_DOWN) {
            int hi = -1;
            if (ev.x >= x && ev.x < x + CTX_W) {
                int row = (ev.y - y - 2) / CTX_ROW_H;
                if (row >= 0 && row < n && items[row].enabled) hi = row;
            }
            if (hi >= 0) return items[hi].id;
            return 0;
        }
    }
}

/* Find an unused name for a paste target by appending "_N". */
static int unique_name(char* out, int cap, const char* base) {
    /* Try base first. */
    if (fs_read_in(cwd_cluster(), base, 0, 0) < 0) {
        int i = 0; while (base[i] && i < cap - 1) { out[i] = base[i]; i++; }
        out[i] = 0;
        return 1;
    }
    /* Insert _2, _3, ... before the dot (or at end if no dot). */
    for (int n = 2; n < 100; n++) {
        char tmp[FAT12_MAX_NAME];
        int t = 0;
        int dot = -1;
        for (int i = 0; base[i]; i++) if (base[i] == '.') dot = i;
        int end = dot >= 0 ? dot : (int)strlen_local(base);
        for (int i = 0; i < end && t < cap - 4; i++) tmp[t++] = base[i];
        if (t < cap - 3) tmp[t++] = '_';
        if (n < 10) tmp[t++] = '0' + n;
        else { tmp[t++] = '0' + (n / 10); tmp[t++] = '0' + (n % 10); }
        if (dot >= 0) {
            for (int i = dot; base[i] && t < cap - 1; i++) tmp[t++] = base[i];
        }
        tmp[t] = 0;
        if (fs_read_in(cwd_cluster(), tmp, 0, 0) < 0) {
            int i = 0; while (tmp[i] && i < cap - 1) { out[i] = tmp[i]; i++; }
            out[i] = 0;
            return 1;
        }
    }
    return 0;
}

static void run_ctx_action(int id, const struct fat12_entry* e) {
    if (id == CTX_EDIT) {
        editor_open(e->name);
    } else if (id == CTX_RENAME) {
        char newname[FAT12_MAX_NAME];
        if (!input_dialog("Rename", "New name:", newname, sizeof(newname))) {
            paint_executive(); return;
        }
        /* Read content, delete old, create new. Cap at 64 KiB inline. */
        static char rbuf[65536];
        int sz = fs_read_in(cwd_cluster(), e->name, rbuf, sizeof(rbuf));
        if (sz < 0) { paint_executive(); return; }
        fs_delete(cwd_cluster(), e->name);
        fs_create_file(cwd_cluster(), newname, rbuf, (uint32_t)sz);
    } else if (id == CTX_DELETE) {
        if (!confirm_dialog("Delete?", "Are you sure?")) {
            paint_executive(); return;
        }
        if (e->attr & 0x10) fs_rmdir(cwd_cluster(),  e->name);
        else                fs_delete(cwd_cluster(), e->name);
    } else if (id == CTX_INFO) {
        info_dialog(e);
    } else if (id == CTX_COPY) {
        int n = 0;
        while (e->name[n] && n < FAT12_MAX_NAME - 1) {
            g_clip_name[n] = e->name[n]; n++;
        }
        g_clip_name[n] = 0;
        int sz = fs_read_in(cwd_cluster(), e->name, g_clip_buf,
                            sizeof(g_clip_buf));
        g_clip_size = sz < 0 ? 0 : sz;
    } else if (id == CTX_PASTE) {
        char dst[FAT12_MAX_NAME];
        if (!unique_name(dst, sizeof(dst), g_clip_name)) {
            paint_executive(); return;
        }
        fs_create_file(cwd_cluster(), dst, g_clip_buf,
                       (uint32_t)g_clip_size);
    }
    load_dir();
    paint_executive();
}

static int is_gui_app(const char* leaf) {
    /* Windowed apps start with "WIN". */
    if ((leaf[0] == 'W' || leaf[0] == 'w') &&
        (leaf[1] == 'I' || leaf[1] == 'i') &&
        (leaf[2] == 'N' || leaf[2] == 'n')) return 1;
    /* Known graphics-mode apps. */
    if (has_suffix_icase(leaf, "PLASMA.BIN")) return 1;
    if (has_suffix_icase(leaf, "DOOM.BIN"))   return 1;
    return 0;
}

extern void wm_open_terminal_with(const char* run_path);

static void launch(const struct fat12_entry* e) {
    if (e->attr & 0x10) {
        /* Directory — push CWD and reload. ".." pops. */
        if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == 0) {
            if (g_cwd_depth > 0) g_cwd_depth--;
        } else if (e->name[0] == '.' && e->name[1] == 0) {
            /* "." — stay put. */
        } else {
            if (g_cwd_depth < CWD_DEPTH_MAX) {
                g_cwd_stack[g_cwd_depth].cluster = e->first_cluster;
                int n = 0;
                while (e->name[n] && n < FAT12_MAX_NAME - 1) {
                    g_cwd_stack[g_cwd_depth].name[n] = e->name[n];
                    n++;
                }
                g_cwd_stack[g_cwd_depth].name[n] = 0;
                g_cwd_depth++;
            }
        }
        load_dir();
        paint_executive();
        return;
    }
    /* .EXE — pop the inspector dialog (we can't run them yet). */
    if (has_suffix_icase(e->name, ".EXE")) {
        fs_set_app_cwd(cwd_cluster());
        cursor_hide();
        exe_info_dialog(e->name);
        paint_executive();
        return;
    }
    if (has_suffix_icase(e->name, ".BIN")) {
        fs_set_app_cwd(cwd_cluster());
        if (is_gui_app(e->name)) {
            /* Cursor stays as the regular arrow during the app's
             * run; loader_run flips to hourglass only while it's
             * actually reading the binary from disk. */
            cursor_show();
            loader_run(e->name, "");
        } else {
            cursor_hide();
            wm_open_terminal_with(e->name);
        }
        load_dir();   /* might have created/modified files */
        paint_executive();
        return;
    }
    /* .BMP / .JPG / .JPEG — open in WINIMG. The viewer reads files
     * via fs_read_file which uses the kernel-wide "app cwd". The
     * loader sets app_cwd to wherever the .BIN lives (so it can find
     * WINIMG.BIN under /APPS). To also let the viewer see the user's
     * image, we temporarily copy the image into /APPS, run, then
     * delete. */
    int is_image = has_suffix_icase(e->name, ".BMP")  ||
                   has_suffix_icase(e->name, ".JPG")  ||
                   has_suffix_icase(e->name, ".JPEG") ||
                   has_suffix_icase(e->name, ".ANI");
    if (is_image) {
        uint16_t apps = fs_find_dir(0, "APPS");
        if (apps != 0xFFFF) {
            static char shuttle[1024 * 1024];   /* 1 MiB shuttle */
            int sz = fs_read_in(cwd_cluster(), e->name,
                                shuttle, sizeof(shuttle));
            if (sz >= 0) {
                (void)fs_delete(apps, e->name);
                fs_create_file(apps, e->name, shuttle, (uint32_t)sz);
                fs_set_app_cwd(apps);
                cursor_show();
                loader_run("WINIMG.BIN", e->name);
                fs_delete(apps, e->name);
            }
        }
        paint_executive();
        return;
    }
    /* TXT or other: open in a terminal window via TYPE. */
    if (has_suffix_icase(e->name, ".TXT")) {
        cursor_hide();
        fs_set_app_cwd(cwd_cluster());
        wm_open_terminal_with(e->name);   /* terminal will TYPE then wait */
        paint_executive();
        return;
    }
    /* Audio/video — recognised but no decoders yet. Surface a
     * friendly "not supported" dialog. */
    if (has_suffix_icase(e->name, ".MP3") ||
        has_suffix_icase(e->name, ".MP4")) {
        sound_chord_error();
        about_dialog();   /* close enough as a placeholder for now;
                           * a dedicated dialog comes when we have
                           * real audio. */
        paint_executive();
        return;
    }
}

/* ---- event loop ------------------------------------------------ */

static int hit_row(int mx, int my) {
    (void)mx;
    if (my < LIST_TOP || my >= LIST_BOTTOM) return -1;
    int row = (my - LIST_TOP) / ROW_H;
    int idx = g_scroll + row;
    if (idx < 0 || idx >= g_n_entries) return -1;
    return idx;
}

void desktop_loop(void) {
    if (!fb_present()) return;
    wm_set_bg_repaint(paint_executive);
    /* BoxOS 2.0: no in-line file list any more. The desktop is just
     * a wallpaper + menu bar; file browsing lives in WINFILES.BIN.
     * Keeping g_n_entries = 0 keeps the legacy row-click handlers
     * harmless if a stray click lands in what used to be the list
     * area. */
    g_n_entries = 0;
    paint_executive();

    /* Arise boot fork — check for the installed marker (written by
     * SETUP.BIN at the end of the install wizard). Absent means we're
     * on fresh live media, so we launch Setup. Present means a real
     * install: hand off to the login screen first.
     *
     * INSTALL.CFG lives in /SYS/. WINLOGIN.BIN itself lives in /APPS.
     * The launcher reads the BIN from /APPS but we override the new
     * task's cwd to /SYS so the login screen's bos_read_file picks
     * up INSTALL.CFG. */
    /* Boot fork: if /SYS/SETUP.DONE is missing the OS hasn't been
     * set up yet — auto-launch the (now merged, single-reboot)
     * Setup wizard. Once Setup writes SETUP.DONE (to a writable
     * boot disk for in-place installs, or to the target drive for
     * CD-installs), subsequent boots go straight to the login
     * screen. If the disk is read-only and there's no target, Setup
     * still runs but its install step reports the error so you know
     * to fix the UTM config; Esc on the wizard drops to the desktop.
     *
     * The Setup wizard is still reachable from the Start menu / the
     * `SETUP` shell command after first boot. */
    {
        uint16_t sys  = fs_find_dir(0, "SYS");
        int setup_done = 0;
        if (sys != 0xFFFF) {
            static char probe[4];
            setup_done = fs_read_in(sys, "SETUP.DONE", probe, sizeof(probe)) >= 0;
        }
        uint16_t apps = fs_find_dir(0, "APPS");
        if (apps != 0xFFFF) {
            uint16_t prev_cwd = fs_get_app_cwd();
            fs_set_app_cwd(apps);
            const char* first = setup_done ? "WINLOGIN.BIN" : "SETUP.BIN";
            task_id tid = loader_spawn(first, "");
            fs_set_app_cwd(prev_cwd);
            /* WINLOGIN reads /SYS/INSTALL.CFG, so it wants /SYS as
             * its cwd. SETUP.BIN's writes also go through /SYS-aware
             * helpers, so /SYS works for it too. */
            if (tid >= 0 && sys != 0xFFFF)
                task_set_cwd_for(tid, sys);
        }
    }

    int      last_clicked = -1;
    uint64_t last_click_ms = 0;
    uint64_t last_clock_s  = 0;

    for (;;) {
        /* Live clock: redraw the menu-bar time once a second without
         * touching anything else. */
        uint64_t now_s = timer_ms() / 1000;
        if (now_s != last_clock_s) {
            last_clock_s = now_s;
            wm_paint_clock(now_s);
        }

        struct gui_event ev;
        if (!gui_poll_event(&ev)) {
            timer_sleep_ms(15);
            continue;
        }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned ascii = ev.arg1 & 0xFF;
            if (ascii == 27) return;                        /* Esc -> shell */
            /* Arrow keys: 0x80 up, 0x81 down (emitted by keyboard.c
             * for the extended PS/2 scancodes). */
            if (ascii == 0x80) {                            /* Up */
                if (g_sel > 0) {
                    int prev = g_sel; g_sel--;
                    int prev_y = LIST_TOP + (prev - g_scroll) * ROW_H;
                    int new_y  = LIST_TOP + (g_sel - g_scroll) * ROW_H;
                    paint_row(prev, prev_y, 0);
                    paint_row(g_sel, new_y, 1);
                    paint_status();
                    cursor_show();
                }
                continue;
            }
            if (ascii == 0x81) {                            /* Down */
                if (g_sel + 1 < g_n_entries) {
                    int prev = g_sel; g_sel++;
                    int prev_y = LIST_TOP + (prev - g_scroll) * ROW_H;
                    int new_y  = LIST_TOP + (g_sel - g_scroll) * ROW_H;
                    paint_row(prev, prev_y, 0);
                    paint_row(g_sel, new_y, 1);
                    paint_status();
                    cursor_show();
                }
                continue;
            }
            /* F12 = Print Screen — save framebuffer to /DOCS. */
            if (ascii == 0x85) {
                cursor_set_busy(1);
                print_screen_save();
                cursor_set_busy(0);
                sound_chord_ok();
                continue;
            }
            if (ascii == '\n' || ascii == '\r') {
                if (g_sel >= 0 && g_sel < g_n_entries)
                    launch(&g_entries[g_sel]);
                last_clicked = -1;
                continue;
            }
            /* Backspace = up directory. */
            if (ascii == 8) {
                if (g_cwd_depth > 0) {
                    g_cwd_depth--;
                    load_dir();
                    paint_executive();
                }
                continue;
            }
            /* Per-selection shortcuts. */
            if (g_sel >= 0 && g_sel < g_n_entries) {
                struct fat12_entry* e = &g_entries[g_sel];
                if (ascii == 'r' || ascii == 'R') {
                    /* r = run/open */
                    launch(e);
                    continue;
                }
                if (ascii == 'i' || ascii == 'I') {
                    info_dialog(e);
                    paint_executive();
                    continue;
                }
                if (ascii == 'n' || ascii == 'N') {
                    run_ctx_action(CTX_RENAME, e);
                    continue;
                }
                if (ascii == 'd' || ascii == 'D') {
                    run_ctx_action(CTX_DELETE, e);
                    continue;
                }
                if (ascii == 'c' || ascii == 'C') {
                    run_ctx_action(CTX_COPY, e);
                    continue;
                }
                if (ascii == 'v' || ascii == 'V') {
                    run_ctx_action(CTX_PASTE, e);
                    continue;
                }
                if (ascii == 'e' || ascii == 'E') {
                    if (has_suffix_icase(e->name, ".TXT")) {
                        run_ctx_action(CTX_EDIT, e);
                    }
                    continue;
                }
            }
            /* Selection-independent shortcuts. */
            if (ascii == 'g' || ascii == 'G') {
                /* g = refresh (FAT12 doesn't change underneath us
                 * but useful after creating files). */
                load_dir();
                paint_executive();
                continue;
            }
        }

        /* Right-click on a file row → context menu. */
        if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 2)) {
            int idx = hit_row(ev.x, ev.y);
            struct ctx_item items[8];
            int n;
            if (idx >= 0) {
                /* Click on a file row: select it first so the user
                 * sees what the menu acts on. */
                int prev = g_sel;
                g_sel = idx;
                int prev_y = LIST_TOP + (prev - g_scroll) * ROW_H;
                int new_y  = LIST_TOP + (idx  - g_scroll) * ROW_H;
                paint_row(prev, prev_y, 0);
                paint_row(idx,  new_y,  1);
                cursor_show();
                n = ctx_items_for(&g_entries[idx], items);
            } else {
                /* Empty space: only Paste is meaningful. */
                if (g_clip_size < 0) continue;
                items[0] = (struct ctx_item){ "Paste", CTX_PASTE, 1 };
                n = 1;
            }
            if (n == 0) continue;
            int chosen = context_menu_loop(ev.x, ev.y, items, n);
            paint_executive();
            if (chosen) {
                if (chosen == CTX_PASTE && idx < 0) {
                    run_ctx_action(chosen, &g_entries[0]);
                } else if (idx >= 0) {
                    run_ctx_action(chosen, &g_entries[idx]);
                }
            }
            continue;
        }

        if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            /* Arise: Start button click → launch Program Manager
             * (the de facto Start menu for now). The launcher
             * searches /APPS and /GAMES, so a bare 8.3 name is
             * enough. */
            if (wm_hit_start_button(ev.x, ev.y)) {
                (void)loader_spawn("WINPROG.BIN", "");
                continue;
            }
            /* Arise has no top menu bar — bypass the v1.0 Executive
             * file-list / menu-bar interactions entirely. Clicks on
             * the wallpaper are no-ops. */
            continue;
            /* fallthrough — unreachable, kept so the v1.0 Executive
             * code below still compiles in case we want a recovery
             * mode that re-uses it. */
            if (ev.y < TITLE_H) continue;
            if (ev.y < TITLE_H + MENUBAR_H) {
                /* Menu bar — File opens a dropdown with real
                 * commands; View just refreshes; Special pops About. */
                if (ev.x >= MENU_FILE_X && ev.x < MENU_FILE_X + MENU_FILE_W) {
                    int id = file_menu_loop();
                    if (id == MI_LOG_OUT) return;     /* fall to shell */
                    paint_executive();
                    if (id > 0) run_menu_action(id);
                } else if (ev.x >= MENU_VIEW_X &&
                           ev.x < MENU_VIEW_X + MENU_VIEW_W) {
                    int id = view_menu_loop();
                    paint_executive();
                    if (id == MI_SORT_NAME) { g_sort_mode = 0; load_dir(); paint_executive(); }
                    if (id == MI_SORT_SIZE) { g_sort_mode = 1; load_dir(); paint_executive(); }
                    if (id == MI_REFRESH)   { load_dir(); paint_executive(); }
                } else if (ev.x >= MENU_SPECIAL_X &&
                           ev.x < MENU_SPECIAL_X + MENU_SPECIAL_W) {
                    /* Special is now the Settings shortcut. Always
                     * launches WINSETT regardless of the user's
                     * current dir; we point app_cwd at /APPS first. */
                    uint16_t apps = fs_find_dir(0, "APPS");
                    if (apps != 0xFFFF) {
                        fs_set_app_cwd(apps);
                        cursor_show();
                        loader_run("WINSETT.BIN", "");
                    }
                    paint_executive();
                }
                continue;
            }
            int idx = hit_row(ev.x, ev.y);
            if (idx < 0) continue;
            uint64_t now = timer_ms();
            if (idx == last_clicked && (now - last_click_ms) < 500) {
                /* double-click */
                launch(&g_entries[idx]);
                last_clicked = -1;
            } else {
                int prev = g_sel;
                g_sel = idx;
                /* Repaint the two affected rows only. */
                int prev_y = LIST_TOP + (prev - g_scroll) * ROW_H;
                int new_y  = LIST_TOP + (idx  - g_scroll) * ROW_H;
                paint_row(prev, prev_y, 0);
                paint_row(idx,  new_y,  1);
                cursor_show();
                last_clicked = idx;
                last_click_ms = now;
            }
        }
    }
}
