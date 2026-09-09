/* WINSETT — the Settings app. Single front-end for everything
 * configurable in BoxOS:
 *
 *   Appearance — colour theme + menu-bar title presets
 *   Sound      — PC speaker mute / test tone
 *   System     — uptime, heap usage, version, build info
 *   Drivers    — every subsystem with status
 *   Power      — Reboot + Halt
 *
 * Each tab is a panel painted on the right side of the window. The
 * left strip is the tab bar; clicking switches panels. */

#include "boxos_app.h"

#define WIN_W 520
#define WIN_H 340

#define TAB_W 110
#define TAB_H 28
#define TAB_N 5

#define TAB_APPEARANCE 0
#define TAB_SOUND      1
#define TAB_SYSTEM     2
#define TAB_DRIVERS    3
#define TAB_POWER      4

static int g_tab = TAB_APPEARANCE;
static int g_win = -1;
static int g_cw, g_ch;

static const int win_x = 60;
static const int win_y = 60;
static const int chrome_offset_x = 1;
static const int chrome_offset_y = 13;

static int strlen_l(const char* s) { int n = 0; while (s && s[n]) n++; return n; }

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

static void u64_to_str(uint64_t v, char* out) {
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (int)(v % 10); v /= 10; }
    int o = 0; while (t) out[o++] = tmp[--t]; out[o] = 0;
}

static void clear_pane(void) {
    gui_fill_rect(g_win, TAB_W, 0, g_cw - TAB_W, g_ch, 7);
}

/* ---- tab strip ------------------------------------------------- */

static const char* g_tab_labels[TAB_N] = {
    "Appearance", "Sound", "System", "Drivers", "Power",
};

static void paint_tabs(void) {
    gui_fill_rect(g_win, 0, 0, TAB_W, g_ch, 8);
    for (int i = 0; i < TAB_N; i++) {
        int y = i * TAB_H;
        int sel = (i == g_tab);
        gui_fill_rect(g_win, 0, y, TAB_W, TAB_H, sel ? 7 : 8);
        int label_n = strlen_l(g_tab_labels[i]);
        gui_text(g_win, (TAB_W - label_n * 8) / 2, y + 10,
                 g_tab_labels[i], sel ? 0 : 15, sel ? 7 : 8);
    }
}

/* ---- Appearance ----------------------------------------------- */

static const char* g_title_presets[] = {
    "BoxOS Arise",
    "RadiDOS 3.0",
    "Hello, world",
    "the OS that boots",
};
#define N_TITLES ((int)(sizeof(g_title_presets) / sizeof(g_title_presets[0])))
static int g_title_idx = 0;

static void paint_appearance(void) {
    clear_pane();
    gui_text(g_win, TAB_W + 16, 12, "Theme:", 0, 7);
    int active = theme_index();
    int n = theme_count();
    for (int i = 0; i < n; i++) {
        char name[40];
        theme_name(i, name, sizeof(name));
        int row_y = 36 + i * 24;
        int sel = (i == active);
        gui_fill_rect(g_win, TAB_W + 16, row_y, 16, 16, sel ? 9 : 7);
        gui_fill_rect(g_win, TAB_W + 18, row_y + 2, 12, 12, sel ? 15 : 0);
        gui_text(g_win, TAB_W + 40, row_y + 4, name, 0, 7);
        if (sel) gui_text(g_win, TAB_W + 200, row_y + 4, "(active)", 4, 7);
    }
    /* Menu-bar title section. */
    gui_text(g_win, TAB_W + 16, 36 + n * 24 + 16, "Menu bar title:", 0, 7);
    int row_y = 36 + n * 24 + 36;
    gui_fill_rect(g_win, TAB_W + 16, row_y, g_cw - TAB_W - 32, 24, 15);
    gui_text(g_win, TAB_W + 24, row_y + 8, g_title_presets[g_title_idx], 0, 15);
    gui_text(g_win, TAB_W + 16, row_y + 32,
             "click the bar to cycle to the next preset.", 0, 7);
}

static int hit_appearance_theme(int rx, int ry, int* out_row) {
    if (rx < TAB_W + 16 || rx >= TAB_W + 220) return 0;
    if (ry < 36) return 0;
    int row = (ry - 36) / 24;
    if (row < 0 || row >= theme_count()) return 0;
    *out_row = row;
    return 1;
}

static int hit_appearance_title(int rx, int ry) {
    int n = theme_count();
    int row_y = 36 + n * 24 + 36;
    if (rx < TAB_W + 16 || rx >= g_cw - 16) return 0;
    if (ry < row_y || ry >= row_y + 24) return 0;
    return 1;
}

/* ---- Sound ----------------------------------------------------- */

static void paint_sound(void) {
    clear_pane();
    gui_text(g_win, TAB_W + 16, 16, "PC SPEAKER", 0, 7);
    /* Mute toggle: on/off state shown as a colored pill. */
    int on = sound_enabled();
    gui_fill_rect(g_win, TAB_W + 16, 44, 110, 24, on ? 2 : 8);
    gui_text(g_win, TAB_W + 40, 52, on ? "ENABLED" : "MUTED", 15, on ? 2 : 8);
    gui_text(g_win, TAB_W + 134, 52, "click to toggle", 0, 7);

    gui_fill_rect(g_win, TAB_W + 16, 84, 110, 24, 11);
    gui_text(g_win, TAB_W + 46, 92, "TEST TONE", 0, 11);

    gui_fill_rect(g_win, TAB_W + 16, 124, 110, 24, 14);
    gui_text(g_win, TAB_W + 50, 132, "BELL", 0, 14);

    gui_fill_rect(g_win, TAB_W + 16, 164, 110, 24, 12);
    gui_text(g_win, TAB_W + 42, 172, "ERROR", 0, 12);

    /* Layered help text: the most common reason "I press TEST and
     * hear nothing" isn't a code bug, it's that QEMU/UTM don't route
     * the PC speaker to the host without an explicit audiodev. */
    gui_text(g_win, TAB_W + 16, 220,
             "single-voice square wave (PIT ch2 + port 0x61).",
             0, 7);
    gui_text(g_win, TAB_W + 16, 236,
             "no sound on a VM?  the host needs to forward the",
             0, 7);
    gui_text(g_win, TAB_W + 16, 252,
             "PC speaker.  In QEMU: -audiodev coreaudio,id=snd",
             0, 7);
    gui_text(g_win, TAB_W + 16, 268,
             "and -machine pcspk-audiodev=snd  (the BoxOS Makefile",
             0, 7);
    gui_text(g_win, TAB_W + 16, 284,
             "now does this for `make run`).  In UTM, enable Sound", 0, 7);
    gui_text(g_win, TAB_W + 16, g_ch - 26,
             "in the VM's Edit > Sound panel and reboot.", 0, 7);
}

static int sound_btn(int rx, int ry, int row) {
    int by = 44 + row * 40;
    return rx >= TAB_W + 16 && rx < TAB_W + 16 + 110 &&
           ry >= by && ry < by + 24;
}

/* ---- System ---------------------------------------------------- */

static void paint_system(void) {
    clear_pane();
    gui_text(g_win, TAB_W + 16,  12, "BoxOS Arise (v3.0)",       0, 7);
    gui_text(g_win, TAB_W + 16,  32, "RadiDOS 3.0 shell.",        0, 7);
    gui_text(g_win, TAB_W + 16,  56, "Hand-rolled 64-bit kernel + apps", 0, 7);
    gui_text(g_win, TAB_W + 16,  72, "in C and asm.  No external libs.",  0, 7);

    /* Uptime */
    uint64_t s = ticks_ms() / 1000;
    int hh = (int)(s / 3600), mm = (int)((s / 60) % 60), ss = (int)(s % 60);
    char up[32]; int p = 0;
    char num[12];
    int_to_str(hh, num); int j = 0; while (num[j]) up[p++] = num[j++];
    up[p++] = 'h'; up[p++] = ' ';
    int_to_str(mm, num); j = 0;     while (num[j]) up[p++] = num[j++];
    up[p++] = 'm'; up[p++] = ' ';
    int_to_str(ss, num); j = 0;     while (num[j]) up[p++] = num[j++];
    up[p++] = 's'; up[p] = 0;

    gui_text(g_win, TAB_W + 16, 110, "uptime:", 0, 7);
    gui_text(g_win, TAB_W + 96, 110, up,        4, 7);

    /* Heap */
    uint64_t hu = heap_used(), ht = heap_total();
    char buf[64]; int b = 0;
    u64_to_str(hu, num); j = 0; while (num[j]) buf[b++] = num[j++];
    buf[b++] = ' '; buf[b++] = '/'; buf[b++] = ' ';
    u64_to_str(ht, num); j = 0; while (num[j]) buf[b++] = num[j++];
    const char* tail = " bytes";
    j = 0; while (tail[j]) buf[b++] = tail[j++];
    buf[b] = 0;
    gui_text(g_win, TAB_W + 16, 130, "heap:",   0, 7);
    gui_text(g_win, TAB_W + 96, 130, buf,       4, 7);

    /* Memory map cheats */
    gui_text(g_win, TAB_W + 16, 158, "kernel @ 0x8E00",       0, 7);
    gui_text(g_win, TAB_W + 16, 174, "apps   @ 0x200000",     0, 7);
    gui_text(g_win, TAB_W + 16, 190, "fb     @ 0xFD000000",   0, 7);

    gui_text(g_win, TAB_W + 16, g_ch - 26,
             "Esc closes Settings.", 0, 7);
}

/* ---- Drivers --------------------------------------------------- */

struct drv_row { const char* name; const char* status; const char* note; };

static const struct drv_row g_drivers[] = {
    { "FRAMEBUFFER", "OK",      "640x480x8 @ 0xFD000000 (Bochs VBE)" },
    { "PS/2 KEYBD",  "OK",      "scancode set 1, US QWERTY" },
    { "PS/2 MOUSE",  "OK",      "IntelliMouse 4-byte (wheel)" },
    { "FAT12",       "MOUNTED", "drive 0, 32 MiB live + factory" },
    { "PIT TIMER",   "OK",      "ch 0 @ 100 Hz, ch 2 = sound" },
    { "IDT/PIC",     "OK",      "IRQ0/1/12/5 + syscall int 0x80" },
    { "PC SPEAKER",  NULL,      "single-voice square wave" },
    { "SB16",        NULL,      "8-bit DMA1 + IRQ5 PCM (set sound = SB16)" },
    { "HEAP",        "OK",      "16 MiB bump @ 0x1000000" },
};
#define N_DRIVERS ((int)(sizeof(g_drivers) / sizeof(g_drivers[0])))

static void paint_drivers(void) {
    clear_pane();
    gui_text(g_win, TAB_W + 16, 12,
             "DRIVER          STATUS    NOTES", 0, 7);
    gui_fill_rect(g_win, TAB_W + 16, 28, g_cw - TAB_W - 32, 1, 0);
    for (int i = 0; i < N_DRIVERS; i++) {
        int y = 36 + i * 18;
        gui_text(g_win, TAB_W + 16, y, g_drivers[i].name, 0, 7);
        const char* st = g_drivers[i].status;
        if (!st) {
            if (g_drivers[i].name[0] == 'P')      /* PC SPEAKER */
                st = sound_enabled() ? "ON" : "MUTED";
            else                                  /* SB16 */
                st = bos_sb16_present() ? "OK" : "ABSENT";
        }
        uint8_t fg = 2;        /* green */
        if (st[0] == 'M' || st[0] == 'A') fg = 14;
        gui_text(g_win, TAB_W + 16 + 16 * 8, y, st, fg, 7);
        gui_text(g_win, TAB_W + 16 + 26 * 8, y, g_drivers[i].note, 0, 7);
    }
    gui_text(g_win, TAB_W + 16, g_ch - 26,
             "click PC SPEAKER row to mute / unmute.", 0, 7);
}

static int hit_drivers_speaker(int rx, int ry) {
    if (rx < TAB_W + 16 || rx >= TAB_W + 16 + 16 * 8 + 4) return 0;
    int row = (ry - 36) / 18;
    return row == 6;
}

/* ---- Power ----------------------------------------------------- */

static int g_power_confirm = 0;     /* 1=reboot, 2=halt; 0 = idle    */

static void paint_power(void) {
    clear_pane();
    gui_text(g_win, TAB_W + 16, 16, "POWER OPTIONS", 0, 7);
    gui_text(g_win, TAB_W + 16, 36,
             "stop the OS or reset the machine.", 0, 7);

    /* Reboot button (with manual 1-px border). */
    gui_fill_rect(g_win, TAB_W + 16, 76, 140, 36, 14);
    gui_fill_rect(g_win, TAB_W + 16, 76, 140, 1, 0);
    gui_fill_rect(g_win, TAB_W + 16, 76 + 35, 140, 1, 0);
    gui_fill_rect(g_win, TAB_W + 16, 76, 1, 36, 0);
    gui_fill_rect(g_win, TAB_W + 16 + 139, 76, 1, 36, 0);
    gui_text(g_win, TAB_W + 50, 90, "REBOOT", 0, 14);

    /* Halt button. */
    gui_fill_rect(g_win, TAB_W + 168, 76, 140, 36, 12);
    gui_fill_rect(g_win, TAB_W + 168, 76, 140, 1, 0);
    gui_fill_rect(g_win, TAB_W + 168, 76 + 35, 140, 1, 0);
    gui_fill_rect(g_win, TAB_W + 168, 76, 1, 36, 0);
    gui_fill_rect(g_win, TAB_W + 168 + 139, 76, 1, 36, 0);
    gui_text(g_win, TAB_W + 206, 90, "HALT", 0, 12);

    if (g_power_confirm == 1) {
        gui_text(g_win, TAB_W + 16, 130,
                 "click REBOOT again to confirm.", 0, 7);
    } else if (g_power_confirm == 2) {
        gui_text(g_win, TAB_W + 16, 130,
                 "click HALT again to confirm.", 0, 7);
    } else {
        gui_text(g_win, TAB_W + 16, 130,
                 "click once to arm, again to confirm.", 0, 7);
    }

    gui_text(g_win, TAB_W + 16, g_ch - 26,
             "REBOOT pulses the keyboard controller.  HALT cli;hlt forever.",
             0, 7);
}

static int hit_power(int rx, int ry, int* which) {
    if (ry < 76 || ry >= 76 + 36) return 0;
    if (rx >= TAB_W + 16  && rx < TAB_W + 16 + 140) { *which = 1; return 1; }
    if (rx >= TAB_W + 168 && rx < TAB_W + 168 + 140){ *which = 2; return 1; }
    return 0;
}

/* ---- main ----------------------------------------------------- */

static void repaint(void) {
    paint_tabs();
    switch (g_tab) {
        case TAB_APPEARANCE: paint_appearance(); break;
        case TAB_SOUND:      paint_sound();      break;
        case TAB_SYSTEM:     paint_system();     break;
        case TAB_DRIVERS:    paint_drivers();    break;
        case TAB_POWER:      paint_power();      break;
    }
}

int app_main(const char* args) {
    (void)args;
    g_win = gui_open_window("Settings", win_x, win_y, WIN_W, WIN_H);
    if (g_win < 0) { bos_puts("WINSETT: cannot open window\n"); return 1; }
    gui_size(g_win, &g_cw, &g_ch);
    repaint();

    uint64_t last_system_refresh = 0;

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) {
            if (g_tab == TAB_SYSTEM) {
                uint64_t now = ticks_ms();
                if (now - last_system_refresh > 1000) {
                    last_system_refresh = now;
                    paint_system();
                }
            }
            sleep_ms(20);
            continue;
        }
        if (ev.type == GUI_EV_PAINT) { repaint(); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned ascii = ev.arg1 & 0xFF;
            if (ascii == 27) { gui_close_window(g_win); return 0; }
            if (ascii == 't' || ascii == 'T') {
                g_tab = (g_tab + 1) % TAB_N;
                g_power_confirm = 0;
                repaint();
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - chrome_offset_x;
            int ry = ev.y - win_y - chrome_offset_y;
            if (rx < 0 || ry < 0) continue;
            /* tab strip */
            if (rx < TAB_W) {
                int t = ry / TAB_H;
                if (t >= 0 && t < TAB_N) {
                    g_tab = t;
                    g_power_confirm = 0;
                    repaint();
                }
                continue;
            }
            /* per-tab dispatch */
            if (g_tab == TAB_APPEARANCE) {
                int row;
                if (hit_appearance_theme(rx, ry, &row)) {
                    theme_set(row);
                    sound_ok();
                    /* Theme change reset the WM, which wipes our
                     * window. Reopen. */
                    gui_close_window(g_win);
                    g_win = gui_open_window("Settings",
                                            win_x, win_y, WIN_W, WIN_H);
                    if (g_win < 0) return 0;
                    gui_size(g_win, &g_cw, &g_ch);
                    repaint();
                    continue;
                }
                if (hit_appearance_title(rx, ry)) {
                    g_title_idx = (g_title_idx + 1) % N_TITLES;
                    wm_set_title(g_title_presets[g_title_idx]);
                    /* WM re-init wipes our window as above. */
                    gui_close_window(g_win);
                    g_win = gui_open_window("Settings",
                                            win_x, win_y, WIN_W, WIN_H);
                    if (g_win < 0) return 0;
                    gui_size(g_win, &g_cw, &g_ch);
                    repaint();
                    continue;
                }
            } else if (g_tab == TAB_SOUND) {
                if (sound_btn(rx, ry, 0)) {
                    sound_enable(!sound_enabled());
                    paint_sound();
                } else if (sound_btn(rx, ry, 1)) {
                    sound_beep(880, 120);
                } else if (sound_btn(rx, ry, 2)) {
                    sound_bell();
                } else if (sound_btn(rx, ry, 3)) {
                    sound_error();
                }
            } else if (g_tab == TAB_DRIVERS) {
                if (hit_drivers_speaker(rx, ry)) {
                    sound_enable(!sound_enabled());
                    paint_drivers();
                }
            } else if (g_tab == TAB_POWER) {
                int which;
                if (hit_power(rx, ry, &which)) {
                    if (g_power_confirm == which) {
                        if (which == 1) reboot_now();
                        else            halt_now();
                    } else {
                        g_power_confirm = which;
                        paint_power();
                    }
                }
            }
        }
    }
}
