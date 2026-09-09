/* doomgeneric platform layer for BoxOS.
 *
 * Implements the 6 functions doomgeneric requires plus the app
 * entry point. Resolution is forced to 320x200 (mode 13h native);
 * pixel_t is uint8_t (palette index) via -DCMAP256. */

#include "boxos_app.h"
#include "doomkeys.h"
#include "doomgeneric.h"
#include "d_event.h"
extern void D_PostEvent(event_t* ev);

/* --- key queue: DG_GetKey returns ONE event per call ----------- */

#define KQ_SIZE 32
static unsigned short kq[KQ_SIZE];
static unsigned       kq_w;
static unsigned       kq_r;

static void kq_push(int pressed, unsigned char key) {
    unsigned next = (kq_w + 1) & (KQ_SIZE - 1);
    if (next == kq_r) return;     /* drop on overflow */
    kq[kq_w] = (unsigned short)((pressed ? 0x100 : 0) | key);
    kq_w = next;
}

/* Translate BoxOS ASCII key to a doomgeneric KEY_*. */
static unsigned char ascii_to_doomkey(char c) {
    if (c == 27)               return KEY_ESCAPE;
    if (c == '\n' || c == '\r')return KEY_ENTER;
    if (c == 8 || c == 127)    return KEY_BACKSPACE;
    if (c == '\t')             return KEY_TAB;
    if (c == ' ')              return KEY_USE;          /* SPACE = open/use */
    if (c == 'w' || c == 'W')  return KEY_UPARROW;
    if (c == 's' || c == 'S')  return KEY_DOWNARROW;
    if (c == 'a' || c == 'A')  return KEY_LEFTARROW;
    if (c == 'd' || c == 'D')  return KEY_RIGHTARROW;
    if (c == 'f' || c == 'F')  return KEY_FIRE;
    if (c >= 32 && c <= 126)   return (unsigned char)c;
    return 0;
}

/* Real press/release events arrive from the kernel via SYS_GET_KEY_EVENT.
 * Each event is (pressed<<8)|ascii. Translate ASCII to KEY_*, push paired
 * press/release straight into the doomgeneric queue. */
static void poll_keyboard(void) {
    unsigned int ev;
    while ((ev = bos_try_get_key()) != 0) {
        unsigned char c = (unsigned char)(ev & 0xFF);
        unsigned char k = ascii_to_doomkey((char)c);
        if (!k) continue;
        kq_push((ev >> 8) & 1, k);
    }
}

/* Drain accumulated PS/2 mouse motion + buttons into a DOOM ev_mouse
 * event. data1 = button bitmask (bit 0 = L, bit 1 = R, bit 2 = M),
 * data2 = dx (turning), data3 = dy (forward when -strafe held). */
static void poll_mouse(void) {
    int32_t buf[3] = { 0, 0, 0 };
    if (!bos_mouse_poll(buf)) return;
    event_t ev;
    ev.type  = ev_mouse;
    ev.data1 = buf[2];                 /* button mask                  */
    ev.data2 = buf[0] * 4;             /* dx — DOS-era driver scaling  */
    ev.data3 = -buf[1] * 4;            /* dy inverted to match DOOM    */
    D_PostEvent(&ev);
}

/* --- DG_* platform functions ----------------------------------- */

void DG_Init(void) {
    /* Stay in text mode through DOOM init so the user can see
     * progress logs. Mode 13h is entered lazily on the first frame. */
    bos_puts("[DG] DG_Init (deferred mode13h)\n");
}

static int mode_switched = 0;

void DG_DrawFrame(void) {
    if (!mode_switched) {
        bos_puts("[DG] first frame -- switching to mode 13h\n");
        gfx_mode_13h();
        mode_switched = 1;
    }
    poll_keyboard();
    poll_mouse();
    gfx_blit(DG_ScreenBuffer);
}

void DG_SleepMs(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return (uint32_t)ticks_ms();
}

int DG_GetKey(int* pressed, unsigned char* key) {
    poll_keyboard();
    if (kq_r == kq_w) return 0;       /* no event */
    unsigned short ev = kq[kq_r];
    kq_r = (kq_r + 1) & (KQ_SIZE - 1);
    *pressed = (ev >> 8) & 1;
    *key     = (unsigned char)(ev & 0xFF);
    return 1;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;        /* no window manager */
}

/* --- I_SetPalette: doomgeneric expects platform to install the DOOM
 * palette into the display. We translate to 0..63 for VGA DAC and
 * call gfx_palette. */
void I_SetPalette(unsigned char* palette) {
    static unsigned char vga_pal[768];
    for (int i = 0; i < 768; i++) vga_pal[i] = palette[i] >> 2;
    gfx_palette(vga_pal);
}

/* --- BoxOS entry --------------------------------------------- */

extern void doomgeneric_Create(int argc, char** argv);
extern void doomgeneric_Tick(void);

#include "setjmp.h"
extern void (*exit_unwind)(int code);

static char  prog_name[] = "doomgen";
static char* fake_argv[2] = { prog_name, 0 };
static jmp_buf exit_jb;

static void unwind_to_shell(int code) {
    longjmp(exit_jb, code ? code : 1);
}

int app_main(const char* args) {
    (void)args;
    bos_puts("[DOOMGEN] starting\n");
    int code = setjmp(exit_jb);
    if (code == 0) {
        exit_unwind = unwind_to_shell;
        doomgeneric_Create(1, fake_argv);
        bos_puts("[DOOMGEN] entering main loop\n");
        while (1) doomgeneric_Tick();
    }
    /* Returning here means DOOM called exit() / I_Quit. Hand the
     * display back to the shell. */
    exit_unwind = 0;
    gfx_mode_text();
    bos_puts("[DOOMGEN] returned to shell\n");
    return code;
}
