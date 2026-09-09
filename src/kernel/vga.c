#include "boxos.h"
#include <stdarg.h>

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;

/* Row 0 used to host a blue "current status" bar — that's been
 * removed (status text still goes to the rolling/disk log). The whole
 * 0..VGA_HEIGHT-1 range is now ordinary text. */
#define STATUS_ROW    0
#define SCROLL_FIRST  0
#define SCROLL_LAST   VGA_HEIGHT

static int      g_row;
static int      g_col;
static uint8_t  g_attr;

/* Forward declaration so the scrollback helpers below can build cells
 * before the existing cell() definition further down in the file. */
static uint16_t cell(char c, uint8_t attr);

/* ---- scrollback ----------------------------------------------- */
/* Every cell ever written into the body region (rows 1..23) goes into
 * a circular buffer. The user can scroll back through it with PgUp/PgDn.
 * sb_top is the LOGICAL row index that currently maps to screen row 1.
 * viewing_offset is how far up the user has scrolled — 0 means "live". */
#define SB_LINES 256
static uint16_t sb[SB_LINES][VGA_WIDTH];
static int      sb_top         = 0;
static int      sb_view_offset = 0;
static bool     sb_initialised = false;

uint16_t vga_visible_cell(int row, int col) {
    if (row < 0 || row >= VGA_HEIGHT) return cell(' ', 0x07);
    if (col < 0 || col >= VGA_WIDTH)  return cell(' ', 0x07);
    int logical = sb_top + row - SCROLL_FIRST - sb_view_offset;
    if (!sb_initialised) return cell(' ', 0x07);
    return sb[((logical % SB_LINES) + SB_LINES) % SB_LINES][col];
}

static int sb_slot(int logical) {
    int s = logical % SB_LINES;
    if (s < 0) s += SB_LINES;
    return s;
}
static uint16_t sb_get(int logical, int col) {
    if (logical < 0) return cell(' ', 0x07);
    return sb[sb_slot(logical)][col];
}
static void sb_put(int logical, int col, uint16_t v) {
    sb[sb_slot(logical)][col] = v;
}

static void sb_init_once(void) {
    if (sb_initialised) return;
    uint16_t blank = cell(' ', 0x07);
    for (int r = 0; r < SB_LINES; r++)
        for (int c = 0; c < VGA_WIDTH; c++) sb[r][c] = blank;
    sb_initialised = true;
}

/* Repaint screen rows SCROLL_FIRST..SCROLL_LAST-1 from the scrollback,
 * adjusted by viewing_offset. Called whenever the offset changes. */
static void sb_redraw(void) {
    int visible = SCROLL_LAST - SCROLL_FIRST;
    for (int r = 0; r < visible; r++) {
        int logical = sb_top + r - sb_view_offset;
        for (int c = 0; c < VGA_WIDTH; c++)
            VGA[(SCROLL_FIRST + r) * VGA_WIDTH + c] = sb_get(logical, c);
    }
    if (fbcon_active()) fbcon_repaint();
}

static uint16_t cell(char c, uint8_t attr) {
    return ((uint16_t)attr << 8) | (uint8_t)c;
}

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(g_row * VGA_WIDTH + g_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    if (fbcon_active()) fbcon_set_cursor(g_col, g_row);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    g_attr = (uint8_t)((bg & 0x0F) << 4) | (fg & 0x0F);
}

void vga_clear(void) {
    sb_init_once();
    uint16_t blank = cell(' ', g_attr);
    /* Clear every row of the visible framebuffer. */
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA[i] = blank;
    /* Reset scrollback too — CLS means clean slate. */
    for (int r = 0; r < SB_LINES; r++)
        for (int c = 0; c < VGA_WIDTH; c++) sb[r][c] = blank;
    sb_top = 0;
    sb_view_offset = 0;
    g_row = SCROLL_FIRST;
    g_col = 0;
    if (fbcon_active()) fbcon_repaint();
    update_cursor();
}

void vga_init(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

static void scroll(void) {
    sb_init_once();
    /* The row currently at SCROLL_FIRST is logical row sb_top — it's
     * already in the scrollback buffer. Bump sb_top and clear the new
     * bottom logical row in the buffer. */
    sb_top++;
    int new_bot = sb_top + (SCROLL_LAST - SCROLL_FIRST - 1);
    uint16_t blank = cell(' ', g_attr);
    for (int x = 0; x < VGA_WIDTH; x++) sb_put(new_bot, x, blank);

    /* Update the visible framebuffer too — but only when the user is
     * looking at "live". When they're scrolled up, leave VGA frozen at
     * the saved view; new output still accumulates in sb behind it. */
    if (sb_view_offset == 0) {
        for (int y = SCROLL_FIRST + 1; y < SCROLL_LAST; y++)
            for (int x = 0; x < VGA_WIDTH; x++)
                VGA[(y - 1) * VGA_WIDTH + x] = VGA[y * VGA_WIDTH + x];
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA[(SCROLL_LAST - 1) * VGA_WIDTH + x] = blank;
        if (fbcon_active()) fbcon_repaint();
    }
}

/* Auto-mirror: the last completed text line (everything printed
 * since the previous '\n') gets copied to the status row whenever
 * a newline is printed. Lets us see "what's happening now" without
 * any app-level cooperation. */
static char g_lastline[VGA_WIDTH + 1];
static int  g_lastline_pos;

/* Last completed status line (for graphics-mode overlay). */
static char g_status_text[VGA_WIDTH + 1];

/* Rolling log: last LOG_LINES messages, accessible by gfx overlay so
 * we can see history of what happened up to a freeze/crash. */
#define LOG_LINES 16
#define LOG_LINEWIDTH 80
static char g_log[LOG_LINES][LOG_LINEWIDTH + 1];
static int  g_log_count;     /* total messages ever written       */

/* Disk-backed log: every status update is also written to LBAs
 * 65520..65535 of the FAT12 disk (8 KiB, past the FAT12 data area).
 * After the VM stops, the host reads the qcow2 directly:
 *   qemu-img convert -O raw boxos-fs.qcow2 /tmp/fs.img
 *   tail -c 8192 /tmp/fs.img       # last 8 KiB = log
 * Format: each entry is one 80-byte line, NUL-padded. 100 entries
 * fit. After 100, wraps around. */
#define DISK_LOG_LBA_START   65520
#define DISK_LOG_SECTORS     16
#define DISK_LOG_BYTES       (DISK_LOG_SECTORS * 512)
#define DISK_LOG_LINEWIDTH   80
#define DISK_LOG_ENTRIES     (DISK_LOG_BYTES / DISK_LOG_LINEWIDTH)
static uint8_t disk_log_buf[DISK_LOG_BYTES];
static int     disk_log_pos;
static int     disk_log_drive_bus = -1;
static int     disk_log_drive     = -1;

void vga_log_set_disk(int bus, int drive) {
    disk_log_drive_bus = bus;
    disk_log_drive     = drive;
    /* Initialise the log region to all 0xFF so empty slots are obvious. */
    for (int i = 0; i < DISK_LOG_BYTES; i++) disk_log_buf[i] = 0xFF;
    disk_log_pos = 0;
}

static void disk_log_flush(void) {
    if (disk_log_drive_bus < 0) return;
    /* Write 8 sectors at a time (max ATA single-burst). */
    for (int sec = 0; sec < DISK_LOG_SECTORS; sec += 8) {
        int n = (DISK_LOG_SECTORS - sec) > 8 ? 8 : (DISK_LOG_SECTORS - sec);
        ata_write(disk_log_drive_bus, disk_log_drive,
                  DISK_LOG_LBA_START + sec, (uint8_t)n,
                  &disk_log_buf[sec * 512]);
    }
}

static void log_push(const char* msg) {
    int slot = g_log_count % LOG_LINES;
    int i = 0;
    while (msg && msg[i] && i < LOG_LINEWIDTH) {
        g_log[slot][i] = msg[i];
        i++;
    }
    g_log[slot][i] = 0;
    g_log_count++;

    /* Also append to the disk log buffer (ring), then flush. */
    if (disk_log_drive_bus >= 0) {
        int slot_off = (disk_log_pos % DISK_LOG_ENTRIES) * DISK_LOG_LINEWIDTH;
        int j = 0;
        while (msg && msg[j] && j < DISK_LOG_LINEWIDTH - 1) {
            disk_log_buf[slot_off + j] = (uint8_t)msg[j];
            j++;
        }
        /* Pad the rest of the slot with NULs so trailing junk doesn't leak. */
        while (j < DISK_LOG_LINEWIDTH) disk_log_buf[slot_off + j++] = 0;
        disk_log_pos++;
        disk_log_flush();
    }
}

const char* vga_status_text(void) { return g_status_text; }
const char* vga_log_line(int n) {
    /* Returns line n of the rolling log, where n=0 is the OLDEST
     * still in the buffer and n=LOG_LINES-1 is the newest. Empty
     * string if not enough log entries yet. */
    if (n < 0 || n >= LOG_LINES) return "";
    int total = g_log_count;
    int oldest_slot;
    if (total <= LOG_LINES) {
        if (n >= total) return "";
        oldest_slot = 0;
    } else {
        oldest_slot = total % LOG_LINES;
    }
    int slot = (oldest_slot + n) % LOG_LINES;
    return g_log[slot];
}
int vga_log_lines(void) {
    return g_log_count < LOG_LINES ? g_log_count : LOG_LINES;
}

static void blit_status_line(const char* msg) {
    uint8_t status_attr = (uint8_t)((VGA_BLUE & 0x0F) << 4) | (VGA_WHITE & 0x0F);
    int row = STATUS_ROW;
    int x = 0;
    while (msg && *msg && x < VGA_WIDTH) {
        VGA[row * VGA_WIDTH + x] = cell(*msg++, status_attr);
        x++;
    }
    while (x < VGA_WIDTH) {
        VGA[row * VGA_WIDTH + x] = cell(' ', status_attr);
        x++;
    }
}

/* Overwrite the status row with `msg`, padded to full width with
 * spaces. Cursor position is preserved. Color: white on blue.
 * Also updates g_status_text + appends to rolling log. */
void vga_status_line(const char* msg) {
    /* The blue "current status" bar at row 0 was getting in the way.
     * Keep the rolling-log + disk-log behaviour (so post-mortem
     * debugging still works) but skip the on-screen paint. The row 0
     * cells stay as ordinary text — see vga_clear below. */
    int i = 0;
    while (msg && msg[i] && i < VGA_WIDTH) { g_status_text[i] = msg[i]; i++; }
    g_status_text[i] = 0;
    log_push(g_status_text);
    g_lastline_pos = 0;
    g_lastline[0] = 0;
}

void vga_putc(char c) {
    if (c == '\n') {
        /* Mirror the just-completed line through the unified status
         * path so it lands on row 0, in the rolling log, and on disk. */
        g_lastline[g_lastline_pos] = 0;
        if (g_lastline_pos > 0) {
            vga_status_line(g_lastline);
        }
        g_lastline_pos = 0;
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
        g_lastline_pos = 0;
    } else if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            uint16_t v = cell(' ', g_attr);
            if (sb_view_offset == 0) VGA[g_row * VGA_WIDTH + g_col] = v;
            sb_put(sb_top + g_row - SCROLL_FIRST, g_col, v);
            if (fbcon_active()) fbcon_paint_cell(g_row, g_col);
        }
        if (g_lastline_pos > 0) g_lastline_pos--;
    } else if (c == '\t') {
        do { vga_putc(' '); } while (g_col % 4);
        return;
    } else {
        uint16_t v = cell(c, g_attr);
        if (sb_view_offset == 0) VGA[g_row * VGA_WIDTH + g_col] = v;
        sb_put(sb_top + g_row - SCROLL_FIRST, g_col, v);
        if (fbcon_active()) fbcon_paint_cell(g_row, g_col);
        g_col++;
        if (g_col >= VGA_WIDTH) { g_col = 0; g_row++; }
        if (g_lastline_pos < VGA_WIDTH) g_lastline[g_lastline_pos++] = c;
    }
    /* Status row 0 is reserved; if the cursor wandered there from a
     * fresh boot, snap it to row 1. If it reaches the bottom, scroll
     * the body region (rows 1..23). */
    if (g_row < SCROLL_FIRST) g_row = SCROLL_FIRST;
    if (g_row >= SCROLL_LAST) {
        scroll();
        g_row = SCROLL_LAST - 1;
    }
    update_cursor();
}

void vga_puts(const char* s) { while (*s) vga_putc(*s++); }

/* ---- scroll-back nav ---------------------------------------------- */

static int sb_max_offset(void) {
    int visible = SCROLL_LAST - SCROLL_FIRST;
    /* Don't let the user scroll into rows that have wrapped past the
     * circular-buffer horizon. */
    int cap = SB_LINES - visible;
    if (sb_top < cap) cap = sb_top;
    if (cap < 0) cap = 0;
    return cap;
}

void vga_scroll_up(int rows) {
    int max = sb_max_offset();
    int new_off = sb_view_offset + rows;
    if (new_off > max) new_off = max;
    if (new_off == sb_view_offset) return;
    sb_view_offset = new_off;
    sb_redraw();
}

void vga_scroll_down(int rows) {
    int new_off = sb_view_offset - rows;
    if (new_off < 0) new_off = 0;
    if (new_off == sb_view_offset) return;
    sb_view_offset = new_off;
    sb_redraw();
}

void vga_scroll_to_bottom(void) {
    if (sb_view_offset == 0) return;
    sb_view_offset = 0;
    sb_redraw();
}

bool vga_is_scrolled(void) { return sb_view_offset > 0; }

static void put_uint(uint64_t v, unsigned base, int upper, int width, char pad) {
    char buf[32];
    int n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) buf[n++] = '0';
    while (v) { buf[n++] = digits[v % base]; v /= base; }
    while (n < width) buf[n++] = pad;
    while (n--) vga_putc(buf[n]);
}

static void put_int(int64_t v, int width, char pad) {
    if (v < 0) { vga_putc('-'); v = -v; if (width) width--; }
    put_uint((uint64_t)v, 10, 0, width, pad);
}

static size_t mini_strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}

static void put_str(const char* s, int width, bool left, char pad) {
    int len = (int)mini_strlen(s);
    if (!left) for (int p = len; p < width; p++) vga_putc(pad);
    while (*s) vga_putc(*s++);
    if  (left) for (int p = len; p < width; p++) vga_putc(pad);
}

void vga_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { vga_putc(*fmt++); continue; }
        fmt++;
        bool left = false;
        char pad = ' ';
        int  width = 0;
        if (*fmt == '-') { left = true; fmt++; }
        if (*fmt == '0') { pad  = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
            case 'd': case 'i': put_int((int64_t)va_arg(ap, int), width, pad); break;
            case 'u':           put_uint((uint64_t)va_arg(ap, unsigned), 10, 0, width, pad); break;
            case 'x':           put_uint((uint64_t)va_arg(ap, unsigned), 16, 0, width, pad); break;
            case 'X':           put_uint((uint64_t)va_arg(ap, unsigned), 16, 1, width, pad); break;
            case 'p': vga_puts("0x"); put_uint((uint64_t)(uintptr_t)va_arg(ap, void*), 16, 0, 16, '0'); break;
            case 'l': {
                fmt++;
                if (*fmt == 'd' || *fmt == 'i') put_int(va_arg(ap, int64_t), width, pad);
                else                            put_uint(va_arg(ap, uint64_t), *fmt == 'x' ? 16 : 10, 0, width, pad);
                break;
            }
            case 'c': vga_putc((char)va_arg(ap, int)); break;
            case 's': put_str(va_arg(ap, const char*), width, left, pad); break;
            case '%': vga_putc('%'); break;
            default:  vga_putc('%'); vga_putc(*fmt); break;
        }
        if (*fmt) fmt++;
    }
    va_end(ap);
}
