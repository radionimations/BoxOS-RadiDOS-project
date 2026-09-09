/* VGA mode 13h driver (320x200x256, linear framebuffer @ 0xA0000).
 *
 * Pure register programming, no BIOS calls. Public API:
 *   gfx_init()         - call once at boot to snapshot the BIOS font
 *   gfx_set_mode_13h() - graphics, 320x200x256
 *   gfx_set_text_mode()- back to 80x25 text, font + palette restored
 *   gfx_blit(buf)      - copy 64000-byte indexed framebuffer to VGA
 *   gfx_set_palette(p) - install 256-entry, R-G-B 6-bit palette
 *
 * Why we save/restore plane 2:
 * Mode 13h is chain-4: every CPU byte at 0xA0000+i goes to plane (i%4).
 * That includes plane 2, which holds the text-mode character glyphs
 * (the BIOS-loaded font). Without restoring it, returning to text
 * mode shows scrambled gibberish where letters used to be.
 */

#include "boxos.h"

/* ---- mode 13h register tables ---------------------------------- */
static const uint8_t M13_MISC = 0x63;
static const uint8_t M13_SEQ [5]  = { 0x03, 0x01, 0x0F, 0x00, 0x0E };
static const uint8_t M13_CRTC[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80,
    0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x9C, 0x0E,
    0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF
};
static const uint8_t M13_GC  [9]  = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
    0x05, 0x0F, 0xFF
};
static const uint8_t M13_AC  [21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x41, 0x00,
    0x0F, 0x00, 0x00
};

/* ---- mode 03h (80x25 colour text) -------------------------------- */
static const uint8_t M03_MISC = 0x67;
static const uint8_t M03_SEQ [5]  = { 0x03, 0x00, 0x03, 0x00, 0x02 };
static const uint8_t M03_CRTC[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81,
    0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E,
    0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E,
    0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};
static const uint8_t M03_GC  [9]  = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x0E, 0x00, 0xFF
};
static const uint8_t M03_AC  [21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x0C, 0x00,
    0x0F, 0x08, 0x00
};

static void program(uint8_t misc,
                    const uint8_t* seq,
                    const uint8_t* crtc,
                    const uint8_t* gc,
                    const uint8_t* ac) {
    outb(0x3C2, misc);

    for (int i = 0; i < 5; i++) {
        outb(0x3C4, (uint8_t)i);
        outb(0x3C5, seq[i]);
    }

    outb(0x3D4, 0x03); outb(0x3D5, (uint8_t)(inb(0x3D5) | 0x80));
    outb(0x3D4, 0x11); outb(0x3D5, (uint8_t)(inb(0x3D5) & ~0x80));
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    for (int i = 0; i < 9; i++) {
        outb(0x3CE, (uint8_t)i);
        outb(0x3CF, gc[i]);
    }

    for (int i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, ac[i]);
    }

    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

/* ---- plane-2 (font) save / restore ----------------------------- */

#define PLANE2_BYTES 0x10000        /* 64 KiB per VGA plane          */
static uint8_t font_backup[PLANE2_BYTES];
static bool    font_saved = false;

static void linear_plane2_access(void) {
    /* Make 0xA0000-0xAFFFF look like a flat window into plane 2. */
    outb(0x3C4, 2); outb(0x3C5, 0x04);   /* SEQ map mask = plane 2  */
    outb(0x3C4, 4); outb(0x3C5, 0x06);   /* SEQ mem mode: ext on,
                                           even/odd off, chain4 off */
    outb(0x3CE, 4); outb(0x3CF, 0x02);   /* GC read map = plane 2   */
    outb(0x3CE, 5); outb(0x3CF, 0x00);   /* GC mode: read 0, write 0 */
    outb(0x3CE, 6); outb(0x3CF, 0x05);   /* GC misc: A0000, even/odd off */
}

/* Restore the SEQ/GC subset that linear_plane2_access() touches back
 * to standard text-mode values. Lets gfx_init be called at boot time
 * (so we snapshot the font with the system in a known-clean state)
 * without leaving the VGA in a state where 0xB8000 is unmapped. */
static void restore_text_regs(void) {
    outb(0x3C4, 2); outb(0x3C5, 0x03);
    outb(0x3C4, 4); outb(0x3C5, 0x02);
    outb(0x3CE, 4); outb(0x3CF, 0x00);
    outb(0x3CE, 5); outb(0x3CF, 0x10);
    outb(0x3CE, 6); outb(0x3CF, 0x0E);
}

void gfx_init(void) {
    if (font_saved) return;
    linear_plane2_access();
    volatile uint8_t* p = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < PLANE2_BYTES; i++) font_backup[i] = p[i];
    font_saved = true;
    restore_text_regs();
}

/* Expose the BIOS-saved 8x16 VGA font for fbcon. SeaBIOS lays
 * character maps out in 32-byte slots (16 active rows + 16 bytes
 * padding) so all 256 glyphs occupy 8 KiB. Reading at slot * 32 +
 * row gives the actual scanline byte. Returns 0 if we never
 * snapshotted the font (gfx_init not called). */
uint8_t gfx_font_byte(uint8_t ch, int row) {
    if (!font_saved || row < 0 || row >= 16) return 0;
    return font_backup[(int)ch * 32 + row];
}

static void restore_font(void) {
    if (!font_saved) return;
    linear_plane2_access();
    volatile uint8_t* p = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < PLANE2_BYTES; i++) p[i] = font_backup[i];
}

extern const unsigned char font_8x8[96][8];

/* Load our hand-rolled 8x8 font into plane 2 as 8x16 glyphs (the 8x8
 * data is written into rows 4..11 so each char sits centred in a 16-row
 * cell; rows 0..3 and 12..15 stay blank). The Cirrus VGA in UTM doesn't
 * round-trip plane 2 reliably across mode 13h, so we don't trust the
 * BIOS-font save/restore — we just paint our own font on every text
 * mode switch. */
static void load_builtin_font(void) {
    /* Belt-and-suspenders: explicitly nail down every register that
     * affects how a write to 0xA0000 hits plane 2. */
    /* SEQ */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);   /* map mask = plane 2 only */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);   /* mem: ext on, e/o off, chain4 off */
    /* GC */
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);   /* set/reset value 0 */
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);   /* enable set/reset 0 */
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);   /* data rotate / function = 0 */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);   /* read map = plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* mode = read 0, write 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);   /* memmap A0000-AFFFF */
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);   /* bit mask = pass all bits */

    volatile uint8_t* p = (volatile uint8_t*)0xA0000;
    /* Zero all 256 glyph slots (32 bytes each = 8 KiB used by VGA). */
    for (int i = 0; i < 256 * 32; i++) p[i] = 0;
    /* Render the printable ASCII range from font_8x8, centred in
     * each 16-row cell (rows 4..11). */
    for (int c = 32; c <= 126; c++) {
        for (int row = 0; row < 8; row++)
            p[c * 32 + row + 4] = font_8x8[c - 32][row];
    }
}

/* ---- standard 16-colour DAC palette (text mode) ---------------- */

static const struct { uint8_t idx, r, g, b; } TEXT_PAL[] = {
    {  0,  0,  0,  0}, {  1,  0,  0, 42}, {  2,  0, 42,  0}, {  3,  0, 42, 42},
    {  4, 42,  0,  0}, {  5, 42,  0, 42}, {  7, 42, 42, 42}, { 20, 42, 21,  0},
    { 56, 21, 21, 21}, { 57, 21, 21, 63}, { 58, 21, 63, 21}, { 59, 21, 63, 63},
    { 60, 63, 21, 21}, { 61, 63, 21, 63}, { 62, 63, 63, 21}, { 63, 63, 63, 63},
};

static void install_text_palette(void) {
    for (size_t i = 0; i < sizeof(TEXT_PAL)/sizeof(TEXT_PAL[0]); i++) {
        outb(0x3C8, TEXT_PAL[i].idx);
        outb(0x3C9, TEXT_PAL[i].r);
        outb(0x3C9, TEXT_PAL[i].g);
        outb(0x3C9, TEXT_PAL[i].b);
    }
}

static void grayscale_palette(void) {
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        uint8_t v = (uint8_t)(i >> 2);
        outb(0x3C9, v);
        outb(0x3C9, v);
        outb(0x3C9, v);
    }
}

/* ---- public API ----------------------------------------------- */

/* Graphics-mode (320x200x256) apps run FULLSCREEN inside the BGA
 * 640x480 LFB: their 320x200 frame is pixel-doubled into the central
 * 640x400 region, with 40px black bars top and bottom. The OS palette
 * is reapplied on app exit so the Executive comes back uncoloured.
 * We don't try to save the previous palette — QEMU's std-vga DAC
 * read path is unreliable; load the OS default outright. */
static int      g_gfx_active = 0;

void gfx_set_mode_13h(void) {
    if (fb_present() && fb_pixels()) {
        if (g_gfx_active) return;
        /* Hide the OS cursor and clear the whole BGA framebuffer to
         * black so the app starts on a clean canvas (and the
         * Executive chrome doesn't show through behind a partial
         * blit). */
        cursor_hide();
        fb_fill(0);
        g_gfx_active = 1;
        return;
    }
    /* Pre-GUI fallback: real mode 13h via VGA register programming. */
    if (!font_saved) gfx_init();
    program(M13_MISC, M13_SEQ, M13_CRTC, M13_GC, M13_AC);
    grayscale_palette();
    volatile uint8_t* fb = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < 0x10000; i++) fb[i] = 0;
}

void gfx_set_text_mode(void) {
    if (g_gfx_active) {
        g_gfx_active = 0;
        /* Bring the OS palette back unconditionally. */
        fb_reload_palette();
        cursor_show();
        /* DOOM/PLASMA scribbled all over the framebuffer; the WM
         * suspended itself while g_gfx_active was set. Now that we're
         * back, rebuild the desktop + every window's chrome and ask
         * apps to redraw their content. */
        wm_repaint_all();
        return;
    }
    /* Legacy fallback. */
    program(M03_MISC, M03_SEQ, M03_CRTC, M03_GC, M03_AC);
    if (font_saved) restore_font();
    else            load_builtin_font();
    program(M03_MISC, M03_SEQ, M03_CRTC, M03_GC, M03_AC);
    install_text_palette();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    volatile uint16_t* tfb = (volatile uint16_t*)0xB8000;
    uint16_t blank = (uint16_t)((0x07 << 8) | ' ');
    for (int i = 0; i < 80 * 25; i++) tfb[i] = blank;
    vga_clear();
}

/* Called by the loader when the app's entry returns. Forces a clean
 * graphics tear-down even if the app didn't call gfx_set_text_mode
 * itself (PLASMA, DOOM). */
void gfx_app_cleanup(void) {
    if (g_gfx_active) gfx_set_text_mode();
}

/* Public probe: anyone painting the framebuffer should check this and
 * stay out of the way while a fullscreen 320x200 app owns the screen.
 * Used by wm_paint_clock and gui_poll_event so the desktop's clock
 * ticks and stray app WM_PAINT events don't write 640-wide rows into
 * the middle of DOOM's blit. */
int gfx_in_app_mode(void) { return g_gfx_active; }

extern const unsigned char font_8x8[96][8];
extern const char* vga_log_line(int n);
extern int vga_log_lines(void);

/* Draw one line of text into the mode-13h framebuffer at row=line_idx
 * (each line is 8 pixels tall). col 0..39 with 8x8 font. */
static void draw_line(volatile uint8_t* fb, int line_idx, const char* s,
                      uint8_t fg, uint8_t bg) {
    int row_start = line_idx * 8;
    /* Clear this 8-pixel band. */
    for (int y = 0; y < 8; y++) {
        volatile uint8_t* line = fb + (row_start + y) * 320;
        for (int x = 0; x < 320; x++) line[x] = bg;
    }
    int col = 0;
    while (s && *s && col < 40) {
        unsigned char c = (unsigned char)*s++;
        const unsigned char* glyph = (c < 32 || c > 126)
                                         ? font_8x8[0]
                                         : font_8x8[c - 32];
        for (int row = 0; row < 8; row++) {
            unsigned char bits = glyph[row];
            volatile uint8_t* line = fb + (row_start + row) * 320 + col * 8;
            for (int x = 0; x < 8; x++) {
                if (bits & (0x80 >> x)) line[x] = fg;
            }
        }
        col++;
    }
}

/* Render the LAST 4 log entries at the top of the framebuffer.
 * 4 lines * 8 px = 32 px tall — leaves 168 px for the game view
 * (which matches DOOM's status-bar layout: 168 px world + 32 px HUD).
 * Disk log keeps a much longer history for analysis. */
#define OVERLAY_LINES 4
static void overlay_status(volatile uint8_t* fb, uint8_t fg, uint8_t bg) {
    int n = vga_log_lines();
    int start = n > OVERLAY_LINES ? n - OVERLAY_LINES : 0;
    int shown = n - start;
    /* Clear the unused upper rows when log is short. */
    for (int i = shown; i < OVERLAY_LINES; i++) draw_line(fb, i, "", fg, bg);
    /* Fill the bottom-aligned log area. */
    for (int i = 0; i < shown; i++) {
        draw_line(fb, OVERLAY_LINES - shown + i, vga_log_line(start + i), fg, bg);
    }
}

void gfx_blit(const void* src) {
    if (g_gfx_active && fb_pixels()) {
        const uint8_t* s = (const uint8_t*)src;
        const struct boot_info* bi = fb_bootinfo();
        int pitch = bi->fb_pitch;
        /* 320x200 → 640x400, 2x pixel doubling, centered with
         * 40-px letterbox top and bottom (480 - 400 = 80 / 2). */
        const int dst_x0 = 0;
        const int dst_y0 = 40;
        for (int y = 0; y < 200; y++) {
            const uint8_t* sline = s + (uint64_t)y * 320;
            uint8_t* d0 = fb_pixels() +
                          (uint64_t)(dst_y0 + y * 2) * pitch + dst_x0;
            uint8_t* d1 = d0 + pitch;
            for (int x = 0; x < 320; x++) {
                uint8_t v = sline[x];
                d0[x * 2]     = v;
                d0[x * 2 + 1] = v;
                d1[x * 2]     = v;
                d1[x * 2 + 1] = v;
            }
        }
        return;
    }
    /* Legacy fallback. */
    volatile uint8_t* fb = (volatile uint8_t*)0xA0000;
    const uint8_t* s = (const uint8_t*)src;
    for (int i = 0; i < 320 * 200; i++) fb[i] = s[i];
    for (int i = 320 * 200; i < 0x10000; i++) fb[i] = 0;
    (void)overlay_status;
}

void gfx_set_palette(const void* pal) {
    /* Always writes to the VGA DAC, which is shared by both BGA and
     * legacy modes. While a graphics-mode app is running, this also
     * affects the rest of the screen behind the window — that's
     * deliberate (PLASMA's whole point is animating the palette).
     * The previous palette was snapshotted in gfx_set_mode_13h() and
     * is restored when the window closes. */
    const uint8_t* p = (const uint8_t*)pal;
    outb(0x3C8, 0);
    for (int i = 0; i < 256 * 3; i++) outb(0x3C9, p[i] & 0x3F);
}
