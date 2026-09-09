/* Read-only FAT12 driver. Single-disk, no caching, big enough to
 * find files in the root directory and stream their cluster chain
 * into a flat buffer. */

#include "boxos.h"

struct __attribute__((packed)) bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     label[11];
    char     fs_type[8];
};

static struct {
    bool      mounted;
    int       bus;
    int       drive;
    uint16_t  reserved;
    uint8_t   fat_count;
    uint16_t  spf;            /* sectors per FAT */
    uint16_t  root_entries;
    uint8_t   spc;            /* sectors per cluster */
    uint32_t  partition_lba;  /* LBA where the FAT12 image starts */
    uint32_t  root_lba;
    uint32_t  root_sectors;
    uint32_t  data_lba;
} fs;

/* Convenience wrappers — all FAT12 LBAs are added to partition_lba so a
 * single ATA disk can hold the bootloader/kernel at the front and the
 * filesystem starting at some fixed offset further in. */
static int fread_lba(uint32_t lba, uint8_t count, void* buf) {
    return ata_read(fs.bus, fs.drive, fs.partition_lba + lba, count, buf);
}
static int fwrite_lba(uint32_t lba, uint8_t count, const void* buf) {
    return ata_write(fs.bus, fs.drive, fs.partition_lba + lba, count, buf);
}

/* Sized for the 32 MiB FAT12 image (FAT ~= 6 sectors), with headroom up
 * to FAT12's hard limit (~12 sectors at 4084 clusters). 16 covers both. */
#define FAT_MAX_SECTORS 16
static uint8_t fat_buf[FAT_MAX_SECTORS * 512];
static uint8_t sec_buf[512];

static int try_mount(int bus, int drive, uint32_t base) {
    if (ata_read(bus, drive, base, 1, sec_buf) < 0) return -1;
    struct bpb* b = (struct bpb*)sec_buf;

    if (b->bytes_per_sector != 512) return -1;
    if (b->fat_count == 0 || b->fat_count > 2) return -1;
    if (b->sectors_per_cluster == 0 || b->reserved_sectors == 0) return -1;
    if (b->root_entries == 0) return -1;
    if (b->sectors_per_fat == 0 || b->sectors_per_fat > FAT_MAX_SECTORS) return -1;

    if (ata_read(bus, drive, base + b->reserved_sectors,
                 (uint8_t)b->sectors_per_fat, fat_buf) < 0) return -1;

    fs.mounted       = true;
    fs.bus           = bus;
    fs.drive         = drive;
    fs.partition_lba = base;
    fs.reserved      = b->reserved_sectors;
    fs.fat_count     = b->fat_count;
    fs.spf           = b->sectors_per_fat;
    fs.root_entries  = b->root_entries;
    fs.spc           = b->sectors_per_cluster;
    fs.root_lba      = b->reserved_sectors + (uint32_t)b->fat_count * b->sectors_per_fat;
    fs.root_sectors  = ((uint32_t)b->root_entries * 32 + 511) / 512;
    fs.data_lba      = fs.root_lba + fs.root_sectors;
    return 0;
}

/* Probe LBAs we know we plant filesystems at:
 *   0     — legacy "FS as a separate disk" layout
 *   2048  — combined boxos-bootable.img layout (boot+kernel @ 0..2047,
 *           FAT12 @ 2048+) */
#define FS_OFFSET_COMBINED 2048

/* When we boot from an ATAPI CD-ROM, the raw boxos-bootable.img is
 * embedded as a file named BOXOS.IMG inside an ISO 9660 wrapper. We
 * parse the ISO root directory to find its starting LBA so the
 * usual FAT12 probe can run against the embedded image. The offset
 * (in 512-byte units) is captured here so cross-disk install reads
 * the right byte range too. */
static uint32_t g_iso_image_offset_512 = 0;

uint32_t fs_iso_image_offset_512(void) { return g_iso_image_offset_512; }

/* Read the ISO 9660 PVD + root directory to find BOXOS.IMG, then
 * return its starting LBA in 2048-byte sectors. -1 if not found or
 * if the disc isn't ISO 9660. */
static int iso9660_find_boxos_img(int bus, int drive, uint32_t* out_lba_2k) {
    static uint8_t sec2k[2048];
    /* PVD lives at 2 KiB sector 16, i.e. 512-byte LBA 64. */
    if (ata_read(bus, drive, 64, 4, sec2k) < 0) return -1;
    if (sec2k[0] != 1) return -1;
    if (sec2k[1] != 'C' || sec2k[2] != 'D' || sec2k[3] != '0' ||
        sec2k[4] != '0' || sec2k[5] != '1') return -1;
    /* Root-directory record at byte 156 of the PVD. */
    const uint8_t* rec = sec2k + 156;
    uint32_t root_lba_2k = (uint32_t)rec[2]       |
                           ((uint32_t)rec[3] << 8) |
                           ((uint32_t)rec[4] << 16)|
                           ((uint32_t)rec[5] << 24);
    uint32_t root_size   = (uint32_t)rec[10]      |
                           ((uint32_t)rec[11] << 8)|
                           ((uint32_t)rec[12] << 16)|
                           ((uint32_t)rec[13] << 24);
    /* Read the root directory. Tiny ISO; one 2 KiB sector is enough
     * (BoxOS ships exactly one file at the root). */
    if (root_size == 0 || root_size > 2048) return -1;
    if (ata_read(bus, drive, root_lba_2k * 4, 4, sec2k) < 0) return -1;
    uint32_t off = 0;
    while (off < root_size) {
        uint8_t reclen  = sec2k[off];
        if (reclen == 0) break;
        uint8_t namelen = sec2k[off + 32];
        const uint8_t* name = sec2k + off + 33;
        /* Match BOXOS.IMG (with the ISO 9660 ";1" version suffix
         * appended by xorriso). */
        if (namelen >= 9 &&
            name[0] == 'B' && name[1] == 'O' && name[2] == 'X' &&
            name[3] == 'O' && name[4] == 'S' && name[5] == '.' &&
            name[6] == 'I' && name[7] == 'M' && name[8] == 'G') {
            const uint8_t* r = sec2k + off;
            *out_lba_2k = (uint32_t)r[2]       |
                          ((uint32_t)r[3] << 8) |
                          ((uint32_t)r[4] << 16)|
                          ((uint32_t)r[5] << 24);
            return 0;
        }
        off += reclen;
    }
    return -1;
}

int fs_mount(void) {
    static const uint32_t offsets[] = { 0, FS_OFFSET_COMBINED };

    /* Pass 1 — real hard disks. An *installed* HDD must always win
     * over the bootstrap CD: if you install, leave the .iso in the
     * tray, and reboot, the kernel must mount the HDD's filesystem
     * (which carries /SYS/SETUP.DONE) and not the CD's (which never
     * does — that's why Setup used to re-appear forever). */
    for (int bus = 0; bus < 2; bus++) {
        for (int drv = 0; drv < 2; drv++) {
            const char* bn = bus == 0 ? "primary  " : "secondary";
            const char* dn = drv == 0 ? "master" : "slave ";
            if (ata_identify(bus, drv) < 0)        continue;
            if (ata_drive_type(bus, drv) == 2)     continue;   /* CDs later */
            vga_printf("[fs] IDE %s %s: ", bn, dn);
            for (size_t i = 0; i < sizeof(offsets)/sizeof(*offsets); i++) {
                if (try_mount(bus, drv, offsets[i]) == 0) {
                    if (offsets[i] == 0) vga_puts("FAT12 mounted\n");
                    else                 vga_printf("FAT12 mounted (offset %u)\n",
                                                    (unsigned)offsets[i]);
                    return 0;
                }
            }
            vga_puts("present, but not FAT12\n");
        }
    }

    /* Pass 2 — ATAPI CD-ROMs. boxos.iso embeds the raw
     * boxos-bootable.img as BOXOS.IMG inside an ISO 9660 wrapper. */
    for (int bus = 0; bus < 2; bus++) {
        for (int drv = 0; drv < 2; drv++) {
            if (ata_identify(bus, drv) < 0)        continue;
            if (ata_drive_type(bus, drv) != 2)     continue;
            const char* bn = bus == 0 ? "primary  " : "secondary";
            const char* dn = drv == 0 ? "master" : "slave ";
            vga_printf("[fs] IDE %s %s: ", bn, dn);
            uint32_t img_lba_2k = 0;
            if (iso9660_find_boxos_img(bus, drv, &img_lba_2k) == 0) {
                uint32_t base = img_lba_2k * 4 + FS_OFFSET_COMBINED;
                if (try_mount(bus, drv, base) == 0) {
                    g_iso_image_offset_512 = img_lba_2k * 4;
                    vga_printf("FAT12 mounted (CD, ISO offset %u sectors)\n",
                               (unsigned)(img_lba_2k * 4));
                    return 0;
                }
            }
            vga_puts("ATAPI drive, no BOXOS.IMG\n");
        }
    }
    return -1;
}

int fs_drive(void) { return fs.mounted ? (fs.bus * 2 + fs.drive) : -1; }

static uint16_t fat12_next(uint16_t cluster);

/* Convert "HELLO   BIN" (raw 11-byte directory name) -> "HELLO.BIN" */
static void format_name(const uint8_t* raw, char* out) {
    int o = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[o++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[o++] = (char)raw[i];
    }
    out[o] = 0;
}

/* Convert "HELLO.BIN" -> "HELLO   BIN" (11-byte raw form). Special-cases
 * "." and ".." which FAT stores as bare dots padded with spaces (not as
 * a name + extension split). */
static void encode_name(const char* in, uint8_t* out) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    if (in[0] == '.' && in[1] == 0) { out[0] = '.'; return; }
    if (in[0] == '.' && in[1] == '.' && in[2] == 0) { out[0] = '.'; out[1] = '.'; return; }
    int i = 0, o = 0;
    while (in[i] && in[i] != '.' && o < 8) {
        char c = in[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[o++] = (uint8_t)c;
    }
    while (in[i] && in[i] != '.') i++;
    if (in[i] == '.') i++;
    o = 8;
    while (in[i] && o < 11) {
        char c = in[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[o++] = (uint8_t)c;
    }
}

/* Per-entry visibility:
 *   1  = normal entry, fill it in
 *   0  = skip (deleted / volume label / LFN)
 *  -1  = end-of-dir marker
 */
static int dir_entry_visible(const uint8_t* e) {
    if (e[0] == 0x00) return -1;
    if (e[0] == 0xE5) return 0;
    if ((e[11] & 0x0F) == 0x0F) return 0;       /* LFN slot       */
    if (e[11] & 0x08) return 0;                 /* volume label   */
    return 1;
}

static void fill_entry(const uint8_t* e, struct fat12_entry* o) {
    format_name(e, o->name);
    o->attr          = e[11];
    o->first_cluster = (uint16_t)(e[26] | (e[27] << 8));
    o->size          = (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                     | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
}

/* Iterate every visible entry in `dir_cluster`. cluster == 0 means the
 * fixed-position root directory. Returns count, or -1 on read error. */
static int scan_dir(uint16_t dir_cluster, struct fat12_entry* out, int max) {
    if (!fs.mounted) return -1;
    int count = 0;

    if (dir_cluster == 0) {
        for (uint32_t s = 0; s < fs.root_sectors && count < max; s++) {
            if (fread_lba(fs.root_lba + s, 1, sec_buf) < 0) return -1;
            for (int i = 0; i < 16 && count < max; i++) {
                uint8_t* e = sec_buf + i * 32;
                int v = dir_entry_visible(e);
                if (v < 0) return count;
                if (!v) continue;
                fill_entry(e, &out[count]);
                count++;
            }
        }
    } else {
        uint16_t c = dir_cluster;
        while (c >= 2 && c < 0xFF8 && count < max) {
            uint32_t lba = fs.data_lba + (uint32_t)(c - 2) * fs.spc;
            for (uint32_t s = 0; s < fs.spc && count < max; s++) {
                if (fread_lba(lba + s, 1, sec_buf) < 0) return -1;
                for (int i = 0; i < 16 && count < max; i++) {
                    uint8_t* e = sec_buf + i * 32;
                    int v = dir_entry_visible(e);
                    if (v < 0) return count;
                    if (!v) continue;
                    fill_entry(e, &out[count]);
                    count++;
                }
            }
            c = fat12_next(c);
        }
    }
    return count;
}

int fs_list_dir(uint16_t dir_cluster, struct fat12_entry* out, int max) {
    return scan_dir(dir_cluster, out, max);
}

/* Compatibility wrapper — root only, files only (matches previous behavior). */
int fs_list(struct fat12_entry* out, int max) {
    int n = scan_dir(0, out, max);
    if (n < 0) return n;
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (out[i].attr & 0x10) continue;
        if (i != w) out[w] = out[i];
        w++;
    }
    return w;
}

/* Lookup `name` in `dir_cluster`. Returns first_cluster on hit, 0xFFFF on
 * miss. `want_dir`: 1 = only return directories, 0 = only files. */
static uint16_t lookup(uint16_t dir_cluster, const char* name, int want_dir, uint32_t* out_size) {
    if (!fs.mounted) return 0xFFFF;
    uint8_t want[11];
    encode_name(name, want);

    /* Inline directory scan — same shape as scan_dir but with an early
     * return on match so we don't have to materialise the whole listing. */
    if (dir_cluster == 0) {
        for (uint32_t s = 0; s < fs.root_sectors; s++) {
            if (fread_lba(fs.root_lba + s, 1, sec_buf) < 0) return 0xFFFF;
            for (int i = 0; i < 16; i++) {
                uint8_t* e = sec_buf + i * 32;
                int v = dir_entry_visible(e);
                if (v < 0) return 0xFFFF;
                if (!v) continue;
                if (memcmp(e, want, 11) != 0) continue;
                int is_dir = (e[11] & 0x10) ? 1 : 0;
                if (want_dir != is_dir) continue;
                if (out_size) {
                    *out_size = (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                              | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
                }
                return (uint16_t)(e[26] | (e[27] << 8));
            }
        }
    } else {
        uint16_t c = dir_cluster;
        while (c >= 2 && c < 0xFF8) {
            uint32_t lba = fs.data_lba + (uint32_t)(c - 2) * fs.spc;
            for (uint32_t s = 0; s < fs.spc; s++) {
                if (fread_lba(lba + s, 1, sec_buf) < 0) return 0xFFFF;
                for (int i = 0; i < 16; i++) {
                    uint8_t* e = sec_buf + i * 32;
                    int v = dir_entry_visible(e);
                    if (v < 0) return 0xFFFF;
                    if (!v) continue;
                    if (memcmp(e, want, 11) != 0) continue;
                    int is_dir = (e[11] & 0x10) ? 1 : 0;
                    if (want_dir != is_dir) continue;
                    if (out_size) {
                        *out_size = (uint32_t)e[28] | ((uint32_t)e[29] << 8)
                                  | ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
                    }
                    return (uint16_t)(e[26] | (e[27] << 8));
                }
            }
            c = fat12_next(c);
        }
    }
    return 0xFFFF;
}

uint16_t fs_find_dir(uint16_t parent, const char* name) {
    return lookup(parent, name, 1, 0);
}

/* Per-task cwd for fs_read_file (apps don't pass a path). Stored on
 * the task struct (task.c) so two concurrent tasks don't clobber
 * each other. task_set/get_cwd are no-ops before task_init() runs;
 * everything that calls these in BoxOS sits comfortably after that. */
#include "task.h"
void     fs_set_app_cwd(uint16_t cluster) { task_set_cwd(cluster); }
uint16_t fs_get_app_cwd(void)              { return task_get_cwd(); }

static uint16_t fat12_next(uint16_t cluster) {
    uint32_t off = (uint32_t)cluster + (uint32_t)cluster / 2;
    uint16_t v = (uint16_t)fat_buf[off] | ((uint16_t)fat_buf[off + 1] << 8);
    return (cluster & 1) ? (uint16_t)(v >> 4) : (uint16_t)(v & 0x0FFF);
}

/* ---- write support ----------------------------------------------- */

static void fat12_set(uint16_t cluster, uint16_t value) {
    uint32_t off = (uint32_t)cluster + (uint32_t)cluster / 2;
    uint16_t v = (uint16_t)fat_buf[off] | ((uint16_t)fat_buf[off + 1] << 8);
    if (cluster & 1) v = (uint16_t)((v & 0x000F) | ((value & 0x0FFF) << 4));
    else             v = (uint16_t)((v & 0xF000) |  (value & 0x0FFF));
    fat_buf[off]     = (uint8_t)(v & 0xFF);
    fat_buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

/* Persist the in-memory FAT cache to disk (both copies if there are 2). */
static int flush_fat(void) {
    if (!fs.mounted) return -1;
    /* FAT 1 starts at fs.reserved sectors. FAT 2 (if present) right after. */
    for (int copy = 0; copy < fs.fat_count; copy++) {
        uint32_t fat_lba = fs.reserved + (uint32_t)copy * fs.spf;
        for (uint16_t s = 0; s < fs.spf; s++) {
            if (fwrite_lba(fat_lba + s, 1, &fat_buf[s * 512]) < 0) return -1;
        }
    }
    return 0;
}

static uint32_t total_clusters(void) {
    /* FAT12 has spf*512*8/12 entries; cluster numbers up to that. */
    return ((uint32_t)fs.spf * 512 * 8) / 12;
}

/* Find a free cluster (FAT entry == 0) and reserve it (mark as EOC). */
static uint16_t alloc_cluster(void) {
    uint32_t n = total_clusters();
    for (uint16_t c = 2; c < n && c < 0xFF8; c++) {
        if (fat12_next(c) == 0) {
            fat12_set(c, 0xFFF);
            return c;
        }
    }
    return 0;
}

static void free_chain(uint16_t start) {
    uint16_t c = start;
    while (c >= 2 && c < 0xFF8) {
        uint16_t nx = fat12_next(c);
        fat12_set(c, 0);
        c = nx;
    }
}

/* Zero the data clusters of `chain` so newly-allocated dirs don't carry
 * leftover content. */
static int zero_cluster(uint16_t cluster) {
    static uint8_t zeros[512] = {0};
    uint32_t lba = fs.data_lba + (uint32_t)(cluster - 2) * fs.spc;
    for (uint32_t s = 0; s < fs.spc; s++) {
        if (fwrite_lba(lba + s, 1, zeros) < 0) return -1;
    }
    return 0;
}

/* Walk a directory and call `cb(entry32, sector_lba, in_sector_index, ctx)`
 * for each slot. Returning non-zero from the callback stops iteration and
 * is the value returned from this function. Skips no entries — caller
 * decides what to do with deleted/free/lfn slots. The buffer in sec_buf
 * holds the current sector when the callback runs; the callback may
 * mutate sec_buf and we'll write it back if `dirty_out` is set. */
static int walk_dir_slots(uint16_t dir_cluster,
                          int (*cb)(uint8_t* e, uint32_t lba, int slot, void* ctx, int* dirty),
                          void* ctx) {
    if (dir_cluster == 0) {
        for (uint32_t s = 0; s < fs.root_sectors; s++) {
            uint32_t lba = fs.root_lba + s;
            if (fread_lba(lba, 1, sec_buf) < 0) return -1;
            int dirty = 0;
            for (int i = 0; i < 16; i++) {
                int rc = cb(sec_buf + i * 32, lba, i, ctx, &dirty);
                if (rc) {
                    if (dirty) fwrite_lba(lba, 1, sec_buf);
                    return rc;
                }
            }
            if (dirty) fwrite_lba(lba, 1, sec_buf);
        }
    } else {
        uint16_t c = dir_cluster;
        while (c >= 2 && c < 0xFF8) {
            uint32_t base = fs.data_lba + (uint32_t)(c - 2) * fs.spc;
            for (uint32_t s = 0; s < fs.spc; s++) {
                uint32_t lba = base + s;
                if (fread_lba(lba, 1, sec_buf) < 0) return -1;
                int dirty = 0;
                for (int i = 0; i < 16; i++) {
                    int rc = cb(sec_buf + i * 32, lba, i, ctx, &dirty);
                    if (rc) {
                        if (dirty) fwrite_lba(lba, 1, sec_buf);
                        return rc;
                    }
                }
                if (dirty) fwrite_lba(lba, 1, sec_buf);
            }
            c = fat12_next(c);
        }
    }
    return 0;
}

/* Find the first free directory slot and stamp `entry32` into it. */
struct put_ctx { const uint8_t* entry; int placed; };
static int put_cb(uint8_t* e, uint32_t lba, int slot, void* ctxp, int* dirty) {
    (void)lba; (void)slot;
    struct put_ctx* ctx = ctxp;
    if (e[0] == 0x00 || e[0] == 0xE5) {
        for (int i = 0; i < 32; i++) e[i] = ctx->entry[i];
        *dirty = 1;
        ctx->placed = 1;
        return 1;
    }
    return 0;
}

static int put_dir_entry(uint16_t dir_cluster, const uint8_t entry[32]) {
    struct put_ctx ctx = { entry, 0 };
    walk_dir_slots(dir_cluster, put_cb, &ctx);
    return ctx.placed ? 0 : -1;
}

/* Mark the named entry deleted (0xE5). */
struct del_ctx { const uint8_t* want; int found; uint16_t first_cluster; uint8_t attr; };
static int del_cb(uint8_t* e, uint32_t lba, int slot, void* ctxp, int* dirty) {
    (void)lba; (void)slot;
    struct del_ctx* ctx = ctxp;
    if (e[0] == 0x00) return -2;        /* end of dir */
    if (e[0] == 0xE5) return 0;
    if ((e[11] & 0x0F) == 0x0F) return 0;
    if (memcmp(e, ctx->want, 11) != 0) return 0;
    ctx->first_cluster = (uint16_t)(e[26] | (e[27] << 8));
    ctx->attr          = e[11];
    e[0] = 0xE5;
    *dirty = 1;
    ctx->found = 1;
    return 1;
}

int fs_delete(uint16_t dir_cluster, const char* name) {
    if (!fs.mounted) return -1;
    uint8_t want[11];
    encode_name(name, want);
    struct del_ctx ctx = { want, 0, 0, 0 };
    walk_dir_slots(dir_cluster, del_cb, &ctx);
    if (!ctx.found) return -1;
    if (ctx.first_cluster >= 2) free_chain(ctx.first_cluster);
    if (flush_fat() < 0) return -1;
    return 0;
}

static void fill_filename(uint8_t* out, const char* name) {
    encode_name(name, out);
}

/* Create a regular file. data may be NULL for a zero-byte file. */
int fs_create_file(uint16_t dir_cluster, const char* name, const void* data, uint32_t size) {
    if (!fs.mounted) return -1;
    /* Reject if a file with that name already exists. */
    if (lookup(dir_cluster, name, 0, 0) != 0xFFFF) return -1;
    if (lookup(dir_cluster, name, 1, 0) != 0xFFFF) return -1;

    uint16_t first = 0;
    uint32_t cluster_bytes = (uint32_t)fs.spc * 512;
    if (size > 0) {
        first = alloc_cluster();
        if (!first) return -1;
        uint16_t prev = first, cur = first;
        uint32_t remaining = size;
        const uint8_t* src = (const uint8_t*)data;
        while (remaining > 0) {
            uint32_t lba = fs.data_lba + (uint32_t)(cur - 2) * fs.spc;
            for (uint32_t s = 0; s < fs.spc && remaining > 0; s++) {
                uint8_t buf[512] = {0};
                uint32_t take = remaining < 512 ? remaining : 512;
                memcpy(buf, src, take);
                if (fwrite_lba(lba + s, 1, buf) < 0) return -1;
                src += take;
                remaining -= take;
            }
            if (remaining > 0) {
                uint16_t nx = alloc_cluster();
                if (!nx) { free_chain(first); flush_fat(); return -1; }
                fat12_set(prev, nx);
                prev = nx;
                cur  = nx;
            }
            (void)cluster_bytes;
        }
    }

    uint8_t entry[32] = {0};
    fill_filename(entry, name);
    entry[11] = 0x20;                                   /* archive */
    entry[26] = (uint8_t)(first & 0xFF);
    entry[27] = (uint8_t)((first >> 8) & 0xFF);
    entry[28] = (uint8_t)(size & 0xFF);
    entry[29] = (uint8_t)((size >> 8) & 0xFF);
    entry[30] = (uint8_t)((size >> 16) & 0xFF);
    entry[31] = (uint8_t)((size >> 24) & 0xFF);

    if (put_dir_entry(dir_cluster, entry) < 0) {
        if (first) { free_chain(first); flush_fat(); }
        return -1;
    }
    if (flush_fat() < 0) return -1;
    return 0;
}

/* Create an empty subdirectory under `parent`. */
int fs_mkdir_at(uint16_t parent_cluster, const char* name) {
    if (!fs.mounted) return -1;
    if (lookup(parent_cluster, name, 0, 0) != 0xFFFF) return -1;
    if (lookup(parent_cluster, name, 1, 0) != 0xFFFF) return -1;

    uint16_t cl = alloc_cluster();
    if (!cl) return -1;
    if (zero_cluster(cl) < 0) { free_chain(cl); flush_fat(); return -1; }

    /* Write . and .. entries into the new cluster. */
    uint8_t sec[512] = {0};
    /* "." entry — points at this cluster */
    memset(sec, ' ', 11);
    sec[0] = '.';
    sec[11] = 0x10;
    sec[26] = (uint8_t)(cl & 0xFF);
    sec[27] = (uint8_t)((cl >> 8) & 0xFF);
    /* ".." entry — points at the parent (0 means root) */
    memset(sec + 32, ' ', 11);
    sec[32] = '.';
    sec[33] = '.';
    sec[32 + 11] = 0x10;
    sec[32 + 26] = (uint8_t)(parent_cluster & 0xFF);
    sec[32 + 27] = (uint8_t)((parent_cluster >> 8) & 0xFF);
    uint32_t lba = fs.data_lba + (uint32_t)(cl - 2) * fs.spc;
    if (fwrite_lba(lba, 1, sec) < 0) {
        free_chain(cl); flush_fat(); return -1;
    }

    /* Add the directory entry into the parent. */
    uint8_t entry[32] = {0};
    fill_filename(entry, name);
    entry[11] = 0x10;                                   /* directory */
    entry[26] = (uint8_t)(cl & 0xFF);
    entry[27] = (uint8_t)((cl >> 8) & 0xFF);
    if (put_dir_entry(parent_cluster, entry) < 0) {
        free_chain(cl); flush_fat(); return -1;
    }
    if (flush_fat() < 0) return -1;
    return 0;
}

/* ---- cross-disk install ---------------------------------------- *
 *
 * fs_install_start picks up a target drive (any non-boot ATA drive
 * that responds to IDENTIFY). fs_install_chunk copies one 16 KiB
 * slab at a time so the wizard can repaint between calls.
 *
 * How much we copy depends on the target. The boot media is laid out
 * as boot+kernel (0..2047), the live FAT12 (2048..67583), and a
 * read-only factory copy of that same filesystem (67584..133119).
 *
 * Copying only the first two — which is all this used to do — leaves
 * the installed disk with no factory backup, so FACTORY and Setup's
 * "format first" both fail on it forever after. So: if the target is
 * big enough to hold the factory copy too, take the whole 65 MiB and
 * the installed system keeps a working factory reset. If it isn't,
 * fall back to the short 33 MiB copy and record that this install has
 * no backup, so the shell can say so plainly instead of guessing.
 *
 * Two-step API instead of one big sync call because the chunked
 * version lets the wizard update its progress bar smoothly. */

static int      g_inst_bus = -1, g_inst_drive = -1;
static uint32_t g_inst_total = 0;
static uint32_t g_inst_done  = 0;
/* 0 = ok, 1 = read error from install media, 2 = write error to target. */
static int      g_inst_err_phase = 0;
/* 1 if the copy in progress includes the factory backup region. */
static int      g_inst_with_factory = 0;

#define FS_FACTORY_LBA      67584u   /* where the factory copy starts     */
#define INST_SHORT_SECTORS  67584u   /* boot + kernel + live FAT12 (33MiB)*/
#define INST_FULL_SECTORS  133120u   /* ...plus the factory copy   (65MiB)*/
#define INST_CHUNK_SECTORS     32u

int fs_install_err_phase(void) { return g_inst_err_phase; }

/* 1 if the install now running (or just finished) carried the factory
 * backup across, 0 if it was the short copy. */
int fs_install_includes_factory(void) { return g_inst_with_factory; }

/* Does the media we booted from actually carry a factory backup? An
 * ISO or a freshly-flashed USB does; a disk that was itself installed
 * with the short copy does not, and blindly copying that region would
 * write 32 MiB of garbage and produce a "backup" that fails its own
 * BPB check later. */
static int source_has_factory(void) {
    static uint8_t probe[512];
    uint32_t lba = g_iso_image_offset_512 + FS_FACTORY_LBA;
    if (ata_read(fs.bus, fs.drive, lba, 1, probe) < 0) return 0;
    struct bpb* b = (struct bpb*)probe;
    return (b->bytes_per_sector == 512 && b->fat_count != 0);
}

int fs_install_start(int bus, int drive) {
    if (!fs.mounted) return -1;
    if (bus == fs.bus && drive == fs.drive) return -1;     /* refuse self */
    if (ata_identify(bus, drive) < 0) return -1;

    /* Check the target is big enough before writing a single sector,
     * rather than discovering it 33 MiB in via a write error. A drive
     * that reports 0 sectors told us nothing usable — fall back to the
     * conservative short copy rather than refusing the install. */
    uint32_t cap = ata_sectors(bus, drive);
    if (cap != 0 && cap < INST_SHORT_SECTORS) return -2;   /* too small */

    g_inst_with_factory = (cap >= INST_FULL_SECTORS) && source_has_factory();
    g_inst_total = g_inst_with_factory ? INST_FULL_SECTORS : INST_SHORT_SECTORS;
    g_inst_bus   = bus;
    g_inst_drive = drive;
    g_inst_done  = 0;
    g_inst_err_phase = 0;
    return 0;
}

int fs_install_chunk(void) {
    if (g_inst_bus < 0)            return -1;
    if (g_inst_done >= g_inst_total) return 1;
    static uint8_t buf[512 * INST_CHUNK_SECTORS];
    uint32_t remain = g_inst_total - g_inst_done;
    uint32_t take = remain > INST_CHUNK_SECTORS ? INST_CHUNK_SECTORS : remain;
    /* If we booted from a CD, the raw boxos-bootable.img lives inside
     * an ISO 9660 wrapper at offset g_iso_image_offset_512; shift the
     * read so the install copies the *embedded* image's sectors and
     * not the ISO metadata. */
    uint32_t src_lba = g_iso_image_offset_512 + g_inst_done;
    if (ata_read(fs.bus, fs.drive, src_lba, (uint8_t)take, buf) < 0) {
        g_inst_err_phase = 1; g_inst_bus = -1; return -1;
    }
    if (ata_write(g_inst_bus, g_inst_drive, g_inst_done,
                  (uint8_t)take, buf) < 0) {
        g_inst_err_phase = 2; g_inst_bus = -1; return -1;
    }
    g_inst_done += take;
    return g_inst_done >= g_inst_total ? 1 : 0;
}

int fs_install_percent(void) {
    if (g_inst_total == 0) return 0;
    return (int)((uint64_t)g_inst_done * 100 / g_inst_total);
}

/* Run after fs_install_chunk finishes. Re-mounts the FAT12 from the
 * just-imaged target drive and writes the post-install markers
 * (INSTALL.CFG with the user name + SETUP.DONE) into its /SYS/, so
 * the next boot from that drive goes straight to the desktop. After
 * this call the kernel's "current filesystem" is the target — fine,
 * we're about to reboot. Returns 0 on success. */
int fs_install_finalize(int target_bus, int target_drive,
                        const char* name, const char* company) {
    if (target_bus < 0) return -1;
    /* The image we copied is boxos-bootable.img verbatim, so its
     * FAT12 lives at sector 2048 of the target. */
    fs.mounted = false;
    g_iso_image_offset_512 = 0;          /* target is a plain ATA disk */
    if (try_mount(target_bus, target_drive, FS_OFFSET_COMBINED) != 0) return -1;
    uint16_t sys = fs_find_dir(0, "SYS");
    if (sys == 0xFFFF) return -1;

    /* INSTALL.CFG: KEY=VALUE lines. */
    static char buf[256]; int b = 0;
    const char* kn[3] = { "NAME=", "COMPANY=", "HOSTNAME=" };
    const char* nm   = (name && name[0])    ? name    : "User";
    const char* co   = (company)            ? company : "";
    const char* kv[3] = { nm, co, nm };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; kn[i][j] && b < (int)sizeof(buf) - 1; j++) buf[b++] = kn[i][j];
        for (int j = 0; kv[i][j] && b < (int)sizeof(buf) - 1; j++) buf[b++] = kv[i][j];
        if (b < (int)sizeof(buf) - 1) buf[b++] = '\n';
    }
    buf[b] = 0;
    (void)fs_delete(sys, "INSTALL.CFG");
    if (fs_create_file(sys, "INSTALL.CFG", buf, (uint32_t)b) < 0) return -1;

    static const char done[] = "SETUP.DONE\n";
    (void)fs_delete(sys, "SETUP.DONE");
    if (fs_create_file(sys, "SETUP.DONE", done, (uint32_t)(sizeof(done) - 1)) < 0)
        return -1;
    return 0;
}

/* Sweep ATA buses 0/1 and slots 0/1, returning a 4-bit mask of
 * present drives: bit 0 = bus0:drv0, bit 1 = bus0:drv1, etc. */
int fs_ata_present_mask(void) {
    int mask = 0;
    for (int bus = 0; bus < 2; bus++)
        for (int drv = 0; drv < 2; drv++)
            if (ata_identify(bus, drv) == 0)
                mask |= (1 << (bus * 2 + drv));
    return mask;
}

int fs_boot_bus(void)   { return fs.mounted ? fs.bus   : -1; }
int fs_boot_drive(void) { return fs.mounted ? fs.drive : -1; }

/* Restore the live FAT12 from the read-only factory copy embedded in
 * the bootable image. Returns:
 *    0   restored
 *   -1   legacy two-disk layout — there is no combined image here
 *   -2   I/O error mid-copy
 *   -3   combined layout, but this disk carries no factory backup.
 *        That means it was installed with the short copy onto a drive
 *        too small for the extra 32 MiB (see fs_install_start), so the
 *        caller should say that rather than blaming the disk layout.
 * Caller should reboot afterwards so fs_mount picks up the fresh state. */
int fs_factory_restore(void) {
    if (!fs.mounted)             return -1;
    if (fs.partition_lba == 0)   return -1;        /* legacy layout */

    static uint8_t buf[512];
    const uint32_t live    = FS_OFFSET_COMBINED;
    const uint32_t factory = FS_FACTORY_LBA;
    const uint32_t n       = FS_FACTORY_LBA - FS_OFFSET_COMBINED;
    int bus = fs.bus, drive = fs.drive;

    /* Sanity: factory backup region must look like a FAT12 BPB. */
    if (ata_read(bus, drive, factory, 1, buf) < 0) return -2;
    struct bpb* b = (struct bpb*)buf;
    if (b->bytes_per_sector != 512 || b->fat_count == 0) return -3;

    for (uint32_t i = 0; i < n; i++) {
        if (ata_read(bus, drive, factory + i, 1, buf) < 0) return -2;
        if (ata_write(bus, drive, live    + i, 1, buf) < 0) return -2;
    }
    /* Re-mount so the in-RAM FAT buffer reflects the freshly-restored
     * live area — otherwise the next fs_create_file would flush a
     * stale FAT back over it. (cmd_factory still reboots afterwards;
     * the Setup wizard relies on this re-mount to write its markers
     * onto the just-formatted disk.) */
    fs.mounted = false;
    (void)try_mount(bus, drive, live);
    return 0;
}

int fs_rmdir(uint16_t dir_cluster, const char* name) {
    if (!fs.mounted) return -1;
    /* Refuse to remove non-empty directories. */
    uint16_t cl = lookup(dir_cluster, name, 1, 0);
    if (cl == 0xFFFF) return -1;
    static struct fat12_entry tmp[8];
    int n = scan_dir(cl, tmp, 8);
    int real = 0;
    for (int i = 0; i < n; i++) {
        if (tmp[i].name[0] == '.') continue;
        real++;
    }
    if (real > 0) return -2;                            /* not empty */
    return fs_delete(dir_cluster, name);
}

int fs_read_in(uint16_t dir_cluster, const char* name83, void* buf, uint32_t cap) {
    uint32_t size = 0;
    uint16_t first_cluster = lookup(dir_cluster, name83, 0, &size);
    if (first_cluster == 0xFFFF) return -1;
    /* Probe-only mode: caller passed buf=NULL, just wants the size. */
    if (buf == 0) return (int)size;
    if (size > cap) return -1;

    /* Walk the cluster chain, writing into buf. */
    uint8_t* dst = (uint8_t*)buf;
    uint32_t remaining = size;
    uint16_t c = first_cluster;
    while (c >= 2 && c < 0xFF8 && remaining > 0) {
        uint32_t lba = fs.data_lba + (uint32_t)(c - 2) * fs.spc;
        for (uint32_t s = 0; s < fs.spc && remaining > 0; s++) {
            if (fread_lba(lba + s, 1, sec_buf) < 0) return -1;
            uint32_t take = remaining < 512 ? remaining : 512;
            memcpy(dst, sec_buf, take);
            dst += take;
            remaining -= take;
        }
        c = fat12_next(c);
    }
    return (int)size;
}

/* App-facing path: looks in the cwd the calling task most recently set. */
int fs_read_file(const char* name83, void* buf, uint32_t cap) {
    return fs_read_in(fs_get_app_cwd(), name83, buf, cap);
}
