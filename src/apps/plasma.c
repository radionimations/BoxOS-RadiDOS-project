/* PLASMA.BIN - 320x200 plasma demo in C, the obligatory flex once
 * VGA mode 13h, the PIT clock, and the C app toolchain are all up.
 *
 * Press ESC (or Q) to quit.
 */

#include "boxos_app.h"

static uint8_t fb[GFX_W * GFX_H];   /* zeroed by crt0 via .bss */

/* 256-entry signed-sine table, range -127..127. We pre-compute it
 * with a 4x(1-x) parabola, mirrored, which is good enough for plasma
 * patterns and keeps everything integer. */
static int16_t s_sin[256];

static int16_t isin_step(int x) {
    /* x in [0,64): rising parabola -> [0,127] */
    int q = x & 63;
    int v = (q * (64 - q)) >> 4;     /* 0..63 */
    return (int16_t)(v * 2);          /* 0..126 */
}

static void build_sin(void) {
    for (int i = 0; i < 64;  i++) s_sin[i]       =  isin_step(i);
    for (int i = 0; i < 64;  i++) s_sin[64 + i]  =  isin_step(63 - i);
    for (int i = 0; i < 64;  i++) s_sin[128 + i] = -isin_step(i);
    for (int i = 0; i < 64;  i++) s_sin[192 + i] = -isin_step(63 - i);
}

static int16_t isin(int a) { return s_sin[a & 0xFF]; }

/* Build a fiery palette: black -> red -> yellow -> white. */
static void build_palette(uint8_t* pal) {
    for (int i = 0; i < 256; i++) {
        int r, g, b;
        if (i < 64)         { r = i;             g = 0;            b = 0; }
        else if (i < 128)   { r = 63;            g = (i - 64);     b = 0; }
        else if (i < 192)   { r = 63;            g = 63;           b = (i - 128) >> 1; }
        else                { r = 63;            g = 63;           b = 31 + ((i - 192) >> 1); }
        pal[i * 3 + 0] = (uint8_t)r;
        pal[i * 3 + 1] = (uint8_t)g;
        pal[i * 3 + 2] = (uint8_t)b;
    }
}

int app_main(const char* args) {
    (void)args;

    build_sin();

    static uint8_t pal[768];
    build_palette(pal);

    gfx_mode_13h();
    gfx_palette(pal);

    int t = 0;
    for (;;) {
        char k = bos_try_getc();
        if (k == 27 || k == 'q' || k == 'Q') {
            /* Under BoxOS 2.0 the kernel auto-tears-down our
             * graphics window when we return; we don't need to call
             * gfx_mode_text() ourselves. */
            return 0;
        }

        /* Classic 4-sine plasma:
         *   v = sin(x/8 + t) + sin(y/4 + t/2)
         *     + sin((x+y)/8 + t/3) + sin(sqrt(...)/8 + t/4)
         * We use cheaper integer surrogates. */
        for (int y = 0; y < GFX_H; y++) {
            int sy  = isin((y * 3) + t * 1);          /* -127..127 */
            int sy2 = isin((y * 5) + (t >> 1));
            for (int x = 0; x < GFX_W; x++) {
                int sx  = isin((x * 4) + (t >> 1));
                int sxy = isin((x + y) * 2 + (t * 2));
                int v   = sx + sy + sy2 + sxy;        /* -508..508 */
                /* Map to 0..255 palette index. */
                fb[y * GFX_W + x] = (uint8_t)(((v + 512) >> 2) & 0xFF);
            }
        }

        gfx_blit(fb);
        sleep_ms(20);                                  /* ~50 fps cap */
        t += 4;
    }
}
