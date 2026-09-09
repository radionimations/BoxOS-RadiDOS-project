/* BoxOS framebuffer driver.
 *
 * M1a: detect a usable graphics framebuffer and stash its info.
 *
 * Strategy: we only support QEMU/UTM/VirtualBox bring-up first via
 * the Bochs Graphics Adapter (BGA / VBE Display Interface) IO ports.
 * BGA registers are reachable from long mode, so we don't need a
 * real-mode trampoline yet. Bare-metal VBE via stage-2 INT 10h is
 * a later milestone.
 *
 * BGA ports:
 *   0x1CE  index, 0x1CF data
 *   index 0  ID         (read = BGA version, e.g. 0xB0C0..0xB0C5)
 *   index 1  XRES
 *   index 2  YRES
 *   index 3  BPP
 *   index 4  ENABLE     (1 enable, |0x40 LFB, |0x80 keep contents)
 *   index 5  BANK
 *   ...
 *
 * QEMU's std-vga puts its linear framebuffer at the PCI BAR 0,
 * which on the i440FX/q35 we use is 0xFD000000. We hard-code that
 * for now; reading the PCI config space to find the BAR can come
 * later. */

#include "boxos.h"

#define BOOTINFO_PHYS  0x500ULL
#define BOOTINFO_MAGIC 0x31584F42U   /* 'BOX1' */

#define BGA_INDEX 0x01CE
#define BGA_DATA  0x01CF

#define BGA_REG_ID      0
#define BGA_REG_XRES    1
#define BGA_REG_YRES    2
#define BGA_REG_BPP     3
#define BGA_REG_ENABLE  4

#define BGA_DISABLED    0x00
#define BGA_ENABLED     0x01
#define BGA_LFB_ENABLED 0x40
#define BGA_NOCLEARMEM  0x80

#define BGA_FB_PHYS_DEFAULT 0xFD000000ULL

/* VGA DAC ports for the palette. Even with BGA, the 8-bit palette
 * still goes through the same VGA DAC. */
#define VGA_DAC_INDEX_W 0x03C8
#define VGA_DAC_DATA    0x03C9

static struct boot_info g_bi;
static bool g_fb_present = false;
static bool g_fb_enabled = false;
static uint8_t* g_fb_base = 0;

static inline void bga_write(uint16_t reg, uint16_t val) {
    outw(BGA_INDEX, reg);
    outw(BGA_DATA, val);
}
static inline uint16_t bga_read(uint16_t reg) {
    outw(BGA_INDEX, reg);
    return inw(BGA_DATA);
}

static bool bga_detect(void) {
    /* Probe by reading the ID register. Real BGA versions sit in
     * 0xB0C0..0xB0C5; if no BGA hardware is present, the read
     * returns 0xFFFF or 0x0000. */
    uint16_t id = bga_read(BGA_REG_ID);
    return (id >= 0xB0C0 && id <= 0xB0C5);
}

void fb_init(void) {
    /* Pull the magic-stamped struct from stage 2 first. */
    uintptr_t src_addr = (uintptr_t)BOOTINFO_PHYS;
    memcpy(&g_bi, (const void*)src_addr, sizeof(g_bi));
    if (g_bi.magic != BOOTINFO_MAGIC) {
        g_fb_present = false;
        return;
    }

    /* If stage 2 already filled in fb_addr, trust it. Otherwise try
     * the BGA path. */
    if (g_bi.fb_addr != 0 && g_bi.fb_bpp == 8) {
        g_fb_present = true;
        return;
    }

    if (!bga_detect()) {
        g_fb_present = false;
        return;
    }

    /* Detected BGA. Record the framebuffer info we *will* use.
     * Actually programming the mode happens in fb_enable() (later
     * milestone), once we have a framebuffer console ready. */
    g_bi.fb_addr   = BGA_FB_PHYS_DEFAULT;
    g_bi.fb_width  = 640;
    g_bi.fb_height = 480;
    g_bi.fb_bpp    = 8;
    g_bi.fb_pitch  = 640;
    g_bi.fb_mode   = 0xB0C0;   /* "set via BGA" sentinel */
    g_fb_present   = true;
}

bool fb_present(void) { return g_fb_present; }

const struct boot_info* fb_bootinfo(void) { return &g_bi; }

/* Default 256-entry palette: low 16 are the standard VGA colors,
 * remaining 240 are a smooth 6x6x6 cube + gray ramp. Each entry is
 * 6-bit per channel via the VGA DAC. */
void fb_reload_palette(void) {
    static const uint8_t base16[16][3] = {
        {  0,   0,   0}, {  0,   0,  42}, {  0,  42,   0}, {  0,  42,  42},
        { 42,   0,   0}, { 42,   0,  42}, { 42,  21,   0}, { 42,  42,  42},
        { 21,  21,  21}, { 21,  21,  63}, { 21,  63,  21}, { 21,  63,  63},
        { 63,  21,  21}, { 63,  21,  63}, { 63,  63,  21}, { 63,  63,  63},
    };
    outb(VGA_DAC_INDEX_W, 0);
    for (int i = 0; i < 16; i++) {
        outb(VGA_DAC_DATA, base16[i][0]);
        outb(VGA_DAC_DATA, base16[i][1]);
        outb(VGA_DAC_DATA, base16[i][2]);
    }
    /* 6x6x6 color cube starting at index 16 (216 entries). */
    for (int r = 0; r < 6; r++) for (int g = 0; g < 6; g++) for (int b = 0; b < 6; b++) {
        outb(VGA_DAC_DATA, (uint8_t)(r * 12));
        outb(VGA_DAC_DATA, (uint8_t)(g * 12));
        outb(VGA_DAC_DATA, (uint8_t)(b * 12));
    }
    /* Grayscale ramp for the last 24 entries. */
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(2 + i * 2);
        outb(VGA_DAC_DATA, v);
        outb(VGA_DAC_DATA, v);
        outb(VGA_DAC_DATA, v);
    }
}

/* Identity-map a 2 MiB-aligned region into the existing page tables.
 * Stage 2 set up PML4[0] -> PDPT @ 0x2000 -> PD[0] @ 0x3000 (1 GiB
 * via 2 MiB pages). For the framebuffer at 3.96 GiB we add PDPT[3]
 * pointing at a fresh PD at 0x6000 that maps 3..4 GiB. */
static void map_framebuffer(uint64_t phys) {
    (void)phys;   /* same scheme regardless of phys; we map all of 3..4 GiB */
    volatile uint64_t* pdpt = (volatile uint64_t*)0x2000;
    volatile uint64_t* pd3  = (volatile uint64_t*)0x6000;
    for (int i = 0; i < 512; i++)
        pd3[i] = ((uint64_t)i * 0x200000ULL + 0xC0000000ULL) | 0x83ULL;
    pdpt[3] = 0x00006003ULL;   /* phys 0x6000 | P | RW */
    /* Flush TLB. */
    uint64_t cr3;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ __volatile__ ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

int fb_enable(void) {
    if (!g_fb_present) return -1;
    if (g_fb_enabled)  return 0;

    /* Program BGA: disable, set geometry, re-enable with LFB.
     * NOCLEARMEM is essential — without it, the std-vga zeroes the
     * legacy 0xB8000 text-mode backing memory during the mode switch,
     * destroying the boot logs we want fbcon to render. */
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES,   g_bi.fb_width);
    bga_write(BGA_REG_YRES,   g_bi.fb_height);
    bga_write(BGA_REG_BPP,    g_bi.fb_bpp);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED | BGA_NOCLEARMEM);

    map_framebuffer(g_bi.fb_addr);
    g_fb_base = (uint8_t*)(uintptr_t)g_bi.fb_addr;

    fb_reload_palette();

    /* Clear the LFB to black (independently of vram preservation). */
    uint64_t bytes = (uint64_t)g_bi.fb_pitch * (uint64_t)g_bi.fb_height;
    memset(g_fb_base, 0, (size_t)bytes);

    g_fb_enabled = true;
    return 0;
}

void fb_fill(uint8_t color) {
    if (!g_fb_enabled) return;
    uint64_t bytes = (uint64_t)g_bi.fb_pitch * (uint64_t)g_bi.fb_height;
    memset(g_fb_base, color, (size_t)bytes);
}

void fb_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (!g_fb_enabled) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= g_bi.fb_width || y >= g_bi.fb_height) return;
    if (x + w > g_bi.fb_width)  w = g_bi.fb_width  - x;
    if (y + h > g_bi.fb_height) h = g_bi.fb_height - y;
    if (w <= 0 || h <= 0) return;
    uint8_t* row = g_fb_base + (uint64_t)y * g_bi.fb_pitch + x;
    for (int j = 0; j < h; j++) {
        memset(row, color, (size_t)w);
        row += g_bi.fb_pitch;
    }
}

uint8_t* fb_pixels(void) { return g_fb_base; }
