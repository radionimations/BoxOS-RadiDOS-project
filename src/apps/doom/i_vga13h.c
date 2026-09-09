/* BoxOS-port replacement for FastDoom's VGA mode 13h driver.
 *
 * Original DOS version programmed VGA registers via int 0x10 + outp.
 * Here we just call the BoxOS kernel: gfx_mode_13h() to switch mode,
 * gfx_blit(backbuffer) to flip a frame.
 *
 * The differential-update fast paths (386/486 variants) are gone —
 * we always blit the entire 320x200 buffer per frame. The differential
 * logic was a DOS optimisation that doesn't pay off here. */

#include <string.h>
#include "doomtype.h"
#include "i_ibm.h"
#include "v_video.h"
#include "i_system.h"
#include "doomstat.h"
#include "i_vga13h.h"
#include "boxos_app.h"

#if defined(MODE_13H)

void (*finishfunc)(void);

/* Used by FastDoom code that referenced the 64K mode-13h vrambuffer.
 * In BoxOS we use it as the "front" buffer pcscreen points at, so
 * differential CopyDWords from backbuffer→pcscreen has somewhere to
 * land before the blit. */
extern byte vrambuffer[SCREENWIDTH * SCREENHEIGHT];

void I_CleanupVRAMbuffer(void)
{
    memset(vrambuffer, 0, SCREENWIDTH * SCREENHEIGHT);
}

void I_FinishUpdateDirect(void)
{
    /* Push the rendered backbuffer to the real VGA hardware via the
     * BoxOS syscall. We blit the whole frame regardless of which
     * dirty-region flags the engine set; correctness over speed. */
    gfx_blit(backbuffer);
    updatestate = I_NOUPDATE;
}

/* Differential variants exist only because other source files
 * reference them via finishfunc reassignment. Both fall through to
 * the direct path. */
void I_FinishUpdateDifferential386(void) { I_FinishUpdateDirect(); }
void I_FinishUpdateDifferential486(void) { I_FinishUpdateDirect(); }

void I_UpdateFinishFunc(void)
{
    finishfunc = I_FinishUpdateDirect;
}

void VGA_13H_InitGraphics(void)
{
    /* Switch the kernel to mode 13h. */
    gfx_mode_13h();
    /* DOOM uses pcscreen/destscreen as its "front" framebuffer.
     * Point them at our local 64K shadow buffer so any direct writes
     * land in RAM rather than crashing. The actual screen update goes
     * through gfx_blit(backbuffer) once per frame. */
    pcscreen = destscreen = vrambuffer;
    finishfunc = I_FinishUpdateDirect;
}

#endif
