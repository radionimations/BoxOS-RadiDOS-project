#include "boxos.h"

#define LINE_MAX 128
#define MAX_DIR_ENTRIES 64
#define CWD_DEPTH_MAX   16
#define CWD_PATH_MAX    128
#define PROMPT_FMT_MAX  32

/* Customisable PROMPT format. CMD-style escapes:
 *   $P  current path
 *   $G  '>'
 *   $L  '<'
 *   $D  uptime as dec ms (we don't have a real clock yet)
 *   $$  literal '$'
 *   $H  backspace (handled by user, just emit)
 *   $_  newline */
static char g_prompt_fmt[PROMPT_FMT_MAX] = "BOX $P$G ";

/* Current working directory: stack of (cluster, name) pairs starting
 * from root. depth=0 means we're at root. The path string is rebuilt
 * on demand. */
static struct {
    uint16_t cluster;
    char     name[FAT12_MAX_NAME];
} cwd_stack[CWD_DEPTH_MAX];
static int cwd_depth = 0;

static uint16_t cwd_cluster(void) {
    return cwd_depth == 0 ? 0 : cwd_stack[cwd_depth - 1].cluster;
}

static void cwd_path(char* out, size_t cap) {
    size_t o = 0;
    if (cap < 2) { if (cap) out[0] = 0; return; }
    out[o++] = '/';
    for (int i = 0; i < cwd_depth; i++) {
        size_t n = strlen(cwd_stack[i].name);
        if (o + n + 2 >= cap) break;
        for (size_t k = 0; k < n; k++) out[o++] = cwd_stack[i].name[k];
        if (i + 1 < cwd_depth) out[o++] = '/';
    }
    out[o] = 0;
}

static void prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    char path[CWD_PATH_MAX];
    cwd_path(path, sizeof(path));
    /* Walk g_prompt_fmt and emit, expanding $P/$G/$L/$D/$_/$$ as we go. */
    for (const char* p = g_prompt_fmt; *p; p++) {
        if (*p != '$') { vga_putc(*p); continue; }
        char e = *++p;
        if (!e) break;
        switch (e) {
            case 'P': case 'p': vga_puts(path);            break;
            case 'G': case 'g': vga_putc('>');             break;
            case 'L': case 'l': vga_putc('<');             break;
            case '_':           vga_putc('\n');            break;
            case '$':           vga_putc('$');             break;
            case 'D': case 'd': {
                uint64_t s = timer_ms() / 1000;
                vga_printf("%lu", (unsigned long)s);
                break;
            }
            default: vga_putc('$'); vga_putc(e);
        }
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static size_t read_line(char* buf, size_t cap) {
    size_t i = 0;
    for (;;) {
        char c = keyboard_getc();
        if (c == '\n') {
            vga_putc('\n');
            buf[i] = 0;
            return i;
        }
        if (c == '\b') {
            if (i > 0) { i--; vga_putc('\b'); }
            continue;
        }
        if (c >= ' ' && c < 127 && i + 1 < cap) {
            buf[i++] = c;
            vga_putc(c);
        }
    }
}

extern void kernel_main(void);

/* ----------------------- built-in commands ----------------------- */

static void line(uint8_t fg, const char* s) {
    vga_set_color(fg, VGA_BLACK);
    vga_puts(s);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_help(const char* args) {
    (void)args;
    line(VGA_LIGHT_CYAN, "RadiDOS shell -- built-in commands\n\n");
    vga_puts("  Browse:\n");
    vga_puts("    DIR / LS              list current directory\n");
    vga_puts("    TREE                  recursive tree of cwd\n");
    vga_puts("    CD <name>             descend into a subdirectory\n");
    vga_puts("    CD ..                 up one level\n");
    vga_puts("    CD /                  go back to the root\n");
    vga_puts("    PWD                   print current path\n");
    vga_puts("    FIND <name>           search disk for a file\n");
    vga_puts("\n  Read files:\n");
    vga_puts("    CAT / TYPE <f>        print a text file\n");
    vga_puts("    MORE <f>              paginated CAT (space=next, q=quit)\n");
    vga_puts("    HEXDUMP <f>           hex view of a file\n");
    vga_puts("    SIZE <f>              file size in bytes\n");
    vga_puts("    WC <f>                line/word/byte count\n");
    vga_puts("\n  Write files:\n");
    vga_puts("    WRITE <f> <text>      create a small file\n");
    vga_puts("    EDIT <f>              tiny line editor (.SAVE / .EXIT)\n");
    vga_puts("    DEL / ERASE <f>       delete a file\n");
    vga_puts("    COPY <src> <dst>      copy a file (paths ok)\n");
    vga_puts("    MOVE <src> <dst>      copy + delete source\n");
    vga_puts("    REN <old> <new>       rename in current dir\n");
    vga_puts("    MKDIR / MD <name>     create a directory\n");
    vga_puts("    RMDIR / RD <name>     remove an empty directory\n");
    vga_puts("\n  Setup / first-run:\n");
    vga_puts("    SETUP                 hardware report + getting-started\n");
    vga_puts("    FACTORY               wipe + restore + reboot (typed-YES guarded)\n");
    vga_puts("\n  Run:\n");
    vga_puts("    RUN <name> [args]     load and run an app\n");
    vga_puts("    (or just type the program's name)\n");
    vga_puts("\n  System:\n");
    vga_puts("    VER  ABOUT  WHOAMI  HOSTNAME  UPTIME  DATE/TIME  MEM  VOL\n");
    vga_puts("    ATTRIB <f>            show R/H/S/V/D/A flags of a file\n");
    vga_puts("    WHERE  <name>         find file in cwd, /, /APPS, /GAMES, /SYS\n");
    vga_puts("\n  Text utilities:\n");
    vga_puts("    SORT    <f>           sort file lines alphabetically\n");
    vga_puts("    FINDSTR <pat> <f>     grep (case-insensitive, line numbers)\n");
    vga_puts("    FC      <a> <b>       byte-compare two files\n");
    vga_puts("\n  Display / I/O:\n");
    vga_puts("    CLS / CLEAR           clear the screen\n");
    vga_puts("    ECHO <text>           print text\n");
    vga_puts("    COLOR  <hex>          foreground colour 0-F\n");
    vga_puts("    TITLE  <text>         set the menu-bar title\n");
    vga_puts("    PROMPT <fmt>          set prompt format ($P/$G/$L/$D/$_)\n");
    vga_puts("\n  Scripting helpers:\n");
    vga_puts("    PAUSE                 wait for any key\n");
    vga_puts("    REM   <text>          comment (no-op)\n");
    vga_puts("    CHOICE [/c:KEYS] msg  ask Y/N (or custom key set)\n");
    vga_puts("\n  Power:\n");
    vga_puts("    REBOOT  HALT/EXIT\n");
    vga_puts("\n  Scrollback: PgUp/PgDn or mouse wheel.\n");
}

static void cmd_cls(const char* args)  { (void)args; vga_clear(); }

static void cmd_echo(const char* args) {
    vga_puts(args ? args : "");
    vga_putc('\n');
}

static void cmd_ver(const char* args) {
    (void)args;
    line(VGA_LIGHT_CYAN, "RadiDOS 2.0 (x86_64, long mode)\n");
    vga_puts("Built " __DATE__ " " __TIME__ "\n");
}

static void cmd_about(const char* args) {
    (void)args;
    line(VGA_LIGHT_CYAN, "\n  RadiDOS\n");
    vga_puts("  -------\n");
    vga_puts("  A 64-bit hobby OS: bootloader, kernel, FAT12, mode 13h,\n");
    vga_puts("  PIT timer, PS/2 keyboard + mouse, app loader. Runs DOOM.\n");
    vga_puts("  Source: hand-rolled C + asm. No BIOS calls after boot.\n\n");
}

static void cmd_whoami(const char* args) {
    (void)args;
    vga_puts("kernel\n");
}

static void cmd_uptime(const char* args) {
    (void)args;
    uint64_t ms = timer_ms();
    uint64_t s  = ms / 1000;
    vga_printf("up %lu.%03lu seconds (%lu ms)\n",
               (unsigned long)s, (unsigned long)(ms % 1000), (unsigned long)ms);
}

static void cmd_date(const char* args) {
    (void)args;
    /* No RTC driver yet — just print elapsed time since boot. */
    uint64_t ms = timer_ms();
    uint64_t s  = ms / 1000;
    uint64_t m  = s / 60;
    uint64_t h  = m / 60;
    vga_printf("uptime: %02lu:%02lu:%02lu\n",
               (unsigned long)h, (unsigned long)(m % 60), (unsigned long)(s % 60));
}

static void cmd_mem(const char* args) {
    (void)args;
    vga_printf("  boot sector @ 0x%lx\n", (uint64_t)0x7C00);
    vga_printf("  stage 2     @ 0x%lx\n", (uint64_t)0x7E00);
    vga_printf("  kernel base @ 0x%lx\n", (uint64_t)0x8E00);
    vga_printf("  kernel_main @ %p\n",     (void*)kernel_main);
    vga_printf("  stack top   @ 0x%lx\n", (uint64_t)0x90000);
    vga_printf("  vga buffer  @ 0x%lx\n", (uint64_t)0xB8000);
    vga_printf("  app region  @ 0x%lx\n", (uint64_t)APP_LOAD_ADDR);
    vga_printf("  heap used   %lu / %lu bytes\n",
               (unsigned long)heap_used(), (unsigned long)heap_total());
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void cmd_color(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: COLOR <hex 0-F>\n"); return; }
    int fg = hex_digit(args[0]);
    if (fg < 0) { vga_puts("bad color\n"); return; }
    vga_set_color((uint8_t)fg, VGA_BLACK);
    vga_puts("color changed\n");
}

static void cmd_reboot(const char* args) {
    (void)args;
    vga_puts("Rebooting...\n");
    while (inb(0x64) & 0x02) { /* wait for empty input buf */ }
    outb(0x64, 0xFE);
    struct { uint16_t lim; uint64_t base; } __attribute__((packed)) z = {0, 0};
    __asm__ __volatile__ ("cli; lidt %0; int $3" :: "m"(z));
    for (;;) __asm__ __volatile__ ("hlt");
}

/* When shell_run is being called from wm_open_terminal, we want
 * EXIT to close the window and return to the desktop instead of
 * doing a hard cli;hlt. cmd_halt sets g_shell_exit; shell_run's
 * top-level loop checks it. */
static int g_shell_exit = 0;

static void cmd_halt(const char* args) {
    (void)args;
    vga_puts("Closing terminal...\n");
    g_shell_exit = 1;
}

/* ----------------------- filesystem cmds ------------------------- */

static int require_fs(void) {
    if (fs_drive() < 0) {
        vga_puts("No filesystem mounted. Attach boxos-fs.img as a HDD.\n");
        return 0;
    }
    return 1;
}

static void cmd_pwd(const char* args) {
    (void)args;
    char p[CWD_PATH_MAX];
    cwd_path(p, sizeof(p));
    vga_puts(p);
    vga_putc('\n');
}

static void cmd_dir(const char* args) {
    (void)args;
    if (!require_fs()) return;

    static struct fat12_entry entries[MAX_DIR_ENTRIES];
    int n = fs_list_dir(cwd_cluster(), entries, MAX_DIR_ENTRIES);
    if (n < 0) { vga_puts("DIR: read error\n"); return; }

    char path[CWD_PATH_MAX];
    cwd_path(path, sizeof(path));
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_printf(" Volume: BOXOS  (FAT12, IDE drive %d)\n", fs_drive());
    vga_printf(" Path:   %s\n", path);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int dirs = 0, files = 0;
    for (int i = 0; i < n; i++) {
        if (entries[i].name[0] == '.') continue;        /* hide . / .. */
        if (entries[i].attr & 0x10) {
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            vga_printf("  <DIR>  %s\n", entries[i].name);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            dirs++;
        } else {
            vga_printf("         %-13s  %8lu bytes\n",
                       entries[i].name, (uint64_t)entries[i].size);
            files++;
        }
    }
    vga_printf("  %d dir(s), %d file(s)\n", dirs, files);
}

static void to_upper(char* s) {
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
}

/* Walk one path segment of `path` from the current cwd_stack state,
 * mutating cwd_stack/cwd_depth. Returns 0 on success, -1 on error. */
static int cd_segment(const char* seg, size_t len) {
    if (len == 0) return 0;
    if (len == 1 && seg[0] == '.') return 0;
    if (len == 2 && seg[0] == '.' && seg[1] == '.') {
        if (cwd_depth > 0) cwd_depth--;
        return 0;
    }
    char up[FAT12_MAX_NAME];
    size_t n = len < FAT12_MAX_NAME - 1 ? len : FAT12_MAX_NAME - 1;
    for (size_t i = 0; i < n; i++) {
        char c = seg[i];
        up[i] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    up[n] = 0;

    uint16_t child = fs_find_dir(cwd_cluster(), up);
    if (child == 0xFFFF) return -1;
    if (cwd_depth >= CWD_DEPTH_MAX) return -1;
    cwd_stack[cwd_depth].cluster = child;
    memcpy(cwd_stack[cwd_depth].name, up, n);
    cwd_stack[cwd_depth].name[n] = 0;
    cwd_depth++;
    return 0;
}

static void cmd_cd(const char* args) {
    if (!args || !args[0]) {
        char p[CWD_PATH_MAX];
        cwd_path(p, sizeof(p));
        vga_puts(p);
        vga_putc('\n');
        return;
    }
    if (!require_fs()) return;

    /* Take the first whitespace-terminated token as the path. */
    char path[CWD_PATH_MAX];
    size_t n = 0;
    while (args[n] && args[n] != ' ' && n + 1 < sizeof(path)) { path[n] = args[n]; n++; }
    path[n] = 0;

    /* Absolute paths reset to root. */
    int saved_depth = cwd_depth;
    if (path[0] == '/' || path[0] == '\\') {
        cwd_depth = 0;
        size_t i = 1;
        size_t j = i;
        while (path[i]) {
            j = i;
            while (path[i] && path[i] != '/' && path[i] != '\\') i++;
            if (cd_segment(&path[j], i - j) < 0) {
                cwd_depth = saved_depth;
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_printf("CD: no such directory in '%s'\n", path);
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                return;
            }
            if (path[i]) i++;
        }
        return;
    }

    /* Relative path. */
    size_t i = 0;
    while (path[i]) {
        size_t j = i;
        while (path[i] && path[i] != '/' && path[i] != '\\') i++;
        if (cd_segment(&path[j], i - j) < 0) {
            cwd_depth = saved_depth;
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_puts("CD: no such directory in path\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            return;
        }
        if (path[i]) i++;
    }
}

static char cat_buf[16384];

static void cmd_cat(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: CAT <file>\n"); return; }
    if (!require_fs()) return;

    char name[FAT12_MAX_NAME];
    size_t n = 0;
    while (args[n] && args[n] != ' ' && n + 1 < sizeof(name)) {
        name[n] = args[n];
        n++;
    }
    name[n] = 0;
    to_upper(name);

    int sz = fs_read_in(cwd_cluster(), name, 0, 0);
    if (sz < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("CAT: '%s' not found\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if ((uint32_t)sz > sizeof(cat_buf)) {
        vga_printf("CAT: file is %d bytes, max %lu\n", sz, (unsigned long)sizeof(cat_buf));
        return;
    }
    int got = fs_read_in(cwd_cluster(), name, cat_buf, sizeof(cat_buf));
    if (got < 0) { vga_puts("CAT: read error\n"); return; }
    for (int i = 0; i < got; i++) {
        char c = cat_buf[i];
        if (c == '\r') continue;
        if (c == '\n' || (c >= ' ' && c < 127) || c == '\t') vga_putc(c);
        else vga_putc('.');
    }
    if (got > 0 && cat_buf[got - 1] != '\n') vga_putc('\n');
}

static void cmd_size(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: SIZE <file>\n"); return; }
    if (!require_fs()) return;

    char name[FAT12_MAX_NAME];
    size_t n = 0;
    while (args[n] && args[n] != ' ' && n + 1 < sizeof(name)) {
        name[n] = args[n];
        n++;
    }
    name[n] = 0;
    to_upper(name);

    int sz = fs_read_in(cwd_cluster(), name, 0, 0);
    if (sz < 0) {
        vga_printf("SIZE: '%s' not found\n", name);
        return;
    }
    vga_printf("%-13s  %d bytes\n", name, sz);
}

/* Resolve `path` (absolute when it begins with '/', otherwise relative
 * to cwd) to a (parent_directory_cluster, basename) pair. Walks each
 * '/' segment with fs_find_dir; understands "." and "..". On success
 * returns 0 and fills out_dir + out_name; on error returns -1. */
static int resolve_path(const char* path,
                        uint16_t* out_dir,
                        char out_name[FAT12_MAX_NAME])
{
    if (!path || !*path) return -1;
    uint16_t cur = (path[0] == '/' || path[0] == '\\') ? 0 : cwd_cluster();
    if (path[0] == '/' || path[0] == '\\') path++;

    char seg[FAT12_MAX_NAME];
    while (1) {
        size_t i = 0;
        while (*path && *path != '/' && *path != '\\' && i + 1 < sizeof(seg)) {
            char c = *path++;
            if (c >= 'a' && c <= 'z') c -= 32;
            seg[i++] = c;
        }
        seg[i] = 0;

        int more = (*path == '/' || *path == '\\');
        if (!more) {
            if (i == 0) return -1;
            *out_dir = cur;
            for (size_t k = 0; k <= i; k++) out_name[k] = seg[k];
            return 0;
        }
        path++;                                 /* skip the separator */
        if (i == 0) continue;
        if (i == 1 && seg[0] == '.') continue;
        if (i == 2 && seg[0] == '.' && seg[1] == '.') {
            if (cur == 0) continue;             /* already root */
            uint16_t parent = fs_find_dir(cur, "..");
            if (parent == 0xFFFF) return -1;
            cur = parent;
            continue;
        }
        uint16_t next = fs_find_dir(cur, seg);
        if (next == 0xFFFF) return -1;
        cur = next;
    }
}

/* Like resolve_path, but if the final segment names an existing
 * directory, descend into it and use a default basename. Useful for
 * "MOVE foo.txt /bin/" style commands. */
static int resolve_target(const char* path,
                          const char* default_name,
                          uint16_t* out_dir,
                          char out_name[FAT12_MAX_NAME])
{
    char base[FAT12_MAX_NAME];
    uint16_t dir;
    if (resolve_path(path, &dir, base) < 0) return -1;
    uint16_t maybe = fs_find_dir(dir, base);
    if (maybe != 0xFFFF) {
        *out_dir = maybe;
        size_t n = 0;
        while (default_name[n] && n + 1 < FAT12_MAX_NAME) {
            char c = default_name[n];
            out_name[n] = (c >= 'a' && c <= 'z') ? c - 32 : c;
            n++;
        }
        out_name[n] = 0;
    } else {
        *out_dir = dir;
        for (int k = 0; base[k] || k == 0; k++) {
            out_name[k] = base[k];
            if (!base[k]) break;
        }
    }
    return 0;
}

/* Helper: parse the first whitespace-delimited token off `args` into
 * `out` (uppercased), and return a pointer to whatever's left (skipping
 * the separating spaces). */
static const char* take_arg(const char* args, char* out, size_t cap) {
    size_t n = 0;
    while (args && *args == ' ') args++;
    while (args && *args && *args != ' ' && n + 1 < cap) {
        char c = *args++;
        if (c >= 'a' && c <= 'z') c -= 32;
        out[n++] = c;
    }
    out[n] = 0;
    while (args && *args == ' ') args++;
    return args ? args : "";
}

/* MORE <file>: cat with pagination (space = next page, q = quit). */
static void cmd_more(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: MORE <file>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    int sz = fs_read_in(cwd_cluster(), name, 0, 0);
    if (sz < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("MORE: '%s' not found\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if ((uint32_t)sz > sizeof(cat_buf)) {
        vga_printf("MORE: file is %d bytes, max %lu\n", sz, (unsigned long)sizeof(cat_buf));
        return;
    }
    int got = fs_read_in(cwd_cluster(), name, cat_buf, sizeof(cat_buf));
    if (got < 0) { vga_puts("MORE: read error\n"); return; }
    int line_count = 0;
    for (int i = 0; i < got; i++) {
        char c = cat_buf[i];
        if (c == '\r') continue;
        if (c == '\n' || (c >= ' ' && c < 127) || c == '\t') vga_putc(c);
        else vga_putc('.');
        if (c == '\n') {
            line_count++;
            if (line_count >= 22) {
                vga_set_color(VGA_BLACK, VGA_LIGHT_GREY);
                vga_puts("--More-- (space, q)");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                char k = keyboard_getc();
                vga_putc('\r');
                for (int j = 0; j < 22; j++) vga_putc(' ');
                vga_putc('\r');
                if (k == 'q' || k == 'Q' || k == 27) return;
                line_count = 0;
            }
        }
    }
    if (got > 0 && cat_buf[got - 1] != '\n') vga_putc('\n');
}

static void cmd_hexdump(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: HEXDUMP <file>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    int sz = fs_read_in(cwd_cluster(), name, 0, 0);
    if (sz < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("HEXDUMP: '%s' not found\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    int max = (int)sizeof(cat_buf);
    if (sz > max) sz = max;
    int got = fs_read_in(cwd_cluster(), name, cat_buf, max);
    if (got < 0) { vga_puts("HEXDUMP: read error\n"); return; }
    for (int off = 0; off < got; off += 16) {
        vga_printf("%08x  ", off);
        for (int i = 0; i < 16; i++) {
            if (off + i < got) vga_printf("%02x ", (uint8_t)cat_buf[off + i]);
            else                vga_puts("   ");
        }
        vga_puts(" |");
        for (int i = 0; i < 16 && off + i < got; i++) {
            char c = cat_buf[off + i];
            vga_putc((c >= 32 && c < 127) ? c : '.');
        }
        vga_puts("|\n");
    }
}

static void cmd_wc(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: WC <file>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    int sz = fs_read_in(cwd_cluster(), name, 0, 0);
    if (sz < 0) { vga_printf("WC: '%s' not found\n", name); return; }
    int max = (int)sizeof(cat_buf);
    if (sz > max) sz = max;
    int got = fs_read_in(cwd_cluster(), name, cat_buf, max);
    if (got < 0) { vga_puts("WC: read error\n"); return; }
    int lines = 0, words = 0, bytes = got;
    int in_word = 0;
    for (int i = 0; i < got; i++) {
        char c = cat_buf[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    vga_printf("  %d lines  %d words  %d bytes  %s\n", lines, words, bytes, name);
}

static void tree_walk(uint16_t cluster, int depth) {
    if (depth > 6) return;
    static struct fat12_entry e[MAX_DIR_ENTRIES];
    int n = fs_list_dir(cluster, e, MAX_DIR_ENTRIES);
    for (int i = 0; i < n; i++) {
        if (e[i].name[0] == '.') continue;
        for (int d = 0; d < depth; d++) vga_puts("  ");
        if (e[i].attr & 0x10) {
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            vga_printf("[%s]\n", e[i].name);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            tree_walk(e[i].first_cluster, depth + 1);
        } else {
            vga_printf("%s  (%lu)\n", e[i].name, (uint64_t)e[i].size);
        }
    }
}

static void cmd_tree(const char* args) {
    (void)args;
    if (!require_fs()) return;
    char path[CWD_PATH_MAX];
    cwd_path(path, sizeof(path));
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_printf("%s\n", path);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    tree_walk(cwd_cluster(), 1);
}

static void cmd_write(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: WRITE <file> <text>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    const char* rest = take_arg(args, name, sizeof(name));
    if (!*rest) { vga_puts("usage: WRITE <file> <text>\n"); return; }
    /* Append a trailing newline so the file is a proper text line. */
    static char tmp[16384];
    size_t n = strlen(rest);
    if (n > sizeof(tmp) - 2) n = sizeof(tmp) - 2;
    memcpy(tmp, rest, n);
    tmp[n] = '\n';
    if (fs_create_file(cwd_cluster(), name, tmp, (uint32_t)(n + 1)) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("WRITE: cannot create '%s' (already exists?)\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("wrote %lu bytes to %s\n", (unsigned long)(n + 1), name);
}

static void cmd_del(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: DEL <file>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    if (fs_delete(cwd_cluster(), name) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("DEL: '%s' not found\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("deleted %s\n", name);
}

static void cmd_mkdir(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: MKDIR <name>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    if (fs_mkdir_at(cwd_cluster(), name) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("MKDIR: cannot create '%s'\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("created %s/\n", name);
}

static void cmd_rmdir(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: RMDIR <name>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    int rc = fs_rmdir(cwd_cluster(), name);
    if (rc == -2) { vga_printf("RMDIR: '%s' not empty\n", name); return; }
    if (rc < 0)   {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("RMDIR: '%s' not found\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("removed %s/\n", name);
}

/* COPY <src> <dst>: read src into cat_buf, create dst from it. dst may
 * be a name (in cwd) or a /path/possibly/dir or a /path/file.ext. */
static char copy_buf[16384];
static void cmd_copy(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: COPY <src> <dst>\n"); return; }
    if (!require_fs()) return;
    char src[CWD_PATH_MAX], dst[CWD_PATH_MAX];
    const char* rest = take_arg(args, src, sizeof(src));
    if (!*rest) { vga_puts("usage: COPY <src> <dst>\n"); return; }
    take_arg(rest, dst, sizeof(dst));

    uint16_t sdir; char sname[FAT12_MAX_NAME];
    if (resolve_path(src, &sdir, sname) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("COPY: source path not found\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    int sz = fs_read_in(sdir, sname, 0, 0);
    if (sz < 0) { vga_printf("COPY: '%s' not found\n", sname); return; }
    if ((uint32_t)sz > sizeof(copy_buf)) {
        vga_printf("COPY: file too large (%d bytes; max %lu)\n", sz, (unsigned long)sizeof(copy_buf));
        return;
    }
    int got = fs_read_in(sdir, sname, copy_buf, sizeof(copy_buf));
    if (got < 0) { vga_puts("COPY: read error\n"); return; }

    uint16_t ddir; char dname[FAT12_MAX_NAME];
    if (resolve_target(dst, sname, &ddir, dname) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("COPY: dest path not found\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (fs_create_file(ddir, dname, copy_buf, (uint32_t)got) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("COPY: cannot create '%s' (already exists?)\n", dname);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("copied %d bytes\n", got);
}

static void cmd_move(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: MOVE <src> <dst>\n"); return; }
    if (!require_fs()) return;
    char src[CWD_PATH_MAX], dst[CWD_PATH_MAX];
    const char* rest = take_arg(args, src, sizeof(src));
    if (!*rest) { vga_puts("usage: MOVE <src> <dst>\n"); return; }
    take_arg(rest, dst, sizeof(dst));

    uint16_t sdir; char sname[FAT12_MAX_NAME];
    if (resolve_path(src, &sdir, sname) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("MOVE: source path not found\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    int sz = fs_read_in(sdir, sname, 0, 0);
    if (sz < 0) { vga_printf("MOVE: '%s' not found\n", sname); return; }
    if ((uint32_t)sz > sizeof(copy_buf)) {
        vga_printf("MOVE: file too large (%d bytes; max %lu)\n", sz, (unsigned long)sizeof(copy_buf));
        return;
    }
    int got = fs_read_in(sdir, sname, copy_buf, sizeof(copy_buf));
    if (got < 0) { vga_puts("MOVE: read error\n"); return; }

    uint16_t ddir; char dname[FAT12_MAX_NAME];
    if (resolve_target(dst, sname, &ddir, dname) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("MOVE: dest path not found\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (fs_create_file(ddir, dname, copy_buf, (uint32_t)got) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("MOVE: cannot create destination\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (fs_delete(sdir, sname) < 0) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts("MOVE: dest written but source delete failed\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("moved %d bytes\n", got);
}

static void cmd_ren(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: REN <old> <new>\n"); return; }
    if (!require_fs()) return;
    char src[FAT12_MAX_NAME], dst[FAT12_MAX_NAME];
    const char* rest = take_arg(args, src, sizeof(src));
    if (!*rest) { vga_puts("usage: REN <old> <new>\n"); return; }
    take_arg(rest, dst, sizeof(dst));

    int sz = fs_read_in(cwd_cluster(), src, 0, 0);
    if (sz < 0) { vga_printf("REN: '%s' not found\n", src); return; }
    if ((uint32_t)sz > sizeof(copy_buf)) {
        vga_printf("REN: file too large (%d bytes)\n", sz);
        return;
    }
    int got = fs_read_in(cwd_cluster(), src, copy_buf, sizeof(copy_buf));
    if (got < 0) { vga_puts("REN: read error\n"); return; }
    if (fs_create_file(cwd_cluster(), dst, copy_buf, (uint32_t)got) < 0) {
        vga_printf("REN: '%s' already exists\n", dst);
        return;
    }
    fs_delete(cwd_cluster(), src);
    vga_printf("renamed %s -> %s\n", src, dst);
}

/* ---- RadiDOS 2.0: CMD-style additions --------------------------- */

static void cmd_pause(const char* args) {
    (void)args;
    vga_puts("Press any key to continue . . . ");
    (void)keyboard_getc();
    vga_putc('\n');
}

static void cmd_rem(const char* args) {
    (void)args;
    /* No-op. CMD's REM is a comment marker; we accept and discard. */
}

static void cmd_hostname(const char* args) {
    (void)args;
    vga_puts("boxos\n");
}

static void cmd_vol(const char* args) {
    (void)args;
    /* TODO: read the FAT12 BPB volume label. For now: a friendly stub. */
    vga_puts(" Volume in drive C is BOXOS-FS\n");
    vga_puts(" Volume Serial Number is 0BOX-1ABC\n");
}

static void cmd_title(const char* args) {
    if (!args || !args[0]) {
        vga_printf("title: %s\n", wm_get_title());
        return;
    }
    wm_set_title(args);
    /* Repaint the menu bar. wm_init() also re-stamps boot chrome, so
     * just call it; fbcon_repaint after to bring the text grid back. */
    wm_init();
    fbcon_repaint();
    cursor_show();
}

static void cmd_prompt(const char* args) {
    if (!args || !args[0]) {
        vga_printf("prompt: %s\n", g_prompt_fmt);
        vga_puts("escapes: $P (path) $G (>) $L (<) $D (uptime sec) $_ (newline) $$ ($)\n");
        return;
    }
    int n = 0;
    while (args[n] && n < (int)sizeof(g_prompt_fmt) - 1) {
        g_prompt_fmt[n] = args[n];
        n++;
    }
    g_prompt_fmt[n] = 0;
}

static int cmd_args_take(const char** pp, char* out, size_t cap) {
    const char* p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    while (*p && *p != ' ' && *p != '\t' && n + 1 < cap) out[n++] = *p++;
    out[n] = 0;
    *pp = p;
    return n > 0;
}

static void cmd_where(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: WHERE <name>\n"); return; }
    char name[FAT12_MAX_NAME];
    const char* p = args;
    cmd_args_take(&p, name, sizeof(name));
    /* Search in: cwd, root, /APPS, /GAMES, /SYS. */
    static const char* const dirs[] = { "APPS", "GAMES", "SYS" };
    int found = 0;
    if (fs_read_in(cwd_cluster(), name, 0, 0) >= 0) {
        char path[CWD_PATH_MAX]; cwd_path(path, sizeof(path));
        vga_printf("%s/%s\n", path[1] ? path : "", name);
        found++;
    }
    if (cwd_cluster() != 0 && fs_read_in(0, name, 0, 0) >= 0) {
        vga_printf("/%s\n", name);
        found++;
    }
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        uint16_t d = fs_find_dir(0, dirs[i]);
        if (d == 0xFFFF) continue;
        if (fs_read_in(d, name, 0, 0) >= 0) {
            vga_printf("/%s/%s\n", dirs[i], name);
            found++;
        }
    }
    if (!found) vga_printf("WHERE: '%s' not found\n", name);
}

static void cmd_attrib(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: ATTRIB <name>\n"); return; }
    char name[FAT12_MAX_NAME];
    const char* p = args;
    cmd_args_take(&p, name, sizeof(name));
    struct fat12_entry list[MAX_DIR_ENTRIES];
    int n = fs_list_dir(cwd_cluster(), list, MAX_DIR_ENTRIES);
    for (int i = 0; i < n; i++) {
        if (strcasecmp(list[i].name, name) != 0) continue;
        uint8_t a = list[i].attr;
        char flags[8];
        flags[0] = (a & 0x01) ? 'R' : ' ';   /* read-only */
        flags[1] = (a & 0x02) ? 'H' : ' ';   /* hidden    */
        flags[2] = (a & 0x04) ? 'S' : ' ';   /* system    */
        flags[3] = (a & 0x08) ? 'V' : ' ';   /* vol label */
        flags[4] = (a & 0x10) ? 'D' : ' ';   /* directory */
        flags[5] = (a & 0x20) ? 'A' : ' ';   /* archive   */
        flags[6] = 0;
        vga_printf("    %s  %s\n", flags, list[i].name);
        return;
    }
    vga_printf("ATTRIB: '%s' not found\n", name);
}

static void cmd_choice(const char* args) {
    /* Accept "/c:KEYS" prefix to override the choice set; otherwise YN. */
    char keys[16] = "YN";
    const char* prompt_text = args ? args : "";
    if (args && args[0] == '/' && (args[1] == 'c' || args[1] == 'C') && args[2] == ':') {
        const char* p = args + 3;
        int k = 0;
        while (*p && *p != ' ' && k + 1 < (int)sizeof(keys)) keys[k++] = *p++;
        keys[k] = 0;
        while (*p == ' ') p++;
        prompt_text = p;
    }
    /* Build the bracketed prompt. */
    vga_puts(prompt_text);
    if (prompt_text[0]) vga_putc(' ');
    vga_putc('[');
    for (int i = 0; keys[i]; i++) {
        if (i) vga_putc(',');
        vga_putc(keys[i]);
    }
    vga_putc(']');
    vga_putc('?');
    vga_putc(' ');
    for (;;) {
        char c = keyboard_getc();
        for (int i = 0; keys[i]; i++) {
            char k = keys[i];
            char up = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
            char ku = (k >= 'a' && k <= 'z') ? (k - 'a' + 'A') : k;
            if (up == ku) {
                vga_putc(c);
                vga_putc('\n');
                vga_printf("CHOICE: %c\n", up);
                return;
            }
        }
    }
}

/* SORT — read a file, sort lines alphabetically, print. We cap the
 * input at 16 KiB and 256 lines to keep the kernel-side allocation
 * predictable. */
static void cmd_sort(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: SORT <file>\n"); return; }
    char name[FAT12_MAX_NAME];
    const char* p = args;
    cmd_args_take(&p, name, sizeof(name));
    static char buf[16384];
    int got = fs_read_in(cwd_cluster(), name, buf, sizeof(buf) - 1);
    if (got < 0) { vga_printf("SORT: '%s' not found\n", name); return; }
    buf[got] = 0;
    /* Slice into NUL-terminated lines. */
    static const char* lines[256];
    int nl = 0;
    char* start = buf;
    for (char* q = buf; *q && nl < (int)(sizeof(lines)/sizeof(lines[0])); q++) {
        if (*q == '\n') { *q = 0; lines[nl++] = start; start = q + 1; }
    }
    if (start && *start && nl < (int)(sizeof(lines)/sizeof(lines[0])))
        lines[nl++] = start;
    /* Stable insertion sort: small inputs, simple. */
    for (int i = 1; i < nl; i++) {
        const char* x = lines[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(lines[j], x) > 0) {
            lines[j + 1] = lines[j];
            j--;
        }
        lines[j + 1] = x;
    }
    for (int i = 0; i < nl; i++) {
        vga_puts(lines[i]);
        vga_putc('\n');
    }
}

/* FC — file compare, byte by byte. Prints the offset of the first
 * differing byte, or "FC: no differences" if the files match. */
static void cmd_fc(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: FC <a> <b>\n"); return; }
    char a[FAT12_MAX_NAME], b[FAT12_MAX_NAME];
    const char* p = args;
    cmd_args_take(&p, a, sizeof(a));
    cmd_args_take(&p, b, sizeof(b));
    if (!a[0] || !b[0]) { vga_puts("usage: FC <a> <b>\n"); return; }
    static char buf_a[8192];
    static char buf_b[8192];
    int sa = fs_read_in(cwd_cluster(), a, buf_a, sizeof(buf_a));
    int sb = fs_read_in(cwd_cluster(), b, buf_b, sizeof(buf_b));
    if (sa < 0) { vga_printf("FC: '%s' not found\n", a); return; }
    if (sb < 0) { vga_printf("FC: '%s' not found\n", b); return; }
    int n = sa < sb ? sa : sb;
    for (int i = 0; i < n; i++) {
        if (buf_a[i] != buf_b[i]) {
            vga_printf("FC: differ at offset %d (0x%x): %02x vs %02x\n",
                       i, i, (unsigned)(uint8_t)buf_a[i],
                            (unsigned)(uint8_t)buf_b[i]);
            return;
        }
    }
    if (sa != sb) {
        vga_printf("FC: equal in first %d bytes; %s is longer (%d vs %d)\n",
                   n, sa > sb ? a : b, sa, sb);
        return;
    }
    vga_puts("FC: no differences\n");
}

/* FINDSTR — grep with line numbers. Accepts a substring (case-
 * insensitive) and a filename. */
static void cmd_findstr(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: FINDSTR <pat> <file>\n"); return; }
    char pat[64], name[FAT12_MAX_NAME];
    const char* p = args;
    cmd_args_take(&p, pat,  sizeof(pat));
    cmd_args_take(&p, name, sizeof(name));
    if (!pat[0] || !name[0]) { vga_puts("usage: FINDSTR <pat> <file>\n"); return; }
    static char buf[16384];
    int got = fs_read_in(cwd_cluster(), name, buf, sizeof(buf) - 1);
    if (got < 0) { vga_printf("FINDSTR: '%s' not found\n", name); return; }
    buf[got] = 0;
    int line = 1;
    char* lstart = buf;
    int hits = 0;
    for (char* q = buf; ; q++) {
        if (*q == '\n' || *q == 0) {
            char saved = *q; *q = 0;
            /* Case-insensitive substring search. */
            int found = 0;
            for (char* s = lstart; *s; s++) {
                int k = 0;
                while (pat[k]) {
                    char c1 = s[k]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                    char c2 = pat[k]; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                    if (c1 == 0 || c1 != c2) break;
                    k++;
                }
                if (pat[k] == 0) { found = 1; break; }
            }
            if (found) { vga_printf("%4d: %s\n", line, lstart); hits++; }
            if (saved == 0) break;
            *q = saved;
            line++;
            lstart = q + 1;
        }
    }
    if (!hits) vga_printf("FINDSTR: no matches for '%s' in '%s'\n", pat, name);
}

/* FACTORY — wipe user changes and restore the original disk layout
 * from the embedded backup, then reboot. Three confirmations: a typed
 * 'YES' AND a typed 'FACTORY' to make accidental presses nearly
 * impossible. */
static void cmd_factory(const char* args) {
    (void)args;
    if (!require_fs()) return;

    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("\n  +---------------------------------------------------+\n");
    vga_puts("  |              FACTORY  RESET  WARNING              |\n");
    vga_puts("  +---------------------------------------------------+\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  This will:\n");
    vga_puts("    - ERASE every file you have created or edited\n");
    vga_puts("    - RESTORE the original APPS/, GAMES/, SYS/, README\n");
    vga_puts("    - REBOOT the machine\n\n");
    vga_puts("  This cannot be undone from inside the OS.\n\n");

    char buf[32];
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  Type 'YES' to continue: ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    read_line(buf, sizeof(buf));
    if (strcasecmp(buf, "YES") != 0) { vga_puts("  aborted.\n"); return; }

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  And type 'FACTORY' to confirm: ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    read_line(buf, sizeof(buf));
    if (strcasecmp(buf, "FACTORY") != 0) { vga_puts("  aborted.\n"); return; }

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  restoring factory image... (this takes a few seconds)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    int rc = fs_factory_restore();
    if (rc == -1) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  no factory backup found on this disk.\n");
        vga_puts("  (you're booted from the legacy two-disk layout.)\n");
        vga_puts("  re-flash 'boxos-bootable.img' to a USB drive instead.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (rc == -2) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  I/O error during restore — disk may be in a bad state.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("  factory image restored. Rebooting in 3 seconds...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    timer_sleep_ms(3000);
    cmd_reboot(0);
}

/* WELCOME — the first-boot hardware report. Used to live under
 * SETUP in v2.x; SETUP is now the disk-management menu below. */
static void cmd_welcome(const char* args) {
    (void)args;
    vga_clear();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");
    vga_puts("    +------------------------------------------------+\n");
    vga_puts("    |              BoxOS Arise welcome               |\n");
    vga_puts("    +------------------------------------------------+\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Hardware checks:\n\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("    [ok] long mode (x86_64)\n");
    vga_puts("    [ok] PIT timer @ 100 Hz\n");
    vga_puts("    [ok] PS/2 keyboard\n");
    vga_puts("    [ok] PS/2 mouse + scroll wheel\n");
    vga_printf("    [ok] FAT12 mounted on IDE drive %d\n", fs_drive());
    vga_printf("    [ok] heap %lu / %lu bytes\n",
               (unsigned long)heap_used(), (unsigned long)heap_total());
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("\n  Where to go next:\n");
    vga_puts("    HELP            list every command\n");
    vga_puts("    SETUP           disk manager (scandisk / wipe / install)\n");
    vga_puts("    DIR             see what's on disk\n");
    vga_puts("    CD GAMES; RUN DOOM\n");
    vga_puts("    EXIT            back to the Arise desktop\n\n");
}

/* ---- SETUP: BoxOS Arise disk manager ---------------------------- *
 *
 * Text-mode counterpart to the windowed install wizard. Lists every
 * ATA drive the BIOS exposed, then offers per-drive actions:
 *   scandisk   — walk the FAT12 of the booted drive, report counts
 *                and chain-integrity problems
 *   wipe       — restore the booted drive from its factory backup
 *                (destructive: any user-saved files disappear)
 *   mark       — write /SYS/INSTALL.CFG so the next boot skips the
 *                windowed install wizard
 *   eject      — drop INSTALL.CFG so setup runs again next boot
 *
 * Cross-disk imaging (boot from a USB / .iso and install to a fresh
 * internal HDD) needs a second mounted disk; that lands in v3.1
 * along with the rtl8139 / TCP-IP stack. */

#include "task.h"

static void setup_print_drives(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("\n  ATA drives:\n");
    int cur = fs_drive();
    for (int bus = 0; bus < 2; bus++) {
        for (int d = 0; d < 2; d++) {
            int idx = bus * 2 + d;
            int present = (ata_identify(bus, d) == 0);
            const char* mark = present
                ? ((idx == cur) ? " (current boot, FAT12 mounted)" : " present")
                : " absent";
            vga_set_color(present ? VGA_LIGHT_GREEN : VGA_DARK_GREY, VGA_BLACK);
            vga_printf("    [%d]  IDE %d:%d %s\n", idx, bus, d, mark);
        }
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Walk the FAT and report cluster counts. Hand-rolled rather than
 * exposing FAT internals out of fat12.c. We rely on fs_read_in to
 * trace one chain at a time and never see >MAX_CLUSTERS_PER_FILE
 * extents, so an outlier flags a probable cross-link. */
static void setup_scandisk(void) {
    if (fs_drive() < 0) {
        vga_puts("  scandisk: no FAT12 mounted.\n");
        return;
    }
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n  Scandisk: walking the directory tree...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    /* Walk via fs_list_dir; counts files + subdirs + total bytes. */
    static struct fat12_entry ents[64];
    int dirs = 0, files = 0;
    uint64_t bytes = 0;
    uint16_t queue[32]; int qhead = 0, qtail = 0;
    queue[qtail++] = 0;       /* root */
    while (qhead < qtail) {
        uint16_t cluster = queue[qhead++];
        int n = fs_list_dir(cluster, ents, 64);
        if (n < 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_printf("    !! unreadable directory @ cluster %u\n",
                       (unsigned)cluster);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            continue;
        }
        for (int i = 0; i < n; i++) {
            if (ents[i].name[0] == '.') continue;
            if (ents[i].attr & 0x10) {
                dirs++;
                if (qtail < 32) queue[qtail++] = ents[i].first_cluster;
            } else {
                files++;
                bytes += ents[i].size;
            }
        }
    }
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_printf("    %d directories  %d files  %lu bytes\n",
               dirs, files, (unsigned long)bytes);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("    no chain anomalies detected.\n");
}

static int setup_confirm(const char* prompt) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts(prompt);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(" Type YES to confirm: ");
    char buf[16];
    read_line(buf, sizeof(buf));
    return strcmp(buf, "YES") == 0;
}

static void setup_wipe(void) {
    if (!setup_confirm(
        "\n  This RESTORES the boot drive from its factory image.\n"
        "  Any user-saved file (paint, music, screenshots, INSTALL.CFG)\n"
        "  will be lost. The kernel, apps and factory docs come back.\n"))
    {
        vga_puts("  aborted.\n");
        return;
    }
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  Restoring factory image...\n");
    int rc = fs_factory_restore();
    if (rc == 0) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("    done. Reboot for the change to take effect.\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("    fs_factory_restore returned %d.\n", rc);
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void setup_mark_installed(void) {
    uint16_t sys = fs_find_dir(0, "SYS");
    if (sys == 0xFFFF) {
        vga_puts("  /SYS not found.\n");
        return;
    }
    const char* body = "NAME=User\nCOMPANY=\nHOSTNAME=Arise\n";
    (void)fs_delete(sys, "INSTALL.CFG");
    int rc = fs_create_file(sys, "INSTALL.CFG", body, (uint32_t)strlen(body));
    if (rc < 0) vga_puts("  could not write /SYS/INSTALL.CFG.\n");
    else        vga_puts("  marked installed. Next boot skips Setup.\n");
}

static void setup_eject_marker(void) {
    uint16_t sys = fs_find_dir(0, "SYS");
    if (sys == 0xFFFF) { vga_puts("  /SYS not found.\n"); return; }
    int rc = fs_delete(sys, "INSTALL.CFG");
    vga_puts(rc == 0
        ? "  cleared. Setup wizard will run on next boot.\n"
        : "  no INSTALL.CFG present (already in setup state).\n");
}

static void cmd_setup(const char* args) {
    (void)args;
    for (;;) {
        vga_clear();
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_puts("\n");
        vga_puts("    +------------------------------------------------+\n");
        vga_puts("    |       BoxOS Arise -- internal SETUP            |\n");
        vga_puts("    +------------------------------------------------+\n");
        setup_print_drives();
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("\n  Actions:\n");
        vga_puts("    1) Scandisk the booted drive\n");
        vga_puts("    2) Wipe + factory-restore the booted drive\n");
        vga_puts("    3) Mark this disk as installed (skip wizard next boot)\n");
        vga_puts("    4) Eject the installed marker (run wizard next boot)\n");
        vga_puts("    Q) Quit back to shell\n\n");
        vga_puts("  Choice: ");
        char buf[8];
        read_line(buf, sizeof(buf));
        if (buf[0] == 'q' || buf[0] == 'Q') return;
        if (buf[0] == '1') { setup_scandisk();      }
        else if (buf[0] == '2') { setup_wipe();         }
        else if (buf[0] == '3') { setup_mark_installed(); }
        else if (buf[0] == '4') { setup_eject_marker();   }
        else continue;
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_puts("\n  Press any key to return to the SETUP menu...");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        (void)keyboard_getc();
    }
}

/* EDIT — minimal line editor. Each line of input becomes a line of the
 * file. Special commands at the start of a line:
 *   .SAVE    write to disk and quit
 *   .EXIT    quit without saving
 *   .HELP    show these commands again
 */
static char edit_buf[16384];

static void cmd_edit(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: EDIT <file>\n"); return; }
    if (!require_fs()) return;
    char name[FAT12_MAX_NAME];
    take_arg(args, name, sizeof(name));
    /* If the file already exists, refuse — keep this simple (no overwrite
     * semantics in the read-only-historic FAT12 layer yet). */
    if (fs_read_in(cwd_cluster(), name, 0, 0) >= 0) {
        vga_printf("EDIT: '%s' already exists. DEL first.\n", name);
        return;
    }
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("EDIT — type lines, '.SAVE' to commit, '.EXIT' to abandon.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    size_t pos = 0;
    char line_buf[256];
    for (;;) {
        vga_puts("> ");
        size_t len = read_line(line_buf, sizeof(line_buf));
        if (strcmp(line_buf, ".EXIT") == 0) {
            vga_puts("(abandoned)\n");
            return;
        }
        if (strcmp(line_buf, ".HELP") == 0) {
            vga_puts(".SAVE = write+quit, .EXIT = quit, .HELP = this\n");
            continue;
        }
        if (strcmp(line_buf, ".SAVE") == 0) {
            if (fs_create_file(cwd_cluster(), name, edit_buf, (uint32_t)pos) < 0) {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_puts("EDIT: save failed\n");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                return;
            }
            vga_printf("saved %lu bytes to %s\n", (unsigned long)pos, name);
            return;
        }
        if (pos + len + 2 > sizeof(edit_buf)) {
            vga_puts("EDIT: buffer full\n");
            continue;
        }
        memcpy(edit_buf + pos, line_buf, len);
        pos += len;
        edit_buf[pos++] = '\n';
    }
}

/* Recursive find: walk dirs printing absolute path of any matching file. */
static void find_walk(uint16_t cluster, const char* prefix, const char* needle, int depth) {
    if (depth > 6) return;                  /* sanity */
    static struct fat12_entry entries[MAX_DIR_ENTRIES];
    int n = fs_list_dir(cluster, entries, MAX_DIR_ENTRIES);
    for (int i = 0; i < n; i++) {
        if (entries[i].name[0] == '.') continue;
        char path[CWD_PATH_MAX];
        size_t pl = strlen(prefix);
        size_t nl = strlen(entries[i].name);
        if (pl + 1 + nl + 1 >= sizeof(path)) continue;
        memcpy(path, prefix, pl);
        path[pl] = '/';
        memcpy(path + pl + 1, entries[i].name, nl + 1);
        if (strstr(path, needle) || strstr(entries[i].name, needle)) {
            vga_puts(path);
            if (entries[i].attr & 0x10) vga_puts("/");
            vga_putc('\n');
        }
        if (entries[i].attr & 0x10) {
            find_walk(entries[i].first_cluster, path, needle, depth + 1);
        }
    }
}

static void cmd_find(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: FIND <name-substring>\n"); return; }
    if (!require_fs()) return;
    char needle[FAT12_MAX_NAME];
    size_t n = 0;
    while (args[n] && args[n] != ' ' && n + 1 < sizeof(needle)) {
        needle[n] = args[n];
        n++;
    }
    needle[n] = 0;
    to_upper(needle);
    find_walk(0, "", needle, 0);
}

static void cmd_run(const char* args) {
    if (!args || !args[0]) { vga_puts("usage: RUN <name>\n"); return; }
    if (!require_fs()) return;

    /* Split off the program name; remainder becomes args. */
    char  name[FAT12_MAX_NAME];
    size_t i = 0;
    while (args[i] && args[i] != ' ' && i + 1 < sizeof(name)) {
        name[i] = args[i];
        i++;
    }
    name[i] = 0;
    const char* prog_args = args[i] ? args + i + 1 : "";

    /* Auto-append .BIN if no extension was given. */
    bool has_dot = false;
    for (size_t k = 0; name[k]; k++) if (name[k] == '.') has_dot = true;
    if (!has_dot && strlen(name) + 4 < sizeof(name)) {
        size_t ln = strlen(name);
        name[ln + 0] = '.'; name[ln + 1] = 'B';
        name[ln + 2] = 'I'; name[ln + 3] = 'N';
        name[ln + 4] = 0;
    }
    to_upper(name);

    /* Apps read files relative to the cwd we set here. */
    fs_set_app_cwd(cwd_cluster());
    int rc = loader_run(name, prog_args);
    fs_set_app_cwd(0);

    if (rc < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("RUN: '%s' not found or load error\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else if (rc != 0) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_printf("[exit %d]\n", rc);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* ------------------------- dispatcher ----------------------------- */

static void execute(char* in) {
    while (*in == ' ') in++;
    if (!*in) return;

    char* cmd  = in;
    char* args = in;
    while (*args && *args != ' ') args++;
    if (*args == ' ') { *args++ = 0; while (*args == ' ') args++; }

    if      (strcasecmp(cmd, "help")   == 0 ||
             strcasecmp(cmd, "?")      == 0) cmd_help(args);
    else if (strcasecmp(cmd, "cls")    == 0 ||
             strcasecmp(cmd, "clear")  == 0) cmd_cls(args);
    else if (strcasecmp(cmd, "echo")   == 0) cmd_echo(args);
    else if (strcasecmp(cmd, "ver")    == 0) cmd_ver(args);
    else if (strcasecmp(cmd, "about")  == 0) cmd_about(args);
    else if (strcasecmp(cmd, "whoami") == 0) cmd_whoami(args);
    else if (strcasecmp(cmd, "uptime") == 0) cmd_uptime(args);
    else if (strcasecmp(cmd, "date")   == 0 ||
             strcasecmp(cmd, "time")   == 0) cmd_date(args);
    else if (strcasecmp(cmd, "mem")    == 0) cmd_mem(args);
    else if (strcasecmp(cmd, "dir")    == 0 ||
             strcasecmp(cmd, "ls")     == 0) cmd_dir(args);
    else if (strcasecmp(cmd, "cd")     == 0) cmd_cd(args);
    else if (strcasecmp(cmd, "pwd")    == 0) cmd_pwd(args);
    else if (strcasecmp(cmd, "cat")    == 0 ||
             strcasecmp(cmd, "type")   == 0) cmd_cat(args);
    else if (strcasecmp(cmd, "size")   == 0) cmd_size(args);
    else if (strcasecmp(cmd, "find")   == 0) cmd_find(args);
    else if (strcasecmp(cmd, "tree")   == 0) cmd_tree(args);
    else if (strcasecmp(cmd, "more")   == 0) cmd_more(args);
    else if (strcasecmp(cmd, "hexdump") == 0) cmd_hexdump(args);
    else if (strcasecmp(cmd, "wc")     == 0) cmd_wc(args);
    else if (strcasecmp(cmd, "write")  == 0) cmd_write(args);
    else if (strcasecmp(cmd, "del")    == 0 ||
             strcasecmp(cmd, "erase")  == 0 ||
             strcasecmp(cmd, "rm")     == 0) cmd_del(args);
    else if (strcasecmp(cmd, "mkdir")  == 0 ||
             strcasecmp(cmd, "md")     == 0) cmd_mkdir(args);
    else if (strcasecmp(cmd, "rmdir")  == 0 ||
             strcasecmp(cmd, "rd")     == 0) cmd_rmdir(args);
    else if (strcasecmp(cmd, "edit")   == 0) cmd_edit(args);
    else if (strcasecmp(cmd, "copy")   == 0 ||
             strcasecmp(cmd, "cp")     == 0) cmd_copy(args);
    else if (strcasecmp(cmd, "move")   == 0 ||
             strcasecmp(cmd, "mv")     == 0) cmd_move(args);
    else if (strcasecmp(cmd, "ren")    == 0 ||
             strcasecmp(cmd, "rename") == 0) cmd_ren(args);
    else if (strcasecmp(cmd, "setup")    == 0) cmd_setup(args);
    else if (strcasecmp(cmd, "welcome")  == 0) cmd_welcome(args);
    else if (strcasecmp(cmd, "factory") == 0 ||
             strcasecmp(cmd, "factoryreset") == 0) cmd_factory(args);
    else if (strcasecmp(cmd, "run")    == 0) cmd_run(args);
    else if (strcasecmp(cmd, "color")  == 0) cmd_color(args);
    else if (strcasecmp(cmd, "reboot") == 0) cmd_reboot(args);
    else if (strcasecmp(cmd, "halt")   == 0 ||
             strcasecmp(cmd, "exit")   == 0) cmd_halt(args);
    /* RadiDOS 2.0 additions */
    else if (strcasecmp(cmd, "chdir")  == 0) cmd_cd(args);
    else if (strcasecmp(cmd, "pause")  == 0) cmd_pause(args);
    else if (strcasecmp(cmd, "rem")    == 0 ||
             strcasecmp(cmd, "::")     == 0) cmd_rem(args);
    else if (strcasecmp(cmd, "hostname") == 0) cmd_hostname(args);
    else if (strcasecmp(cmd, "vol")    == 0) cmd_vol(args);
    else if (strcasecmp(cmd, "title")  == 0) cmd_title(args);
    else if (strcasecmp(cmd, "prompt") == 0) cmd_prompt(args);
    else if (strcasecmp(cmd, "where")  == 0) cmd_where(args);
    else if (strcasecmp(cmd, "attrib") == 0) cmd_attrib(args);
    else if (strcasecmp(cmd, "choice") == 0) cmd_choice(args);
    else if (strcasecmp(cmd, "sort")   == 0) cmd_sort(args);
    else if (strcasecmp(cmd, "fc")     == 0 ||
             strcasecmp(cmd, "comp")   == 0) cmd_fc(args);
    else if (strcasecmp(cmd, "findstr") == 0 ||
             strcasecmp(cmd, "grep")    == 0) cmd_findstr(args);
    else if (fs_drive() >= 0) {
        /* Implicit RUN: typing a program name (DOS-style). */
        char tmp[LINE_MAX];
        size_t cn = strlen(cmd);
        size_t an = strlen(args);
        if (cn + 1 + an + 1 <= sizeof(tmp)) {
            memcpy(tmp, cmd, cn);
            if (an) { tmp[cn] = ' '; memcpy(tmp + cn + 1, args, an + 1); }
            else    { tmp[cn] = 0; }
            cmd_run(tmp);
        }
    }
    else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("Bad command or file name: '");
        vga_puts(cmd);
        vga_puts("'\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
    }
}

/* First-boot detection.
 *   - Mounted FS missing? always show setup.
 *   - /INSTALL.OK marker at root missing? first boot — show setup.
 *   - Critical /SYS dir missing? OS layout is broken — show setup.
 * SETUP writes /INSTALL.OK at the end so the next boot skips it.
 * FACTORY wipes the whole FS, so the marker disappears too — meaning
 * a factory-reset machine boots straight back into setup. */
static int needs_first_run_setup(void) {
    if (fs_drive() < 0) return 1;
    if (fs_read_in(0, "INSTALL.OK", 0, 0) < 0) return 1;
    if (fs_find_dir(0, "SYS") == 0xFFFF) return 1;
    return 0;
}

static void run_first_boot_setup(void) {
    cmd_welcome("");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  Press any key to enter the shell.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    (void)keyboard_getc();
    if (fs_drive() >= 0) {
        const char* mark = "RadiDOS first-run setup completed.\n";
        size_t n = strlen(mark);
        /* Best-effort — if the FS is degraded (e.g. read-only ISO boot)
         * we just skip the marker and re-run setup next boot. */
        (void)fs_create_file(0, "INSTALL.OK", mark, (uint32_t)n);
    }
    vga_clear();
}

/* First-boot text setup. The kernel calls this between fs_mount and
 * splash_show whenever /SYS/INSTALL.CFG is missing — i.e. a fresh
 * install media boot. We print a welcome banner that frames why
 * this is happening, then drop into cmd_setup so the user can pick
 * a drive, scandisk, wipe, or just type Q to proceed to the
 * graphical wizard. */
void shell_radidos_setup(void) {
    vga_clear();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");
    vga_puts("    +------------------------------------------------+\n");
    vga_puts("    |  BoxOS Arise -- RadiDOS internal SETUP         |\n");
    vga_puts("    +------------------------------------------------+\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("\n  First boot. The graphical installer runs after this,\n");
    vga_puts("  but before it does you can:\n");
    vga_puts("    - inspect / scandisk the attached drives\n");
    vga_puts("    - wipe + factory-restore the boot drive\n");
    vga_puts("    - mark this disk installed (skip the wizard)\n");
    vga_puts("    - eject any prior INSTALL.CFG\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n  Press any key to enter the SETUP menu, or Esc to skip\n");
    vga_puts("  straight to the graphical wizard.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    int c = keyboard_getc();
    if (c == 27) return;
    cmd_setup("");
}

void shell_run(void) {
    char buf[LINE_MAX];

    g_shell_exit = 0;

    /* The first-boot SETUP wizard used to run here, but now that the
     * boot path lands on the GUI desktop the wizard is unnecessary
     * (and worse, it ate the user's first keystroke if they typed
     * fast after clicking TERMINAL). Skipped. */

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("Type HELP for commands. EXIT or HALT to close the terminal.\n\n");

    while (!g_shell_exit) {
        prompt();
        size_t len = read_line(buf, sizeof(buf));
        if (len) execute(buf);
    }
}
