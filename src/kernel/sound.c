/* PC speaker driver — the simplest sound device on the platform.
 *
 * The PC speaker is wired to PIT channel 2. To play a tone:
 *   1. Set PIT channel 2 to mode 3 (square wave)
 *   2. Write the count for the desired frequency: 1193182 / freq_Hz
 *   3. Set bits 0+1 of port 0x61 to gate the speaker on
 * To stop, clear those two bits.
 *
 * That gives us a square-wave beep good enough for system bells and
 * arcade-y SFX. No mixing, one voice. Apps reach this via the
 * sys_sound_* syscalls. */

#include "boxos.h"

static int g_enabled = 1;     /* respected by sound_beep_ms          */
static int g_playing = 0;

static void pit2_program(uint32_t freq_hz) {
    if (freq_hz < 20)    freq_hz = 20;
    if (freq_hz > 20000) freq_hz = 20000;
    uint32_t divisor = 1193182u / freq_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    /* Mode 3 (square wave), channel 2, lobyte/hibyte */
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
}

void sound_tone_on(uint32_t freq_hz) {
    if (!g_enabled) return;
    /* If SB16 is the active sound model (UTM with model = sb16, no
     * PC-speaker emulation), writes to port 0x61 produce nothing
     * useful on the host AND in some emulators leak as a continuous
     * whine on the SB16's analog mixer. No-op the speaker so DOOM's
     * per-tic beeps don't translate into ear pain. */
    if (sb16_present()) { g_playing = 0; return; }
    pit2_program(freq_hz);
    uint8_t v = inb(0x61);
    outb(0x61, (uint8_t)(v | 0x03));
    g_playing = 1;
}

void sound_tone_off(void) {
    if (sb16_present()) { g_playing = 0; return; }
    uint8_t v = inb(0x61);
    outb(0x61, (uint8_t)(v & ~0x03));
    g_playing = 0;
}

/* Synchronous tone for a duration. Apps that want async beeps build
 * their own scheduling on top using ticks_ms + sound_tone_on/off. */
void sound_beep_ms(uint32_t freq_hz, uint32_t ms) {
    if (!g_enabled) return;
    sound_tone_on(freq_hz);
    timer_sleep_ms(ms);
    sound_tone_off();
}

void sound_set_enabled(int on) {
    g_enabled = on ? 1 : 0;
    if (!on && g_playing) sound_tone_off();
}
int sound_is_enabled(void) { return g_enabled; }

/* Stock system beeps. */
void sound_bell(void) {
    if (!g_enabled) return;
    sound_beep_ms(1000, 80);
}

void sound_chord_ok(void) {
    if (!g_enabled) return;
    sound_beep_ms(880, 60);
    sound_beep_ms(1320, 90);
}

void sound_chord_error(void) {
    if (!g_enabled) return;
    sound_beep_ms(220, 120);
    sound_beep_ms(165, 160);
}
