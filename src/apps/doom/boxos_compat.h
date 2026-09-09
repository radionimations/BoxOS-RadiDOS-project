/* BoxOS compatibility shim for FastDoom.
 *
 * FastDoom was written for OpenWatcom + DOS. This header papers
 * over the DOS APIs and Watcom keywords so the upstream source can
 * compile against GCC + BoxOS without us touching every .c file.
 *
 * Strategy: provide stub headers (<dos.h> etc. as empty), kill DOS
 * keywords (__far, __near, _huge -> nothing), and route DOS-style
 * I/O through BoxOS syscalls or no-ops.
 *
 * This header is force-included via -include in the Makefile if it
 * grows. For now, the FastDoom .c files include their own headers
 * and we shim those via the DOS-style stubs below. */

#ifndef BOXOS_DOOM_COMPAT_H
#define BOXOS_DOOM_COMPAT_H

#include <stdint.h>
/* Hand-rolled forward decls of the BoxOS syscall wrappers we need.
 * NOT #including boxos_app.h here because that pulls in <stdbool.h>
 * which defines `false` as a macro and breaks doomtype.h's
 * `enum { false, true } boolean`. */
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

/* POSIX access() mode flags. Force-defined here because FastDoom
 * uses access(p, R_OK) without always #including <unistd.h>. */
#ifndef R_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

/* ---- Watcom keywords -> nothing on GCC ----------------------- */
#define __far
#define __near
#define _far
#define _near
#define _huge
#define __pascal
#define __cdecl
#define __interrupt
#define __loadds
#define cdecl
#define pascal

/* Watcom #pragma aux is a no-op for our purposes. GCC ignores
 * unknown pragmas, but spell it out so it doesn't warn. */

/* ---- minimal stubs for DOS port I/O -------------------------- */
/* FastDoom uses these for VGA register access (palette, vsync).
 * BoxOS apps cannot do raw port I/O from ring 3 yet, so these are
 * no-ops. The renderer's vsync waits become busy-loops that exit
 * on the first iteration; gfx_blit syncs implicitly via the kernel. */
static inline int  inp(unsigned short port)               { (void)port; return 0; }
static inline int  outp(unsigned short port, int val)     { (void)port; (void)val; return val; }
static inline unsigned inpw(unsigned short port)          { (void)port; return 0; }
static inline unsigned outpw(unsigned short port, unsigned val) { (void)port; (void)val; return val; }

/* ---- DOS interrupt facade (used by i_ibm.c, etc.) ----------- */
union REGS {
    struct { unsigned int ax, bx, cx, dx, si, di, cflag, flags; } w;
    struct { unsigned char al, ah, bl, bh, cl, ch, dl, dh; } h;
    struct { unsigned int eax, ebx, ecx, edx, esi, edi, cflag, flags; } x;
};
struct SREGS { unsigned int es, cs, ss, ds, fs, gs; };

/* int86/int386: stub. Real code paths invoking DOS interrupts are
 * intentionally broken at link time so we know to replace them. */
static inline int int86(int n, union REGS* in, union REGS* out)  { (void)n; (void)in; (void)out; return 0; }
static inline int int386(int n, union REGS* in, union REGS* out) { (void)n; (void)in; (void)out; return 0; }
static inline int int386x(int n, union REGS* in, union REGS* out, struct SREGS* s) { (void)n; (void)in; (void)out; (void)s; return 0; }

/* ---- delay() — DOS spinning sleep -------------------------- */
void sleep_ms(uint64_t ms);  /* from boxos_app.h */
static inline void delay(unsigned ms) { sleep_ms(ms); }

/* ---- DOS file-enumeration shims (for FastDoom's bench mode) -- */
/* We never enumerate files on BoxOS; the bench mode path is dead. */
struct find_t {
    char     reserved[21];
    char     attrib;
    unsigned wr_time;
    unsigned wr_date;
    long     size;
    char     name[260];
};
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_VOLID  0x08
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20
static inline unsigned _dos_findfirst(const char* p, unsigned a, struct find_t* f) { (void)p; (void)a; (void)f; return 1; }
static inline unsigned _dos_findnext (struct find_t* f) { (void)f; return 1; }
static inline unsigned _dos_findclose(struct find_t* f) { (void)f; return 0; }

/* ---- sscanf shim (declaration only; bench path never runs) -- */
int sscanf(const char* s, const char* fmt, ...);

#endif
