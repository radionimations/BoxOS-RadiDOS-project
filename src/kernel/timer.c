/* PIT (8254) channel-0 timer.
 *
 * We program counter 0 to fire IRQ0 at the requested frequency and
 * count ticks. Apps use SYS_GET_TICKS_MS / SYS_SLEEP_MS to time
 * frame loops (Doom needs ~70 fps timing). */

#include "boxos.h"
#include "task.h"

#define PIT_BASE_HZ  1193182u
#define PIT_CH0      0x40
#define PIT_CMD      0x43

static volatile uint64_t g_ticks;
static uint32_t          g_hz = 100;

void timer_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    g_hz = hz;
    uint16_t div = (uint16_t)(PIT_BASE_HZ / hz);

    /* Channel 0, lobyte+hibyte, mode 3 (square wave), binary. */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));
}

void timer_tick(void) {
    g_ticks++;
}

uint64_t timer_ticks(void) {
    return g_ticks;
}

uint64_t timer_ms(void) {
    return (g_ticks * 1000ULL) / g_hz;
}

void timer_sleep_ms(uint64_t ms) {
    uint64_t target = timer_ms() + ms;
    while (timer_ms() < target) {
        /* Cooperative point: let other kernel tasks run while we wait
         * on the clock. The hlt parks the CPU until the next IRQ if
         * nobody else has work. */
        yield();
        __asm__ __volatile__ ("hlt");
    }
}
