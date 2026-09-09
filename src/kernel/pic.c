#include "boxos.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20

void pic_remap(uint8_t off1, uint8_t off2) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11); io_wait();   /* ICW1: init + ICW4 */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, off1); io_wait();  /* ICW2: vector offset */
    outb(PIC2_DATA, off2); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();  /* ICW3: master has slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();  /* ICW3: slave cascade identity 2 */
    outb(PIC1_DATA, 0x01); io_wait();  /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, a1);                /* restore previous masks */
    outb(PIC2_DATA, a2);
}

void pic_mask_all(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ? irq : (uint8_t)(irq - 8);
    outb(port, (uint8_t)(inb(port) & ~(1u << bit)));
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
