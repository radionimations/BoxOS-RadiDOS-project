/* SETUP — first-boot installer for BoxOS Arise.
 *
 * Runs as a normal windowed app, but is auto-launched by the kernel
 * boot path when /SYS/SYSTEM32/INSTALL.CFG is missing (= live media,
 * first boot). Walks the user through a six-page wizard, writes the
 * INSTALL.CFG marker at the end, and reboots into the desktop.
 *
 * Real disk-to-disk file copy + scandisk + mkfs are a follow-up; this
 * version runs in place on the live-media disk and just flips the
 * "installed" flag once the wizard completes. The "Copying files"
 * page is a deliberately-paced progress bar with a fun-fact ticker. */

#include "boxos_app.h"

#define WIN_W 520
#define WIN_H 360
#define WIN_X ((640 - WIN_W) / 2)
#define WIN_Y ((480 - WIN_H) / 2 - 12)

#define BTN_H  24
#define BTN_W  90

/* One continuous wizard — install + post-install ("Phase 4") merged
 * so there's exactly one reboot, hence one eject / driver-state
 * change. */
#define PAGE_WELCOME   0
#define PAGE_USER      1
#define PAGE_DEVICES   2
#define PAGE_NETWORK   3
#define PAGE_TARGET    4
#define PAGE_COMPONENT 5
#define PAGE_INSTALL   6   /* cross-disk byte copy + marker write     */
#define PAGE_FINALIZE  7   /* "configuring components" beat           */
#define PAGE_TIMEZONE  8   /* pick timezone                           */
#define PAGE_DONE      9   /* Finish -> power off; relaunch = desktop  */
#define N_PAGES       10

static int win;
static int cw, ch;
static int page = PAGE_WELCOME;

static char user_name[32]    = "User";
static char user_company[32] = "";
static int  edit_target = 0;          /* 0 = name, 1 = company */
static int  edit_pos    = 0;

/* Cross-disk install state. target_idx = -1 means "no separate
 * target drive, install in place on the boot disk" (only works if
 * the boot disk is writable — a CD/El Torito attach can't store the
 * install). Anything else is the (bus<<1)|drive of the target. */
static int      target_idx        = -1;
static int      install_started   = 0;
static int      install_done      = 0;   /* byte copy finished        */
static int      install_error     = 0;
static int      install_percent_v = 0;
static int      last_start_rc     = 0;   /* return from bos_inst_start  */
static int      finalize_started  = 0;
static int      finalize_done     = 0;
static uint64_t finalize_start_ms = 0;

static const char* tz_names[] = {
    "(UTC-08) Pacific",
    "(UTC-05) Eastern",
    "(UTC+00) London / GMT",
    "(UTC+02) Helsinki / Vilnius",
    "(UTC+03) Moscow",
    "(UTC+05:30) New Delhi",
    "(UTC+08) Beijing / Singapore",
    "(UTC+09) Tokyo",
};
#define N_TZ ((int)(sizeof(tz_names)/sizeof(*tz_names)))
static int tz_pick = 3;

/* Fun facts cycled on the "Copying files" page. Deliberately silent
 * about how this OS gets written — the user's spec asked us not to
 * out the model. */
static const char* facts[] = {
    "BoxOS boots without a single BIOS call after stage 2.",
    "Every window has its own cooperative task and PML4.",
    "The font is 8x8 — the same shape MS-DOS used in 1981.",
    "DOOM ships as a 64-bit flat binary at virtual 0x200000.",
    "FAT12 caps the live filesystem at 32 MiB. Fits in L2 on a M1.",
    "The PIT speaker is gated by two bits of port 0x61.",
    "Arise's z-order clipping is computed per fill_rect call.",
    "Save-under for the outline drag is four 1-px strips.",
    "Mode 13h is 320x200x256, blitted into 640x400 with 40px bars.",
    "The Bochs VBE framebuffer lives at physical 0xFD000000.",
    "RadiDOS commands map 1:1 to fs_* calls in fat12.c.",
    "There's exactly one syscall vector: int 0x80.",
};
#define N_FACTS ((int)(sizeof(facts) / sizeof(facts[0])))
static int fact_idx = 0;

static int strlen_l(const char* s) { int n = 0; while (s && s[n]) n++; return n; }

/* ---- painting -------------------------------------------------- */

static int content_top(void)   { return 56; }
static int content_left(void)  { return 16; }
static int content_right(void) { return cw - 16; }
static int button_y(void)      { return ch - BTN_H - 12; }

static void draw_title_bar(const char* page_title) {
    /* Top banner — orange brand strip with the page title. */
    gui_fill_rect(win, 0, 0, cw, 44, 214);            /* orange */
    gui_fill_rect(win, 0, 43, cw, 1, 94);             /* brown divider */
    gui_text(win, 16, 6,  "BoxOS Arise Setup",        0, 214);
    gui_text(win, 16, 24, page_title,                 0, 214);
    gui_fill_rect(win, 0, 44, cw, 1, 15);             /* highlight */
}

static void draw_button(int x, int y, int w, int label_pad, const char* label,
                        int enabled) {
    uint8_t bg = enabled ? 7 : 8;
    gui_fill_rect(win, x, y, w, BTN_H, bg);
    gui_fill_rect(win, x, y, w, 1, 15);
    gui_fill_rect(win, x, y, 1, BTN_H, 15);
    gui_fill_rect(win, x, y + BTN_H - 1, w, 1, 0);
    gui_fill_rect(win, x + w - 1, y, 1, BTN_H, 0);
    gui_text(win, x + label_pad, y + 8, label, enabled ? 0 : 15, bg);
}

static int back_btn_x(void)   { return 16; }
static int next_btn_x(void)   { return cw - BTN_W - 16; }
static int cancel_btn_x(void) { return cw - 2 * BTN_W - 24; }

static int can_back(void) {
    return page > PAGE_WELCOME && page < PAGE_INSTALL;
}
static int next_enabled(void) {
    if (page == PAGE_TARGET)   return target_idx >= 0;
    if (page == PAGE_INSTALL)  return install_done && !install_error;
    if (page == PAGE_FINALIZE) return finalize_done;
    return page <= PAGE_DONE;
}

static void draw_buttons(void) {
    int by = button_y();
    gui_fill_rect(win, 0, by - 8, cw, 1, 8);
    draw_button(back_btn_x(), by, BTN_W, 28, "< Back", can_back());
    if (page == PAGE_DONE) {
        draw_button(next_btn_x(), by, BTN_W, 18, "Finish", 1);
    } else if (page == PAGE_COMPONENT) {
        draw_button(next_btn_x(), by, BTN_W, 14, "Install", 1);
    } else {
        draw_button(next_btn_x(), by, BTN_W, 26, "Next >", next_enabled());
    }
    /* Cancel only available before the install kicks off. */
    if (page < PAGE_INSTALL)
        draw_button(cancel_btn_x(), by, BTN_W, 22, "Cancel", 1);
}

static void wrap_lines(const char* text, int x, int y, int max_w, int fg) {
    int line_chars = max_w / 8;
    int i = 0;
    while (text[i]) {
        int line_len = 0, last_space = -1;
        while (text[i + line_len] && line_len < line_chars) {
            if (text[i + line_len] == '\n') break;
            if (text[i + line_len] == ' ') last_space = line_len;
            line_len++;
        }
        if (text[i + line_len] && text[i + line_len] != '\n' &&
            last_space > 0 && line_len == line_chars) {
            line_len = last_space;
        }
        for (int j = 0; j < line_len; j++) {
            char c[2] = { text[i + j], 0 };
            gui_text(win, x + j * 8, y, c, fg, 7);
        }
        i += line_len;
        while (text[i] == ' ' || text[i] == '\n') i++;
        y += 16;
    }
}

static void paint_text_field(int x, int y, int w, const char* label,
                             const char* val, int focused) {
    gui_text(win, x, y - 14, label, 0, 7);
    /* Sunken edit box. */
    gui_fill_rect(win, x, y, w, 20, 15);
    gui_fill_rect(win, x, y, w, 1, 0);
    gui_fill_rect(win, x, y, 1, 20, 0);
    gui_fill_rect(win, x, y + 19, w, 1, 15);
    gui_fill_rect(win, x + w - 1, y, 1, 20, 15);
    /* Render visible portion of value (up to fit-in-box chars). */
    int max_chars = (w - 8) / 8;
    int vlen = strlen_l(val);
    int start = vlen > max_chars ? vlen - max_chars : 0;
    gui_text(win, x + 4, y + 6, val + start, 0, 15);
    if (focused) {
        /* Cursor block at end. */
        int cx = x + 4 + (vlen - start) * 8;
        gui_fill_rect(win, cx, y + 4, 1, 12, 0);
    }
}

static void paint_progress(int x, int y, int w, int percent) {
    gui_fill_rect(win, x, y, w, 18, 15);
    gui_fill_rect(win, x, y, w, 1, 0);
    gui_fill_rect(win, x, y, 1, 18, 0);
    gui_fill_rect(win, x, y + 17, w, 1, 15);
    gui_fill_rect(win, x + w - 1, y, 1, 18, 15);
    int fill_w = (w - 4) * percent / 100;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > w - 4) fill_w = w - 4;
    gui_fill_rect(win, x + 2, y + 2, fill_w, 14, 1);
    /* Percentage label centred. */
    char buf[8]; int n = 0;
    int p = percent; if (p > 100) p = 100;
    if (p >= 100) { buf[n++] = '1'; buf[n++] = '0'; buf[n++] = '0'; }
    else if (p >= 10) { buf[n++] = (char)('0' + p / 10); buf[n++] = (char)('0' + p % 10); }
    else              { buf[n++] = (char)('0' + p); }
    buf[n++] = '%'; buf[n] = 0;
    int tx = x + (w - n * 8) / 2;
    gui_text(win, tx, y + 5, buf, 15, fill_w > (tx - x) ? 1 : 15);
}

/* ---- per-page painters ---------------------------------------- */

static void page_welcome(void) {
    draw_title_bar("Welcome");
    int x = content_left(), y = content_top();
    wrap_lines(
        "Welcome to BoxOS Arise.\n"
        "This setup wizard installs BoxOS onto this machine and "
        "captures the few things the OS needs to know about you: "
        "your name (which becomes the computer name), the hardware "
        "we should use, and a starting set of programs.\n"
        "Click Next to continue, or Cancel to drop to the RadiDOS "
        "shell instead.",
        x, y, content_right() - x, 0);
}

static void page_user(void) {
    draw_title_bar("User Information");
    int x = content_left();
    gui_text(win, x, content_top(),
             "Tell BoxOS who you are.  The name below becomes the",
             0, 7);
    gui_text(win, x, content_top() + 16,
             "computer name and the label on the login screen.",
             0, 7);
    paint_text_field(x, content_top() + 60, 360,
                     "Name", user_name, edit_target == 0);
    paint_text_field(x, content_top() + 110, 360,
                     "Company (optional)", user_company, edit_target == 1);
    gui_text(win, x, content_top() + 150,
             "Tab to switch fields. Type to edit.", 8, 7);
}

static void page_devices(void) {
    draw_title_bar("Devices");
    int x = content_left();
    wrap_lines(
        "Setup probed the system for hardware and found the "
        "following devices:",
        x, content_top(), content_right() - x, 0);
    int y = content_top() + 48;
    const char* rows[] = {
        "  CPU          x86_64 long-mode (CPUID)",
        "  Memory       at least 256 MiB (E820)",
        "  Display      Bochs/QEMU VBE 640x480x8",
        "  Keyboard     PS/2 set 1, US QWERTY",
        "  Mouse        PS/2 IntelliMouse",
        "  Storage      ATA PIO, FAT12 mounted",
        "  Sound        PC speaker + Sound Blaster 16 (if present)",
        "  Network      none detected",
    };
    for (int i = 0; i < (int)(sizeof(rows)/sizeof(rows[0])); i++)
        gui_text(win, x, y + i * 16, rows[i], 0, 7);
    gui_text(win, x, y + 8 * 16 + 8,
             "No devices the kernel doesn't already drive.", 8, 7);
}

/* DOS-style drive letters for the 4 ATA slots, just for display. */
static const char* drv_letters[4] = { "C:", "D:", "E:", "F:" };

static int  format_first = 0;     /* "Format the selected drive first" */

static int  target_drive_row_y(int row) { return content_top() + 52 + row * 18; }
static int  format_box_y(void)          { return content_top() + 52 + 4 * 18 + 12; }

/* Which drive rows on PAGE_TARGET can actually be picked: any present
 * drive, except the boot media when it's a read-only CD/El Torito. */
static int target_selectable(int i) {
    int mask = bos_inst_drives();
    if (!((mask >> i) & 1)) return 0;
    if (i == bos_inst_boot_drv() && !bos_boot_drive_writable()) return 0;
    return 1;
}

static void page_target(void) {
    draw_title_bar("Install Target");
    int x = content_left();
    wrap_lines(
        "Setup scanned this PC for fixed disks. Pick the drive to "
        "install BoxOS onto. A blank disk gets BoxOS copied onto it; "
        "a disk marked \"this disk\" is installed in place.",
        x, content_top(), content_right() - x, 0);

    int mask = bos_inst_drives();
    int boot = bos_inst_boot_drv();
    int boot_wr = bos_boot_drive_writable();

    /* If exactly one drive is selectable and nothing is picked yet,
     * pre-select it so the user can just hit Next. */
    if (target_idx < 0) {
        int sel_count = 0, only = -1;
        for (int i = 0; i < 4; i++) if (target_selectable(i)) { sel_count++; only = i; }
        if (sel_count == 1) target_idx = only;
    }

    for (int i = 0; i < 4; i++) {
        int present  = (mask >> i) & 1;
        int is_boot  = (i == boot);
        int pickable = target_selectable(i);
        int hit      = (i == target_idx);
        int y        = target_drive_row_y(i);
        char line[72]; int n = 0;
        const char* l = drv_letters[i];
        while (*l) line[n++] = *l++;
        line[n++] = ' '; line[n++] = ' ';
        const char* tag;
        if (!present)              tag = "(not present)";
        else if (is_boot && !boot_wr) tag = "(install media -- not a target)";
        else if (is_boot)          tag = "(this disk -- install in place)";
        else                       tag = "(empty -- install here)";
        while (*tag) line[n++] = *tag++;
        line[n] = 0;
        if (pickable) {
            gui_fill_rect(win, x - 2, y - 2, content_right() - x + 4, 18,
                          hit ? 9 : 7);
            gui_text(win, x, y + 2, line, hit ? 15 : 0, hit ? 9 : 7);
        } else {
            gui_text(win, x, y + 2, line, 8, 7);
        }
    }

    /* Format checkbox. */
    int fy = format_box_y();
    gui_fill_rect(win, x, fy, 14, 14, 15);
    gui_fill_rect(win, x, fy, 14, 1, 0);
    gui_fill_rect(win, x, fy, 1, 14, 0);
    if (format_first) {
        gui_fill_rect(win, x + 3, fy + 3, 8, 8, 4);   /* red check fill */
        gui_text(win, x + 3, fy + 2, "X", 15, 4);
    }
    gui_text(win, x + 22, fy + 2,
             "Format the selected drive first (wipes user data)", 0, 7);

    /* No writable drive anywhere? Tell the user how to add one. */
    int any_pickable = 0;
    for (int i = 0; i < 4; i++) if (target_selectable(i)) any_pickable = 1;
    if (!any_pickable) {
        gui_text(win, x, fy + 56,
                 "No writable disk found. In UTM add a Disk Image (48+", 4, 7);
        gui_text(win, x, fy + 70,
                 "MiB, Read Only OFF) as a second drive, then restart", 4, 7);
        gui_text(win, x, fy + 84,
                 "Setup. (The install CD itself can't be installed onto.)", 4, 7);
    }

    if (target_idx < 0) {
        gui_text(win, x, fy + 28,
                 "Click a drive row above to select it.", 8, 7);
    } else if (target_idx == boot) {
        gui_text(win, x, fy + 28,
                 "Installing in place on this disk. Click Next.", 1, 7);
    } else {
        gui_text(win, x, fy + 28,
                 "Target selected. Click Next.", 1, 7);
    }
}

static void page_network(void) {
    draw_title_bar("Network");
    int x = content_left();
    /* "Wi-Fi" panel — explicitly unavailable. */
    gui_fill_rect(win, x, content_top(), content_right() - x, 60, 8);
    gui_text(win, x + 8, content_top() + 8,  "Wi-Fi",        15, 8);
    gui_text(win, x + 8, content_top() + 28,
             "WiFi not available -- no compatible adapter detected.",
             15, 8);
    gui_text(win, x + 8, content_top() + 44,
             "(Real 802.11 needs a radio driver and TLS stack.)",
             15, 8);
    /* "Ethernet" panel — placeholder for v3.1. */
    gui_fill_rect(win, x, content_top() + 76, content_right() - x, 60, 8);
    gui_text(win, x + 8, content_top() + 84,  "Wired Ethernet", 15, 8);
    gui_text(win, x + 8, content_top() + 104,
             "Coming in BoxOS 3.1 (rtl8139 + minimal TCP/IP).",
             15, 8);
    gui_text(win, x, content_top() + 156,
             "Skipping network configuration.", 8, 7);
}

static void page_component(void) {
    draw_title_bar("Components");
    int x = content_left();
    wrap_lines(
        "Choose which BoxOS components to install. The default set "
        "includes everything that fits on a typical disk.",
        x, content_top(), content_right() - x, 0);
    int y = content_top() + 48;
    const char* rows[] = {
        "  [X] Core kernel + RadiDOS shell",
        "  [X] Window manager + Arise desktop",
        "  [X] File Manager, Notepad, Calculator, Paint, Clock, Music",
        "  [X] Games — Snake, Minesweeper, Pong, Reversi, Tetris, Solitaire, DOOM",
        "  [X] Sample documents in /DOCS",
        "  [ ] Network stack (not available in this build)",
    };
    for (int i = 0; i < (int)(sizeof(rows)/sizeof(rows[0])); i++)
        gui_text(win, x, y + i * 16, rows[i], 0, 7);
    gui_text(win, x, y + 6 * 16 + 12,
             "Click Install when you're ready.", 8, 7);
}

static void page_install(void) {
    draw_title_bar("Installing BoxOS Arise");
    int x = content_left();
    int boot = bos_inst_boot_drv();
    int in_place = (target_idx == boot);

    const char* status;
    if (install_error) {
        if (in_place) {
            status = "Install failed -- C: is read-only.";
        } else {
            int ph = bos_inst_err_phase();
            status = (ph == 1)
                ? "Install failed -- read error from the install media."
                : (ph == 2)
                ? "Install failed -- write error to the target drive (too small?)."
                : "Install failed -- I/O error on the target drive.";
        }
    } else if (in_place) {
        status = install_done
            ? "Installed onto C:. Click Next to finish."
            : "Writing BoxOS to C:...";
    } else {
        status = install_done
            ? "Copy complete. Click Next to finish."
            : "Copying every sector of BoxOS onto the target drive...";
    }
    gui_text(win, x, content_top(), status, 0, 7);
    if (install_error && !in_place) {
        /* Diagnostic so we can tell apart "refused" vs "I/O" failures. */
        char d[64]; int n = 0;
        const char* lbl = "mask="; while (*lbl) d[n++] = *lbl++;
        int m = bos_inst_drives(); d[n++] = (char)('0' + (m & 7)); d[n++] = ' ';
        lbl = "boot="; while (*lbl) d[n++] = *lbl++;
        int bd = bos_inst_boot_drv();
        if (bd < 0) d[n++] = '?'; else d[n++] = (char)('0' + bd);
        d[n++] = ' ';
        lbl = "tgt="; while (*lbl) d[n++] = *lbl++;
        d[n++] = (char)('0' + target_idx); d[n++] = ' ';
        lbl = "rc="; while (*lbl) d[n++] = *lbl++;
        if (last_start_rc < 0) { d[n++] = '-'; d[n++] = (char)('0' - last_start_rc); }
        else d[n++] = (char)('0' + last_start_rc);
        d[n++] = ' ';
        lbl = "ph="; while (*lbl) d[n++] = *lbl++;
        d[n++] = (char)('0' + bos_inst_err_phase());
        d[n] = 0;
        gui_text(win, x, content_top() + 16, d, 4, 7);
    }
    if (install_error && in_place) {
        gui_text(win, x, content_top() + 44,
                 "C: was attached read-only. In UTM set the disk's", 0, 7);
        gui_text(win, x, content_top() + 60,
                 "Image Type to \"Disk Image\" and uncheck Read Only,", 0, 7);
        gui_text(win, x, content_top() + 76,
                 "or pick a different (writable) drive. Then re-run.", 0, 7);
    } else if (install_error && bos_inst_err_phase() == 2) {
        gui_text(win, x, content_top() + 44,
                 "The target needs at least ~33 MiB of space. Make the", 0, 7);
        gui_text(win, x, content_top() + 60,
                 "destination disk larger, or pick a different drive.", 0, 7);
    }
    paint_progress(x, content_top() + 24, content_right() - x, install_percent_v);

    /* Rotating fun fact. */
    fact_idx = (int)((ticks_ms() / 1500) % N_FACTS);
    gui_fill_rect(win, x, content_top() + 96,
                  content_right() - x, 32, 7);
    gui_text(win, x, content_top() + 96, "Did you know?", 1, 7);
    gui_text(win, x, content_top() + 112, facts[fact_idx], 0, 7);

    if (!in_place && !install_done && !install_error) {
        char tinfo[64]; int n = 0;
        const char* p = "Target: ";
        while (*p) tinfo[n++] = *p++;
        const char* dl = drv_letters[target_idx];
        while (*dl) tinfo[n++] = *dl++;
        tinfo[n++] = ' ';
        const char* px = "(IDE ";
        while (*px) tinfo[n++] = *px++;
        tinfo[n++] = (char)('0' + (target_idx >> 1));
        tinfo[n++] = ':';
        tinfo[n++] = (char)('0' + (target_idx & 1));
        tinfo[n] = 0;
        gui_text(win, x, content_top() + 140, tinfo, 8, 7);
    }
}

static void page_finalize(void) {
    draw_title_bar("Finalising");
    int x = content_left();
    uint64_t elapsed = finalize_start_ms ? (ticks_ms() - finalize_start_ms) : 0;
    int total = 3500;
    int p = (int)((elapsed * 100) / total);
    if (p > 100) p = 100;
    if (p >= 100) finalize_done = 1;
    gui_text(win, x, content_top(),
             finalize_done ? "Done. Click Next to continue."
                           : "Configuring Control Panel and apps...", 0, 7);
    paint_progress(x, content_top() + 24, content_right() - x, p);
    static const char* steps[] = {
        "Registering Program Manager applets...",
        "Building the Start menu index...",
        "Writing /SYS/INSTALL.CFG + /SYS/SETUP.DONE...",
        "Theme assets installed.",
    };
    gui_fill_rect(win, x, content_top() + 60, content_right() - x, 16, 7);
    gui_text(win, x, content_top() + 60,
             steps[(int)(elapsed / 875) % 4], 8, 7);
}

static void page_timezone(void) {
    draw_title_bar("Time Zone");
    int x = content_left();
    gui_text(win, x, content_top(),
             "Pick the time zone closest to where this machine lives.",
             0, 7);
    int y = content_top() + 24;
    for (int i = 0; i < N_TZ; i++) {
        int sel = (i == tz_pick);
        gui_fill_rect(win, x, y - 2, content_right() - x, 18, sel ? 9 : 7);
        gui_text(win, x + 4, y + 2, tz_names[i], sel ? 15 : 0, sel ? 9 : 7);
        y += 20;
    }
    gui_text(win, x, content_top() + N_TZ * 20 + 30,
             "(No RTC yet -- the uptime clock ignores this for now.)",
             8, 7);
}

static void page_done(void) {
    draw_title_bar("Finished");
    int x = content_left();
    wrap_lines(
        "BoxOS Arise has been installed and finalised.\n"
        "Click Finish to power the machine off. Start the VM again "
        "in UTM and it boots straight off the installed disk to the "
        "desktop -- you won't see Setup again. You can leave the "
        "install CD attached; it's ignored from now on.",
        x, content_top(), content_right() - x, 0);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    switch (page) {
        case PAGE_WELCOME:   page_welcome();   break;
        case PAGE_USER:      page_user();      break;
        case PAGE_DEVICES:   page_devices();   break;
        case PAGE_NETWORK:   page_network();   break;
        case PAGE_TARGET:    page_target();    break;
        case PAGE_COMPONENT: page_component(); break;
        case PAGE_INSTALL:   page_install();   break;
        case PAGE_FINALIZE:  page_finalize();  break;
        case PAGE_TIMEZONE:  page_timezone();  break;
        case PAGE_DONE:      page_done();      break;
    }
    draw_buttons();
}

/* ---- input ----------------------------------------------------- */

static int hit_btn(int rx, int ry, int bx) {
    int by = button_y();
    return rx >= bx && rx < bx + BTN_W && ry >= by && ry < by + BTN_H;
}

/* In-place install path (no separate target drive): write
 * /SYS/INSTALL.CFG with the user info AND /SYS/SETUP.DONE so the
 * next boot from this same disk skips Setup entirely. Returns 0 on
 * success, < 0 if the disk is read-only (e.g. a CD attach). */
static int write_install_marker(void) {
    char buf[256]; int b = 0;
    const char* keys[3] = { "NAME=", "COMPANY=", "HOSTNAME=" };
    const char* vals[3] = { user_name, user_company, user_name };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; keys[i][j] && b < (int)sizeof(buf) - 1; j++)
            buf[b++] = keys[i][j];
        for (int j = 0; vals[i][j] && b < (int)sizeof(buf) - 1; j++)
            buf[b++] = vals[i][j];
        if (b < (int)sizeof(buf) - 1) buf[b++] = '\n';
    }
    buf[b] = 0;
    if (bos_save_to_folder("SYS", "INSTALL.CFG", buf, (uint64_t)b) < 0)
        return -1;
    if (bos_save_to_folder("SYS", "SETUP.DONE", "SETUP.DONE\n", 11) < 0)
        return -1;
    return 0;
}

static void go_next(void) {
    if (page == PAGE_DONE) {
        /* Only act if the install actually persisted somewhere. Power
         * off (not reset): a fresh launch boots cleanly off the new
         * disk, where a guest reset can dead-end in the BIOS. */
        if (install_error) return;
        sleep_ms(200);
        power_off_now();
        return;
    }
    if (page == PAGE_COMPONENT) {
        page = PAGE_INSTALL;
        install_started = 0;
        install_done    = 0;
        install_error   = 0;
        install_percent_v = 0;
        int boot = bos_inst_boot_drv();
        if (target_idx == boot) {
            /* Install onto C: (the disk we booted from), in place.
             * Needs a *writable* boot disk (UTM: Disk Image, Read
             * Only off). A read-only CD attach makes the marker
             * writes fail — surfaced as an error rather than
             * pretending it worked. */
            if (format_first && bos_factory_reset() < 0) {
                install_error = 1; install_percent_v = 0; return;
            }
            int rc = write_install_marker();
            if (rc < 0) { install_error = 1; install_percent_v = 0; }
            else        { install_done  = 1; install_percent_v = 100; }
        } else {
            /* Install onto another drive: copy every sector of the
             * boot media onto it, then stamp the markers. (Cross-
             * disk copy overwrites everything, so "format first" is
             * implicit there.) */
            int rc = bos_inst_start(target_idx);
            last_start_rc = rc;
            if (rc < 0) install_error = 1;
            else        install_started = 1;
        }
        return;
    }
    if (page == PAGE_INSTALL) {
        if (!install_done || install_error) return;
        page = PAGE_FINALIZE;
        finalize_started = 0;
        finalize_done    = 0;
        return;
    }
    if (page == PAGE_FINALIZE) {
        if (!finalize_done) return;
        page = PAGE_TIMEZONE;
        return;
    }
    if (page == PAGE_TIMEZONE) { page = PAGE_DONE; return; }
    if (page < PAGE_DONE) page++;
}

static void go_back(void) {
    /* No backing out of install/finalize/done. */
    if (page >= PAGE_INSTALL) return;
    if (can_back()) page--;
}

static void handle_text_key(unsigned a) {
    if (page != PAGE_USER) return;
    char* buf = (edit_target == 0) ? user_name : user_company;
    int cap   = 31;
    int len   = strlen_l(buf);
    if (a == '\t') { edit_target ^= 1; return; }
    if (a == '\b' || a == 0x7F) {
        if (len > 0) buf[len - 1] = 0;
        return;
    }
    if (a == '\n' || a == '\r') {
        edit_target ^= 1; return;
    }
    if (a >= ' ' && a < 0x7F && len < cap) {
        buf[len] = (char)a;
        buf[len + 1] = 0;
    }
    (void)edit_pos;
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("BoxOS Arise Setup", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("SETUP: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    paint_full();

    uint64_t last_repaint = 0;
    for (;;) {
        struct gui_event ev;
        int got = gui_poll_event(&ev);
        if (got && ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (got && ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) {
                /* Cancel — drop the install. */
                gui_close_window(win);
                return 0;
            }
            handle_text_key(a);
            paint_full();
            continue;
        }
        if (got && ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - WIN_X - 1;
            int ry = ev.y - WIN_Y - 13;
            if (hit_btn(rx, ry, back_btn_x()) && can_back()) {
                go_back(); paint_full(); continue;
            }
            if (hit_btn(rx, ry, next_btn_x())) {
                if (!next_enabled()) continue;
                go_next(); paint_full(); continue;
            }
            if (page < PAGE_INSTALL && hit_btn(rx, ry, cancel_btn_x())) {
                gui_close_window(win);
                return 0;
            }
            /* User-info page: click an edit field to focus it. */
            if (page == PAGE_USER) {
                int x = content_left();
                if (rx >= x && rx < x + 360) {
                    if (ry >= content_top() + 60 && ry < content_top() + 80)
                        edit_target = 0;
                    else if (ry >= content_top() + 110 && ry < content_top() + 130)
                        edit_target = 1;
                    paint_full();
                }
            }
            /* Target-pick page: click any selectable drive to select;
             * click the box to toggle "format first". */
            if (page == PAGE_TARGET) {
                int x = content_left();
                for (int i = 0; i < 4; i++) {
                    int ry0 = target_drive_row_y(i);
                    if (target_selectable(i) &&
                        rx >= x - 2 && rx < content_right() + 2 &&
                        ry >= ry0 - 2 && ry < ry0 + 16) {
                        target_idx = i;
                        paint_full();
                    }
                }
                int fy = format_box_y();
                if (rx >= x && rx < x + 220 && ry >= fy && ry < fy + 14) {
                    format_first = !format_first;
                    paint_full();
                }
            }
            /* Timezone-pick page. */
            if (page == PAGE_TIMEZONE) {
                int x = content_left();
                int y0 = content_top() + 24;
                int row = (ry - (y0 - 2)) / 20;
                if (rx >= x && rx < content_right() && row >= 0 && row < N_TZ) {
                    tz_pick = row;
                    paint_full();
                }
            }
            continue;
        }

        /* Drive the byte copy: a batch of real chunks per frame so
         * the progress bar tracks actual work. When the copy is
         * done, re-mount the target and stamp INSTALL.CFG +
         * SETUP.DONE onto it (bos_inst_finalize). */
        if (page == PAGE_INSTALL && install_started && !install_done && !install_error) {
            for (int i = 0; i < 32; i++) {
                int rc = bos_inst_chunk();
                if (rc < 0) { install_error = 1; break; }
                if (rc == 1) { install_done = 1; break; }
            }
            install_percent_v = bos_inst_percent();
            if (install_done && !install_error) {
                if (bos_inst_finalize(target_idx, user_name, user_company) < 0)
                    install_error = 1;
            }
            uint64_t now = ticks_ms();
            if (now - last_repaint >= 80) { last_repaint = now; paint_full(); }
        } else if (page == PAGE_INSTALL) {
            uint64_t now = ticks_ms();
            if (now - last_repaint >= 200) { last_repaint = now; paint_full(); }
        }

        /* Cosmetic finalisation beat — a short timed bar after the
         * copy so it feels like a real "configuring components"
         * step. The actual marker writes already happened above. */
        if (page == PAGE_FINALIZE) {
            if (!finalize_started) {
                finalize_started = 1;
                finalize_start_ms = ticks_ms();
            }
            uint64_t now = ticks_ms();
            if (now - last_repaint >= 80) { last_repaint = now; paint_full(); }
        }

        if (!got) sleep_ms(20);
    }
}
