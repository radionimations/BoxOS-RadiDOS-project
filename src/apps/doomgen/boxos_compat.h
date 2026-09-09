/* BoxOS compatibility shim for doomgeneric. Force-included via
 * -include in the Makefile. */
#ifndef BOXOS_DOOMGEN_COMPAT_H
#define BOXOS_DOOMGEN_COMPAT_H

#include <stdint.h>

/* BoxOS syscall wrappers (forward-declared so we don't pull in
 * <stdbool.h> via boxos_app.h which would define `false`/`true` as
 * macros and conflict with doomtype's enum boolean). */
void     bos_puts(const char* s);
void     bos_status(const char* s);
void     bos_putc(char c);
char     bos_try_getc(void);
char     bos_getc(void);
void     gfx_mode_text(void);
void     gfx_mode_13h(void);
void     gfx_blit(const void* fb_64000_bytes);
void     gfx_palette(const void* pal_768_bytes);
void     sleep_ms(uint64_t ms);
uint64_t ticks_ms(void);
void*    xmalloc(uint64_t n);
void     xfree(void* p);
void     reboot_now(void);

/* Force CMAP256 + 320x200 for DOOM's native mode. */
#define CMAP256
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200

/* POSIX access() flags so doomgeneric doesn't need <unistd.h>. */
#ifndef R_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

#endif
