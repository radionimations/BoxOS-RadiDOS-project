/* PS/2 mouse driver. Initialises the i8042 aux channel, enables IRQ12,
 * runs the IntelliMouse "magic knock" to switch into 4-byte packet mode
 * (so we get the wheel byte too), and reassembles packets in the IRQ.
 * mouse_poll() drains accumulated dx/dy/buttons for the running app
 * (e.g. Doom). The wheel itself feeds the kernel's scroll-back buffer
 * directly so scrolling Just Works at the shell. */

#include "boxos.h"

/* i8042 helpers */
static void wait_in(void)  { for (int i=0; i<100000; i++) if (!(inb(0x64) & 0x02)) return; }
static void wait_out(void) { for (int i=0; i<100000; i++) if  ( inb(0x64) & 0x01)  return; }

static void mouse_write(uint8_t b) {
    wait_in(); outb(0x64, 0xD4);   /* "next byte to aux" */
    wait_in(); outb(0x60, b);
}

static uint8_t mouse_read(void) {
    wait_out();
    return inb(0x60);
}

/* "Sample rate sequence" used to switch the mouse into IntelliMouse
 * (4-byte packet, with wheel) reporting mode. After this knock we ask
 * for the device id; the mouse should return 0x03 if it accepted. */
static int try_intellimouse_knock(void) {
    /* Set sample rate 200, 100, 80. Each command is one byte plus its
     * argument; the mouse acks every byte with 0xFA. */
    mouse_write(0xF3); (void)mouse_read();
    mouse_write(200);  (void)mouse_read();
    mouse_write(0xF3); (void)mouse_read();
    mouse_write(100);  (void)mouse_read();
    mouse_write(0xF3); (void)mouse_read();
    mouse_write(80);   (void)mouse_read();

    /* Get device id. */
    mouse_write(0xF2); (void)mouse_read();
    uint8_t id = mouse_read();
    return id == 0x03 ? 4 : 3;     /* 4-byte packets if wheel present */
}

/* IRQ-side packet reassembly. */
static volatile int      pkt_size = 3;
static volatile int      pkt_phase;
static volatile uint8_t  pkt[4];
static volatile int32_t  acc_dx;
static volatile int32_t  acc_dy;
static volatile int32_t  acc_wheel;
static volatile uint32_t btns;
static volatile int      have_data;

void mouse_init(void) {
    pkt_phase = 0;
    acc_dx = acc_dy = acc_wheel = 0;
    btns = 0;
    have_data = 0;

    /* Enable aux. */
    wait_in(); outb(0x64, 0xA8);

    /* Enable IRQ12 (bit 1) in compaq status, clear disable-mouse-clock
     * (bit 5), keep IRQ1 enabled. */
    wait_in(); outb(0x64, 0x20);
    uint8_t status = mouse_read();
    status |= 0x02;
    status &= ~0x20;
    wait_in(); outb(0x64, 0x60);
    wait_in(); outb(0x60, status);

    /* Reset to defaults so the knock starts from a known state. */
    mouse_write(0xF6); (void)mouse_read();

    /* Try to upgrade to wheel packets. */
    pkt_size = try_intellimouse_knock();

    /* Enable streaming. */
    mouse_write(0xF4); (void)mouse_read();

    pic_unmask(12);
}

void mouse_irq(void) {
    uint8_t s = inb(0x64);
    if (!(s & 0x01)) return;
    if (!(s & 0x20)) return;          /* keyboard byte; ignore */

    uint8_t b = inb(0x60);
    pkt[pkt_phase++] = b;

    /* Resync on first byte: bit 3 of flags is "always 1" — if it isn't,
     * we're mid-packet from a glitch. Drop and try again. */
    if (pkt_phase == 1 && !(b & 0x08)) { pkt_phase = 0; return; }

    if (pkt_phase < pkt_size) return;
    pkt_phase = 0;

    uint8_t flags = pkt[0];
    int32_t dx = (int32_t)pkt[1];
    int32_t dy = (int32_t)pkt[2];
    if (flags & 0x10) dx -= 256;
    if (flags & 0x20) dy -= 256;
    if (flags & 0x40 || flags & 0x80) return;          /* overflow */

    acc_dx += dx;
    acc_dy -= dy;
    btns = (uint32_t)(flags & 0x07);
    have_data = 1;

    /* Move the kernel-side cursor so it tracks the mouse even when
     * no app is polling. dy is inverted on the screen (PS/2 dy is
     * up-positive, screen y is down-positive). */
    cursor_handle_mouse(dx, -dy);

    if (pkt_size == 4) {
        int8_t w = (int8_t)pkt[3];
        if (w) {
            acc_wheel += w;
            /* Feed wheel ticks straight into the scrollback. Positive
             * wheel = scroll up (older content), negative = scroll
             * down. Three rows per tick is comfortable. */
            if (w > 0) vga_scroll_up(3 * w);
            else       vga_scroll_down(-3 * w);
        }
    }
}

uint32_t mouse_buttons(void) { return btns; }

int mouse_poll(void* out12) {
    if (!out12) return 0;
    int32_t* o = (int32_t*)out12;
    __asm__ __volatile__ ("cli");
    int32_t dx = acc_dx, dy = acc_dy;
    uint32_t b = btns;
    int hd = have_data;
    acc_dx = 0;
    acc_dy = 0;
    have_data = 0;
    __asm__ __volatile__ ("sti");
    o[0] = dx;
    o[1] = dy;
    o[2] = (int32_t)b;
    return hd;
}
