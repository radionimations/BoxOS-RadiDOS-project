/* Sound Blaster 16 driver — single-cycle 8-bit mono PCM playback.
 *
 * Probes the DSP at the default ISA base 0x220 (IRQ 5, DMA 1). On
 * success, exposes:
 *
 *   sb16_present()        -- 1 if the DSP responded to reset
 *   sb16_play(pcm, len,r) -- copy `len` unsigned-8 samples into a
 *                            BSS DMA bounce buffer and start playback
 *                            at `r` Hz. Non-blocking; one buffer in
 *                            flight at a time.
 *   sb16_stop()           -- abort an in-flight playback
 *   sb16_irq()            -- called from interrupt_dispatch on IRQ 5
 *
 * Why this is so much code for "play a sample": the SB16 DSP needs
 * a careful reset+poll handshake, the mixer needs to be told which
 * IRQ + DMA pair we picked, and the 8237 DMA controller needs five
 * separate I/O writes (mask, mode, page, address, count) before the
 * DSP's playback command kicks the transfer off. */

#include "boxos.h"

/* DSP / mixer ports relative to base 0x220. */
#define SB_BASE        0x220
#define SB_MIXER_IDX   (SB_BASE + 0x04)
#define SB_MIXER_DATA  (SB_BASE + 0x05)
#define SB_DSP_RESET   (SB_BASE + 0x06)
#define SB_DSP_READ    (SB_BASE + 0x0A)
#define SB_DSP_WRITE   (SB_BASE + 0x0C)   /* command/data out; bit 7 of   */
                                          /* status here = 1 means busy.  */
#define SB_DSP_RSTAT   (SB_BASE + 0x0E)   /* read-data-available status   */
#define SB_DSP_ACK8    (SB_BASE + 0x0E)   /* read here acks 8-bit IRQ     */
#define SB_DSP_ACK16   (SB_BASE + 0x0F)   /* read here acks 16-bit IRQ    */

/* 8237 DMA controller — channel 1 (used for 8-bit playback). */
#define DMA_MASK       0x0A
#define DMA_MODE       0x0B
#define DMA_FF_CLR     0x0C
#define DMA1_ADDR      0x02
#define DMA1_COUNT     0x03
#define DMA1_PAGE      0x83

/* ---- low-level DSP plumbing ----------------------------------- */

static volatile int g_present = 0;
static volatile int g_playing = 0;
static uint8_t      g_dsp_major = 0, g_dsp_minor = 0;

static void short_delay(void) {
    /* A few microseconds. The DSP needs ~100us after the reset
     * pulse to settle; we pad on the safe side. */
    for (volatile int i = 0; i < 4000; i++) io_wait();
}

static void dsp_write(uint8_t v) {
    /* Bit 7 of SB_DSP_WRITE status reads as 1 while the DSP can't
     * accept another byte. Spin until it clears. */
    for (int i = 0; i < 100000; i++) {
        if (!(inb(SB_DSP_WRITE) & 0x80)) break;
    }
    outb(SB_DSP_WRITE, v);
}

static int dsp_read(uint8_t* out) {
    for (int i = 0; i < 100000; i++) {
        if (inb(SB_DSP_RSTAT) & 0x80) {
            *out = inb(SB_DSP_READ);
            return 0;
        }
    }
    return -1;
}

/* Standard reset sequence: pulse 1 then 0 on the reset port and
 * expect 0xAA back from the read port. */
static int dsp_reset(void) {
    outb(SB_DSP_RESET, 1);
    short_delay();
    outb(SB_DSP_RESET, 0);
    short_delay();
    uint8_t v = 0;
    if (dsp_read(&v) < 0) return -1;
    return (v == 0xAA) ? 0 : -1;
}

/* ---- DMA bounce buffer ---------------------------------------- *
 * 8-bit ISA DMA can't cross a 64 KiB boundary and must live in the
 * first 16 MiB of physical memory. Linker.ld puts kernel BSS at
 * 0x100000, comfortably below 16 MiB, and the aligned() attribute
 * keeps the buffer inside a single 64 KiB block. */
#define SB_BUF_SZ 32768
static uint8_t g_buf[SB_BUF_SZ] __attribute__((aligned(0x10000)));

static void program_dma(uint32_t phys, uint32_t len) {
    /* Mask channel 1 so we can reprogram it. */
    outb(DMA_MASK, 0x05);
    /* Clear the address/count flip-flop so the next two writes go
     * to low then high. */
    outb(DMA_FF_CLR, 0x00);
    /* Mode: single transfer (00), address inc (0), no auto-init (0),
     * write transfer (01 = memory -> peripheral), channel 1.
     * 0x49 = 0b01001001. */
    outb(DMA_MODE, 0x49);
    /* Address: low byte then high byte. */
    outb(DMA1_ADDR, (uint8_t)(phys & 0xFF));
    outb(DMA1_ADDR, (uint8_t)((phys >> 8) & 0xFF));
    /* Page (high 8 bits of the 24-bit physical address). */
    outb(DMA1_PAGE, (uint8_t)((phys >> 16) & 0xFF));
    /* Count - 1, low then high. */
    uint32_t c = len - 1;
    outb(DMA1_COUNT, (uint8_t)(c & 0xFF));
    outb(DMA1_COUNT, (uint8_t)((c >> 8) & 0xFF));
    /* Unmask channel 1 — the DSP will now pull bytes from RAM. */
    outb(DMA_MASK, 0x01);
}

/* ---- public API ----------------------------------------------- */

int sb16_present(void) { return g_present; }
int sb16_playing(void) { return g_playing; }

void sb16_irq(void) {
    /* SB raises IRQ 5 when the 8-bit DMA buffer drains. Reading the
     * 8-bit-ack port tells the chip "we saw it". */
    (void)inb(SB_DSP_ACK8);
    g_playing = 0;
}

/* Write a (register, value) pair to the SB16 mixer. */
static void mixer_w(uint8_t reg, uint8_t val) {
    outb(SB_MIXER_IDX,  reg);
    outb(SB_MIXER_DATA, val);
}

int sb16_init(void) {
    g_present = 0;
    g_playing = 0;
    if (dsp_reset() < 0) {
        vga_printf("[sb16] no DSP at base 0x%x (UTM: set Sound to SB16)\n",
                   SB_BASE);
        return -1;
    }
    /* Read the DSP version (cmd 0xE1 -> major, minor). */
    dsp_write(0xE1);
    dsp_read(&g_dsp_major);
    dsp_read(&g_dsp_minor);
    vga_printf("[sb16] DSP %d.%d at base 0x%x, IRQ 5, DMA 1\n",
               g_dsp_major, g_dsp_minor, SB_BASE);

    /* DSP speaker off — silences the DAC until sb16_play runs.
     * Without this, some emulators leak DC on the output. */
    dsp_write(0xD3);

    /* Reset the mixer to defaults, then explicitly mute every input
     * that isn't the DAC (voice). On real SB16 hardware the reset
     * already mutes these, but QEMU's SB16 emulation can leave the
     * FM / MIDI / CD / Line / Mic inputs at non-zero gain, which
     * passes a continuous whine through to the output mixer as soon
     * as anything writes to PIT channel 2 (port 0x61). */
    mixer_w(0x00, 0x00);    /* mixer reset */

    /* Routing: tell the SB16 which IRQ and DMA pair to use. */
    mixer_w(0x80, 0x02);    /* IRQ 5 */
    mixer_w(0x81, 0x22);    /* DMA 1 (8-bit) + DMA 5 (16-bit) */

    /* Master + voice (DAC) set LOW (~25% of max). The SB16 volume
     * registers use the top 5 bits, each step ≈ 3 dB, so 0x40 is
     * ~-24 dB vs the 0xF8 maximum. Two stages combine: master AND
     * voice. Plenty loud once we route PCM through here; keeps the
     * leak whine quiet too if something does slip past the mutes. */
    mixer_w(0x30, 0x40); mixer_w(0x31, 0x40);   /* master L/R */
    mixer_w(0x32, 0x40); mixer_w(0x33, 0x40);   /* voice  L/R */
    /* Mute MIDI, CD, Line, Mic, PC-speaker passthrough, AGC. */
    mixer_w(0x34, 0x00); mixer_w(0x35, 0x00);   /* MIDI L/R */
    mixer_w(0x36, 0x00); mixer_w(0x37, 0x00);   /* CD   L/R */
    mixer_w(0x38, 0x00); mixer_w(0x39, 0x00);   /* Line L/R */
    mixer_w(0x3A, 0x00);                        /* Mic       */
    mixer_w(0x3B, 0x00);                        /* PC spkr   */
    mixer_w(0x3C, 0x00);                        /* output mix: nothing extra */
    mixer_w(0x3D, 0x00); mixer_w(0x3E, 0x00);   /* input mix L/R */
    mixer_w(0x3F, 0x00);                        /* AGC */

    pic_unmask(5);
    g_present = 1;
    return 0;
}

int sb16_play(const uint8_t* pcm, uint32_t len, uint32_t rate_hz) {
    if (!g_present) return -1;
    if (!pcm || len == 0) return -1;
    if (rate_hz < 4000)  rate_hz = 4000;
    if (rate_hz > 44100) rate_hz = 44100;
    if (len > SB_BUF_SZ) len = SB_BUF_SZ;

    /* Bounce into the DMA-safe BSS buffer. */
    for (uint32_t i = 0; i < len; i++) g_buf[i] = pcm[i];

    program_dma((uint32_t)(uintptr_t)g_buf, len);

    /* Tell the DSP the sample rate (cmd 0x41 = output rate, high
     * byte first). SB16 takes the rate directly in Hz. */
    dsp_write(0x41);
    dsp_write((uint8_t)((rate_hz >> 8) & 0xFF));
    dsp_write((uint8_t)(rate_hz & 0xFF));

    /* Single-cycle 8-bit DMA out (cmd 0xC0 + mode 0x00 = mono,
     * unsigned), then (count - 1) low, (count - 1) high. */
    uint32_t c = len - 1;
    dsp_write(0xC0);
    dsp_write(0x00);
    dsp_write((uint8_t)(c & 0xFF));
    dsp_write((uint8_t)((c >> 8) & 0xFF));

    g_playing = 1;
    return 0;
}

void sb16_stop(void) {
    if (!g_present) return;
    /* DSP cmd 0xD0: pause 8-bit DMA. Mask the DMA channel to make
     * sure the chip really stops. */
    dsp_write(0xD0);
    outb(DMA_MASK, 0x05);
    g_playing = 0;
}
