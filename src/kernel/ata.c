/* Polling ATA PIO driver (LBA28). Probes both primary and secondary
 * IDE controllers and either drive on each (4 positions total). */

#include "boxos.h"

#define ATA_PRIMARY_BASE   0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECONDARY_BASE 0x170
#define ATA_SECONDARY_CTRL 0x376

#define SR_BSY  0x80
#define SR_DRDY 0x40
#define SR_DF   0x20
#define SR_DRQ  0x08
#define SR_ERR  0x01

#define CMD_READ_PIO         0x20
#define CMD_WRITE_PIO        0x30
#define CMD_FLUSH            0xE7
#define CMD_IDENTIFY         0xEC
#define CMD_IDENTIFY_PACKET  0xA1
#define CMD_PACKET           0xA0

/* Per-drive type cache, indexed by (bus*2 + drive). 0 = none / not
 * probed, 1 = ATA disk, 2 = ATAPI (CD-ROM-style). Filled in by
 * ata_identify. */
#define DRV_NONE  0
#define DRV_ATA   1
#define DRV_ATAPI 2
static uint8_t g_drv_type[4];

static uint16_t bus_base(int bus) {
    return bus == 0 ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
}
static uint16_t bus_ctrl(int bus) {
    return bus == 0 ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
}

static void ata_400ns(int bus) {
    uint16_t ctrl = bus_ctrl(bus);
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

/* Timeouts are generous on purpose: when the backing store is a
 * growing qcow2, a single PIO write can stall for milliseconds while
 * QEMU allocates clusters and updates metadata, with BSY held the
 * whole time. A too-short spin here surfaced as "WRITE ERROR TO THE
 * TARGET DRIVE" partway through the cross-disk install. */
static int wait_data(int bus) {
    uint16_t status = (uint16_t)(bus_base(bus) + 7);
    for (long i = 0; i < 20000000L; i++) {
        uint8_t s = inb(status);
        if (s & SR_ERR) return -1;
        if (s & SR_DF)  return -1;
        if (!(s & SR_BSY) && (s & SR_DRQ)) return 0;
    }
    return -1;
}

/* Wait for the drive to go fully idle (BSY clear AND DRQ clear). Both
 * are required before issuing any new command. Without the BSY wait,
 * a command issued while the drive is still busy is silently dropped.
 * Without the DRQ wait, we issue a new command while the drive is
 * still holding leftover transfer data ready, and the next inw/outw
 * either reads stale data or sends bytes into the wrong sector — the
 * symptom is "fs_read_in succeeds but the buffer comes back full of
 * 0xFF" because the drive returns the default open-bus byte. */
static int wait_ready(int bus) {
    uint16_t status = (uint16_t)(bus_base(bus) + 7);
    for (long i = 0; i < 40000000L; i++) {
        uint8_t s = inb(status);
        if (s & SR_ERR) return -1;
        if (s & SR_DF)  return -1;
        if (!(s & SR_BSY) && !(s & SR_DRQ)) return 0;
    }
    return -1;
}

/* Try the ATA IDENTIFY DEVICE command. Returns 0 on success and
 * drains the 256-word response. Returns -1 if the drive doesn't
 * answer or returns ERR (which is the standard ATAPI signal). */
static int try_ata_identify(int bus, int drive) {
    uint16_t base = bus_base(bus);
    uint8_t  sel  = (drive == 0) ? 0xA0 : 0xB0;
    outb((uint16_t)(base + 6), sel);
    ata_400ns(bus);
    outb((uint16_t)(base + 2), 0);
    outb((uint16_t)(base + 3), 0);
    outb((uint16_t)(base + 4), 0);
    outb((uint16_t)(base + 5), 0);
    outb((uint16_t)(base + 7), CMD_IDENTIFY);
    uint8_t s = inb((uint16_t)(base + 7));
    if (s == 0) return -1;
    for (int i = 0; i < 100000; i++) {
        s = inb((uint16_t)(base + 7));
        if (s & SR_ERR) return -1;
        if (!(s & SR_BSY) && (s & SR_DRQ)) break;
    }
    if (!(s & SR_DRQ)) return -1;
    for (int i = 0; i < 256; i++) (void)inw(base);
    return 0;
}

/* ATAPI IDENTIFY PACKET DEVICE. Same shape as IDENTIFY but for
 * packet (CD/DVD-style) devices. Returns 0 on success and drains
 * the response. */
static int try_atapi_identify(int bus, int drive) {
    uint16_t base = bus_base(bus);
    uint8_t  sel  = (drive == 0) ? 0xA0 : 0xB0;
    outb((uint16_t)(base + 6), sel);
    ata_400ns(bus);
    outb((uint16_t)(base + 7), CMD_IDENTIFY_PACKET);
    uint8_t s = inb((uint16_t)(base + 7));
    if (s == 0) return -1;
    for (int i = 0; i < 100000; i++) {
        s = inb((uint16_t)(base + 7));
        if (s & SR_ERR) return -1;
        if (!(s & SR_BSY) && (s & SR_DRQ)) break;
    }
    if (!(s & SR_DRQ)) return -1;
    for (int i = 0; i < 256; i++) (void)inw(base);
    return 0;
}

int ata_identify(int bus, int drive) {
    int idx = bus * 2 + drive;
    if (try_ata_identify(bus, drive) == 0) {
        g_drv_type[idx] = DRV_ATA;
        return 0;
    }
    if (try_atapi_identify(bus, drive) == 0) {
        g_drv_type[idx] = DRV_ATAPI;
        return 0;
    }
    g_drv_type[idx] = DRV_NONE;
    return -1;
}

int ata_drive_type(int bus, int drive) {
    return g_drv_type[bus * 2 + drive];
}

/* ---- ATAPI READ_10 ------------------------------------------- *
 *
 * ATAPI drives speak SCSI command packets over the IDE bus. We
 * implement just enough to read sectors:
 *   PACKET (0xA0) opcode lets us hand a 12-byte SCSI command;
 *   READ_10 (0xA8) gets us back 2048-byte sectors. FAT12 thinks in
 *   512-byte sectors, so we read the enclosing 2 KiB block and copy
 *   the right 512-byte slice into the caller's buffer. */

static int atapi_send_packet(int bus, int drive, const uint8_t* cmd12,
                             void* buf, uint32_t buf_bytes) {
    uint16_t base = bus_base(bus);
    uint8_t  sel  = (drive == 0) ? 0xA0 : 0xB0;
    outb((uint16_t)(base + 6), sel);
    ata_400ns(bus);
    outb((uint16_t)(base + 1), 0);                     /* features = 0 */
    outb((uint16_t)(base + 4), (uint8_t)(buf_bytes & 0xFF));
    outb((uint16_t)(base + 5), (uint8_t)((buf_bytes >> 8) & 0xFF));
    outb((uint16_t)(base + 7), CMD_PACKET);

    /* Wait for the drive to assert DRQ for the command transfer. */
    {
        int ok = 0;
        for (int i = 0; i < 1000000; i++) {
            uint8_t s = inb((uint16_t)(base + 7));
            if (s & SR_ERR) return -1;
            if (!(s & SR_BSY) && (s & SR_DRQ)) { ok = 1; break; }
        }
        if (!ok) return -1;
    }
    /* Write the 12-byte SCSI command as six 16-bit words. */
    for (int i = 0; i < 6; i++) {
        uint16_t w = (uint16_t)cmd12[i * 2] |
                     ((uint16_t)cmd12[i * 2 + 1] << 8);
        outw(base, w);
    }
    /* Data phase. */
    {
        int ok = 0;
        for (int i = 0; i < 1000000; i++) {
            uint8_t s = inb((uint16_t)(base + 7));
            if (s & SR_ERR) return -1;
            if (!(s & SR_BSY) && (s & SR_DRQ)) { ok = 1; break; }
        }
        if (!ok) return -1;
    }
    uint16_t* p = (uint16_t*)buf;
    uint32_t words = buf_bytes / 2;
    for (uint32_t i = 0; i < words; i++) p[i] = inw(base);
    /* Settle. */
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb((uint16_t)(base + 7));
        if (!(s & SR_BSY) && !(s & SR_DRQ)) break;
    }
    return 0;
}

static int atapi_read_2k(int bus, int drive, uint32_t lba_2k, void* buf2048) {
    /* SCSI READ_10 (0x28) — 10-byte CDB padded out to the 12-byte
     * packet our drive expects:
     *   [0]    opcode 0x28
     *   [1]    flags  (0)
     *   [2..5] LBA, 32-bit big-endian
     *   [6]    group  (0)
     *   [7..8] transfer length in 2-KiB sectors, 16-bit big-endian
     *   [9..11] control / pad (0)
     * (The earlier draft used READ_12 0xA8 but kept the length byte at
     * cmd[8], which told the drive to send 256 sectors. The drive's
     * protocol-error retry made every read fail silently.) */
    uint8_t cmd[12] = {0};
    cmd[0] = 0x28;
    cmd[2] = (uint8_t)((lba_2k >> 24) & 0xFF);
    cmd[3] = (uint8_t)((lba_2k >> 16) & 0xFF);
    cmd[4] = (uint8_t)((lba_2k >>  8) & 0xFF);
    cmd[5] = (uint8_t)( lba_2k        & 0xFF);
    cmd[7] = 0;
    cmd[8] = 1;                       /* one 2-KiB sector */
    return atapi_send_packet(bus, drive, cmd, buf2048, 2048);
}

/* Read `count` 512-byte sectors starting at `lba` (in 512-byte
 * units) from an ATAPI drive. */
static int atapi_read(int bus, int drive, uint32_t lba, uint8_t count, void* buf) {
    static uint8_t scratch[2048];
    uint8_t* dst = (uint8_t*)buf;
    while (count > 0) {
        uint32_t atapi_lba = lba / 4;
        uint32_t off       = (lba % 4) * 512;
        if (atapi_read_2k(bus, drive, atapi_lba, scratch) < 0) return -1;
        uint32_t take = 2048 - off;
        if ((uint32_t)count * 512 < take) take = (uint32_t)count * 512;
        for (uint32_t i = 0; i < take; i++) dst[i] = scratch[off + i];
        dst   += take;
        lba   += take / 512;
        count -= (uint8_t)(take / 512);
    }
    return 0;
}

int ata_read(int bus, int drive, uint32_t lba, uint8_t count, void* buf) {
    /* If we identified the drive as ATAPI, take the packet path so
     * UTM/QEMU configs that mark the .iso "Removable" (and emulate
     * CD-ROM under the hood) still read correctly. */
    if (g_drv_type[bus * 2 + drive] == DRV_ATAPI)
        return atapi_read(bus, drive, lba, count, buf);
    uint16_t base = bus_base(bus);
    uint8_t  sel  = (drive == 0) ? 0xE0 : 0xF0;     /* LBA mode */

    if (wait_ready(bus) < 0) return -1;
    outb((uint16_t)(base + 6), (uint8_t)(sel | ((lba >> 24) & 0x0F)));
    ata_400ns(bus);
    outb((uint16_t)(base + 1), 0);
    outb((uint16_t)(base + 2), count);
    outb((uint16_t)(base + 3), (uint8_t)(lba & 0xFF));
    outb((uint16_t)(base + 4), (uint8_t)((lba >> 8) & 0xFF));
    outb((uint16_t)(base + 5), (uint8_t)((lba >> 16) & 0xFF));
    outb((uint16_t)(base + 7), CMD_READ_PIO);

    uint16_t* p = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (wait_data(bus) < 0) return -1;
        for (int i = 0; i < 256; i++) p[s * 256 + i] = inw(base);
        ata_400ns(bus);
    }
    return 0;
}

int ata_write(int bus, int drive, uint32_t lba, uint8_t count, const void* buf) {
    /* ATAPI drives in our setup are CD-ROM-style and read-only;
     * refuse writes so callers (e.g. fs_install_chunk if someone
     * picked the wrong direction) fail fast instead of silently
     * succeeding and producing garbage. */
    if (g_drv_type[bus * 2 + drive] == DRV_ATAPI) return -1;
    uint16_t base = bus_base(bus);
    uint8_t  sel  = (drive == 0) ? 0xE0 : 0xF0;

    if (wait_ready(bus) < 0) return -1;
    outb((uint16_t)(base + 6), (uint8_t)(sel | ((lba >> 24) & 0x0F)));
    ata_400ns(bus);
    outb((uint16_t)(base + 1), 0);
    outb((uint16_t)(base + 2), count);
    outb((uint16_t)(base + 3), (uint8_t)(lba & 0xFF));
    outb((uint16_t)(base + 4), (uint8_t)((lba >> 8) & 0xFF));
    outb((uint16_t)(base + 5), (uint8_t)((lba >> 16) & 0xFF));
    outb((uint16_t)(base + 7), CMD_WRITE_PIO);

    const uint16_t* p = (const uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (wait_data(bus) < 0) return -1;
        for (int i = 0; i < 256; i++) outw(base, p[s * 256 + i]);
        ata_400ns(bus);
    }
    /* Wait for the drive to finish writing the last sector before
     * issuing CMD_FLUSH — otherwise the FLUSH lands while BSY is set
     * and gets silently dropped, leaving subsequent FAT/dir writes
     * non-durable. */
    if (wait_ready(bus) < 0) return -1;
    outb((uint16_t)(base + 7), CMD_FLUSH);
    if (wait_ready(bus) < 0) return -1;
    return 0;
}
