#include "boxos.h"

/* Special "key" codes returned through the ring buffer for non-ASCII
 * keys we want the shell to react to (currently just arrows aren't
 * wired - keeping things simple). */

#define KBD_BUF_SIZE 256

static volatile char     kbd_buf[KBD_BUF_SIZE];
static volatile uint16_t kbd_head;
static volatile uint16_t kbd_tail;

/* Parallel event ring — 16-bit values, bit 8 = pressed, bits 7..0 = ASCII.
 * Apps that need press/release semantics (games) read this; the shell still
 * uses the char ring above. Both rings are populated from the IRQ. */
static volatile uint16_t kev_buf[KBD_BUF_SIZE];
static volatile uint16_t kev_head;
static volatile uint16_t kev_tail;

static bool    shift_down;
static bool    caps_lock;
static bool    ctrl_down;
static bool    extended_pending;  /* last byte was 0xE0 prefix */
static uint8_t last_sc;        /* for diagnostics */

uint8_t keyboard_last_scancode(void) { return last_sc; }

/* US QWERTY scancode set 1 (XT). Index = make-code byte. */
static const char sc_to_ascii[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',  0, '\\',
    'z',  'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
};

static const char sc_to_ascii_shift[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  0, '|',
    'Z',  'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0,
};

static void enqueue(char c) {
    /* Any actual user input snaps the scrollback view back to live so
     * the prompt the user is typing into is visible. */
    vga_scroll_to_bottom();
    uint16_t next = (uint16_t)((kbd_head + 1) % KBD_BUF_SIZE);
    if (next == kbd_tail) return;     /* drop on overflow */
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static void enqueue_ev(uint16_t ev) {
    uint16_t next = (uint16_t)((kev_head + 1) % KBD_BUF_SIZE);
    if (next == kev_tail) return;
    kev_buf[kev_head] = ev;
    kev_head = next;
}

void keyboard_init(void) {
    kbd_head = kbd_tail = 0;
    kev_head = kev_tail = 0;
    shift_down = caps_lock = ctrl_down = false;
    /* Drain anything the controller queued from BIOS-time. Bounded
     * so we don't hang forever if the i8042 is missing or wedged
     * (UTM has been observed to keep returning 'data available'). */
    for (int i = 0; i < 64; i++) {
        if (!(inb(0x64) & 1)) break;
        (void)inb(0x60);
    }
}

void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    last_sc = sc;

    /* 0xE0 means the next byte is an "extended" scancode (arrow keys,
     * PgUp/PgDn, etc). Stash a flag and consume the next byte. */
    if (sc == 0xE0) { extended_pending = true; return; }
    if (extended_pending) {
        extended_pending = false;
        if (sc & 0x80) return;          /* ignore extended releases */
        switch (sc) {
            case 0x49: vga_scroll_up(12);     return; /* PgUp */
            case 0x51: vga_scroll_down(12);   return; /* PgDn */
            case 0x47: vga_scroll_up(9999);   return; /* Home */
            case 0x4F: vga_scroll_to_bottom();return; /* End  */
            /* Arrows: emit as ASCII-out-of-band codes 0x80..0x83 so
             * apps can use them via gui_poll_event without breaking
             * DOOM (which still reads keyboard_last_scancode). */
            case 0x48: enqueue(0x80); enqueue_ev(0x180); return; /* Up */
            case 0x50: enqueue(0x81); enqueue_ev(0x181); return; /* Down */
            case 0x4B: enqueue(0x82); enqueue_ev(0x182); return; /* Left */
            case 0x4D: enqueue(0x83); enqueue_ev(0x183); return; /* Right */
            default:                            return;
        }
    }

    /* Shift / ctrl press / release. */
    if (sc == 0x2A || sc == 0x36) { shift_down = true;  return; }
    if (sc == 0xAA || sc == 0xB6) { shift_down = false; return; }
    if (sc == 0x1D)               { ctrl_down  = true;  return; }
    if (sc == 0x9D)               { ctrl_down  = false; return; }
    if (sc == 0x3A)               { caps_lock = !caps_lock; return; }

    int pressed = !(sc & 0x80);
    uint8_t code = sc & 0x7F;
    if (code >= 128) return;

    /* F-keys: scancodes 0x3B..0x44 (F1..F10) and 0x57/0x58 (F11/F12).
     * We surface F11 and F12 as ASCII 0x84 / 0x85 for "screenshot"
     * and "future use"; the rest are ignored for now. */
    if (code == 0x58) {
        if (pressed) enqueue(0x85);
        enqueue_ev((uint16_t)((pressed ? 0x100 : 0) | 0x85));
        return;
    }
    if (code == 0x57) {
        if (pressed) enqueue(0x84);
        enqueue_ev((uint16_t)((pressed ? 0x100 : 0) | 0x84));
        return;
    }

    char base  = sc_to_ascii[code];
    char shift = sc_to_ascii_shift[code];
    char c;

    if (base >= 'a' && base <= 'z') {
        c = (shift_down ^ caps_lock) ? shift : base;
    } else {
        c = shift_down ? shift : base;
    }
    if (!c) return;
    if (pressed) enqueue(c);
    enqueue_ev((uint16_t)((pressed ? 0x100 : 0) | (uint8_t)c));
}

char keyboard_getc(void) {
    while (kbd_head == kbd_tail) {
        __asm__ __volatile__ ("hlt");
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (uint16_t)((kbd_tail + 1) % KBD_BUF_SIZE);
    return c;
}

/* Non-blocking variant for game-loop polling (Doom needs this). */
char keyboard_try_getc(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buf[kbd_tail];
    kbd_tail = (uint16_t)((kbd_tail + 1) % KBD_BUF_SIZE);
    return c;
}

/* Returns 0 if no event, else (pressed << 8) | ascii. */
uint16_t keyboard_try_get_event(void) {
    if (kev_head == kev_tail) return 0;
    uint16_t ev = kev_buf[kev_tail];
    kev_tail = (uint16_t)((kev_tail + 1) % KBD_BUF_SIZE);
    return ev;
}

/* Drop every queued ASCII char and key-event. Called by the loader
 * before handing control to a new app, so the app starts with a
 * clean input state instead of inheriting the user's pre-launch
 * keystrokes (release events in particular were bleeding into
 * gui_poll_event in M5). */
void keyboard_drain(void) {
    kbd_tail = kbd_head;
    kev_tail = kev_head;
}

/* Push a synthetic key event (a press) into both rings, so that apps
 * polling either bos_getc or gui_poll_event see it as if the user
 * had typed it. Used by the WM to implement the title-bar X button
 * (click → inject Esc). */
void keyboard_inject_key(uint8_t ascii) {
    /* ASCII ring */
    uint16_t next = (uint16_t)((kbd_head + 1) % KBD_BUF_SIZE);
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = (char)ascii;
        kbd_head = next;
    }
    /* Event ring — pressed=1 in upper byte. */
    next = (uint16_t)((kev_head + 1) % KBD_BUF_SIZE);
    if (next != kev_tail) {
        kev_buf[kev_head] = (uint16_t)((1 << 8) | ascii);
        kev_head = next;
    }
}
