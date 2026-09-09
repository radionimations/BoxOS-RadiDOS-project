#include <string.h>
#include <dos.h>
#include <conio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "doomtype.h"
#include "i_ibm.h"
#include "v_video.h"
#include "tables.h"
#include "math.h"
#include "i_system.h"
#include "i_vga.h"
#include "i_gamma.h"

#include "doomstat.h"

#if defined(MODE_13H) || defined(MODE_X) || defined(MODE_Y) || defined(MODE_Y_HALF)

byte processedpalette[14 * 768];

void I_ProcessPalette(byte *palette)
{
    int i;

    byte *ptr = gammatable;

    for (i = 0; i < 14 * 768; i += 4, palette += 4)
    {
        processedpalette[i] = ptr[*palette];
        processedpalette[i + 1] = ptr[*(palette + 1)];
        processedpalette[i + 2] = ptr[*(palette + 2)];
        processedpalette[i + 3] = ptr[*(palette + 3)];
    }
}

/* BoxOS-port note: the DAC writes are gone; we go through the BoxOS
 * graphics syscall instead. PLAYPAL is 0..255 per channel; the kernel
 * gfx_palette already masks down to 6 bits internally. */
void gfx_palette(const void* p);    /* forward decl from boxos_app.h */
void I_SetPalette(int numpalette)
{
    int pos = Mul768(numpalette);
    /* Convert 0..255 → 0..63 for the VGA DAC (kernel ANDs with 0x3F
     * but for our PLAYPAL data we want a true shift). Stage in a
     * small local buffer and hand it to the syscall. */
    static byte vga_pal[768];
    int i;
    byte* p = processedpalette + pos;
    for (i = 0; i < 768; i++) vga_pal[i] = p[i] >> 2;
    gfx_palette(vga_pal);
}

#endif
