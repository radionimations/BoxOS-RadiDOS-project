/* Minimal hosted-libc on top of BoxOS syscalls.
 *
 * Provides what FastDoom expects from a normal libc: printf,
 * malloc/free, string ops, qsort, atoi, etc. Backed by BoxOS's
 * bos_* syscalls and the kernel heap. */

#include "boxos_app.h"
#include <stdarg.h>

/* Forward decls of types from our shim headers. We don't include the
 * shim headers here because they declare these functions; we want to
 * *define* them. */
typedef unsigned long size_t_;
#define SIZE_T size_t_

/* ---- string ops ----------------------------------------------- */

void* memset(void* dst, int c, unsigned long n) {
    unsigned char* d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
void* memcpy(void* dst, const void* src, unsigned long n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void* memmove(void* dst, const void* src, unsigned long n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    if (d == s || n == 0) return dst;
    if (d < s) { while (n--) *d++ = *s++; }
    else       { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
int memcmp(const void* a, const void* b, unsigned long n) {
    const unsigned char* x = a;
    const unsigned char* y = b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
void* memchr(const void* s, int c, unsigned long n) {
    const unsigned char* p = s;
    while (n--) { if (*p == (unsigned char)c) return (void*)p; p++; }
    return 0;
}

unsigned long strlen(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}
char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++)) ;
    return r;
}
char* strncpy(char* d, const char* s, unsigned long n) {
    char* r = d;
    while (n && (*d = *s)) { d++; s++; n--; }
    while (n--) *d++ = 0;
    return r;
}
char* strcat(char* d, const char* s) {
    char* r = d;
    while (*d) d++;
    while ((*d++ = *s++)) ;
    return r;
}
char* strncat(char* d, const char* s, unsigned long n) {
    char* r = d;
    while (*d) d++;
    while (n-- && (*d = *s)) { d++; s++; }
    *d = 0;
    return r;
}
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int strncmp(const char* a, const char* b, unsigned long n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int ci(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int strcasecmp(const char* a, const char* b) {
    while (*a && ci(*a) == ci(*b)) { a++; b++; }
    return ci(*a) - ci(*b);
}
int strncasecmp(const char* a, const char* b, unsigned long n) {
    while (n && *a && ci(*a) == ci(*b)) { a++; b++; n--; }
    if (!n) return 0;
    return ci(*a) - ci(*b);
}
int stricmp(const char* a, const char* b)              { return strcasecmp(a, b); }
int strnicmp(const char* a, const char* b, unsigned long n) { return strncasecmp(a, b, n); }

char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == 0) ? (char*)s : 0;
}
char* strrchr(const char* s, int c) {
    const char* last = 0;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char*)last;
}
char* strstr(const char* h, const char* n) {
    if (!*n) return (char*)h;
    for (; *h; h++) {
        const char* a = h; const char* b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return 0;
}
char* strupr(char* s) { for (char* p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; return s; }
char* strlwr(char* s) { for (char* p = s; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32; return s; }

static char  strtok_save_buf[1];
static char* strtok_save = strtok_save_buf;
char* strtok(char* s, const char* sep) {
    if (s) strtok_save = s;
    if (!*strtok_save) return 0;
    /* skip leading sep */
    while (*strtok_save) {
        const char* p = sep;
        int hit = 0;
        while (*p) if (*p++ == *strtok_save) { hit = 1; break; }
        if (!hit) break;
        strtok_save++;
    }
    if (!*strtok_save) return 0;
    char* tok = strtok_save;
    while (*strtok_save) {
        const char* p = sep;
        int hit = 0;
        while (*p) if (*p++ == *strtok_save) { hit = 1; break; }
        if (hit) { *strtok_save++ = 0; return tok; }
        strtok_save++;
    }
    return tok;
}

/* ---- malloc / free / realloc / calloc ------------------------- */

void* malloc(unsigned long n)             { return xmalloc(n); }
void  free(void* p)                       { xfree(p); }
void* calloc(unsigned long n, unsigned long sz) {
    unsigned long total = n * sz;
    void* p = xmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}
void* realloc(void* p, unsigned long n) {
    /* Bump allocator can't really realloc. Just allocate new and copy.
     * Old block leaks; FastDoom rarely reallocs at runtime. */
    if (!p) return xmalloc(n);
    void* q = xmalloc(n);
    if (q) memcpy(q, p, n);   /* may over-read; ok for bump-only heap */
    return q;
}

/* ---- exit / abort -------------------------------------------- */

/* The app entry sets exit_unwind to a longjmp callback that pops out
 * of doomgeneric back to app_main. If unset (no entry hook), we fall
 * through to a halt loop. */
void (*exit_unwind)(int code) = 0;

void exit(int code)  {
    if (exit_unwind) exit_unwind(code);
    bos_puts("\n[exit] no unwind hook -- hanging\n");
    for (;;) { __asm__ __volatile__("cli; hlt"); }
}
void abort(void)     {
    if (exit_unwind) exit_unwind(1);
    bos_puts("\n[abort] no unwind hook -- hanging\n");
    for (;;) { __asm__ __volatile__("cli; hlt"); }
}
int  atexit(void (*fn)(void)) { (void)fn; return 0; }

/* ---- atoi / atol / abs --------------------------------------- */

int atoi(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
long atol(const char* s) { return atoi(s); }
int  abs(int x)  { return x < 0 ? -x : x; }
long labs(long x){ return x < 0 ? -x : x; }

/* ---- env / system stubs -------------------------------------- */

char* getenv(const char* name) { (void)name; return 0; }
int   system(const char* cmd)  { (void)cmd; return -1; }
int errno = 0;

/* ---- rand ----------------------------------------------------- */

static unsigned int rand_state = 1;
int  rand(void)            { rand_state = rand_state * 1103515245u + 12345u;
                             return (int)((rand_state >> 16) & 0x7FFF); }
void srand(unsigned seed)  { rand_state = seed; }

/* ---- qsort (insertion sort — small N is fine for FastDoom) ---- */

void qsort(void* base, unsigned long n, unsigned long sz,
           int (*cmp)(const void*, const void*)) {
    char* a = base;
    char tmp[256];
    if (sz > sizeof(tmp)) return;            /* defensive */
    for (unsigned long i = 1; i < n; i++) {
        memcpy(tmp, a + i*sz, sz);
        unsigned long j = i;
        while (j > 0 && cmp(a + (j-1)*sz, tmp) > 0) {
            memcpy(a + j*sz, a + (j-1)*sz, sz);
            j--;
        }
        memcpy(a + j*sz, tmp, sz);
    }
}

/* ---- printf / sprintf ---------------------------------------- */

static void emit_char(char c, char** out, unsigned long* rem) {
    if (*out && *rem > 1) { *(*out)++ = c; (*rem)--; **out = 0; }
    if (!*out) bos_putc(c);
}

static void emit_str(const char* s, char** out, unsigned long* rem, int width, int leftalign) {
    int len = 0; const char* p = s; while (*p++) len++;
    if (!leftalign) for (int i = len; i < width; i++) emit_char(' ', out, rem);
    while (*s) emit_char(*s++, out, rem);
    if (leftalign)  for (int i = len; i < width; i++) emit_char(' ', out, rem);
}

static void emit_int(long long v, int base, int sign, int width, int zeropad,
                     int upper, char** out, unsigned long* rem) {
    char tmp[32];
    int n = 0;
    int neg = 0;
    unsigned long long uv;
    if (sign && v < 0) { neg = 1; uv = (unsigned long long)(-v); }
    else                uv = (unsigned long long)v;
    if (uv == 0) tmp[n++] = '0';
    while (uv) {
        unsigned long long d = uv % (unsigned)base;
        uv /= (unsigned)base;
        char c = (d < 10) ? '0' + d : (upper ? 'A' : 'a') + d - 10;
        tmp[n++] = c;
    }
    if (neg) tmp[n++] = '-';
    int pad = width - n;
    char padchar = zeropad ? '0' : ' ';
    while (pad-- > 0) emit_char(padchar, out, rem);
    while (n--) emit_char(tmp[n], out, rem);
}

static int vformat(char* outbuf, unsigned long cap, const char* fmt, va_list ap) {
    char* out = outbuf;
    char* start = outbuf;
    unsigned long rem = cap;
    if (out) { if (rem) *out = 0; }

    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit_char(*fmt, &out, &rem); continue; }
        fmt++;
        int leftalign = 0, zeropad = 0, longflag = 0, width = 0;
        int precision = -1;
        while (*fmt == '-' || *fmt == '0' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
            if (*fmt == '-') leftalign = 1;
            if (*fmt == '0') zeropad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt - '0'); fmt++; }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') { precision = precision*10 + (*fmt - '0'); fmt++; }
        }
        if (*fmt == 'l' || *fmt == 'L') { longflag = 1; fmt++; if (*fmt == 'l') fmt++; }
        if (*fmt == 'h') fmt++;
        if (*fmt == 'z') { longflag = 1; fmt++; }

        switch (*fmt) {
            case 'd': case 'i': {
                long long v = longflag ? va_arg(ap, long long) : va_arg(ap, int);
                /* For integer conversions, %.Nd means: pad with zeros to N
                 * digits regardless of `width`/`zeropad` flags. */
                int pad_w = (precision >= 0) ? precision : width;
                int pad_zero = (precision >= 0) ? 1 : zeropad;
                emit_int(v, 10, 1, pad_w, pad_zero, 0, &out, &rem);
                break;
            }
            case 'u': {
                unsigned long long v = longflag ? va_arg(ap, unsigned long long)
                                                 : va_arg(ap, unsigned int);
                int pad_w = (precision >= 0) ? precision : width;
                int pad_zero = (precision >= 0) ? 1 : zeropad;
                emit_int((long long)v, 10, 0, pad_w, pad_zero, 0, &out, &rem);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long v = longflag ? va_arg(ap, unsigned long long)
                                                 : va_arg(ap, unsigned int);
                int pad_w = (precision >= 0) ? precision : width;
                int pad_zero = (precision >= 0) ? 1 : zeropad;
                emit_int((long long)v, 16, 0, pad_w, pad_zero, *fmt == 'X', &out, &rem);
                break;
            }
            case 'p': {
                unsigned long long v = (unsigned long long)(unsigned long)va_arg(ap, void*);
                emit_char('0', &out, &rem); emit_char('x', &out, &rem);
                emit_int((long long)v, 16, 0, 0, 0, 0, &out, &rem);
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);
                emit_char((char)c, &out, &rem);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                emit_str(s, &out, &rem, width, leftalign);
                break;
            }
            case '%': emit_char('%', &out, &rem); break;
            case 0:   fmt--; break;
            default:  emit_char('%', &out, &rem); emit_char(*fmt, &out, &rem); break;
        }
    }
    if (out) *out = 0;
    return out ? (int)(out - start) : 0;
}

int vprintf(const char* fmt, va_list ap) {
    return vformat(0, 0, fmt, ap);
}
int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vformat(0, 0, fmt, ap);
    va_end(ap);
    return r;
}
int vsprintf(char* buf, const char* fmt, va_list ap) {
    return vformat(buf, 0x7FFFFFFF, fmt, ap);
}
int sprintf(char* buf, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vformat(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return r;
}
int snprintf(char* buf, unsigned long n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vformat(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int puts(const char* s)        { while (*s) bos_putc(*s++); bos_putc('\n'); return 0; }
int putchar(int c)             { bos_putc((char)c); return c; }

/* ---- FILE *: a thin handle table over bos_read_file ---------- */
/* FastDoom uses fopen/fread for the WAD and small config files.
 * We only support read-only from the FAT12 root in 8.3 names. */

#define MAX_FILES 16
#define MAX_PATH  64
struct FILE_t {
    int    used;
    unsigned char* data;        /* lazily allocated on first fread */
    unsigned long  size;
    unsigned long  pos;
    char           path[MAX_PATH];
};
static struct FILE_t filetab[MAX_FILES];

typedef struct FILE_t FILE;

/* stdio "stream" sentinels — we only support output as bos_puts. */
static struct FILE_t stdfile_stub;
FILE* const stdin  = &stdfile_stub;
FILE* const stdout = &stdfile_stub;
FILE* const stderr = &stdfile_stub;

/* Lazy-load: open just records path + size. The data buffer is only
 * allocated when something actually reads (or seeks into) the file.
 * This lets DOOM's D_CheckFileSize (which fopens + fseeks-end + ftell)
 * peek at the size without consuming 4 MiB for a 4 MiB WAD. */
static int file_realize(struct FILE_t* f) {
    if (f->data) return 0;
    f->data = xmalloc(f->size);
    if (!f->data) return -1;
    long long n = bos_read_file(f->path, f->data, f->size);
    if (n < 0) { f->data = 0; return -1; }
    return 0;
}

static FILE* open_into_heap(const char* path) {
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) if (!filetab[i].used) { slot = i; break; }
    if (slot < 0) return 0;

    const char* base = path;
    for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;

    long long sz = bos_read_file(base, 0, 0);
    if (sz < 0) return 0;

    /* Stash the path so we can lazy-load on first read. */
    int i = 0;
    while (base[i] && i < MAX_PATH - 1) { filetab[slot].path[i] = base[i]; i++; }
    filetab[slot].path[i] = 0;

    filetab[slot].used = 1;
    filetab[slot].data = 0;                 /* not loaded yet */
    filetab[slot].size = (unsigned long)sz;
    filetab[slot].pos  = 0;
    return &filetab[slot];
}

FILE* fopen(const char* path, const char* mode) {
    /* Reads only. */
    if (mode && mode[0] == 'w') return 0;
    return open_into_heap(path);
}
int fclose(FILE* f) {
    if (!f || f == &stdfile_stub) return 0;
    if (f->data) xfree(f->data);
    f->used = 0; f->data = 0; f->size = 0; f->pos = 0;
    return 0;
}
unsigned long fread(void* buf, unsigned long sz, unsigned long n, FILE* f) {
    if (!f || f == &stdfile_stub) return 0;
    if (file_realize(f) != 0) return 0;
    unsigned long want = sz * n;
    unsigned long avail = f->size - f->pos;
    if (want > avail) want = avail;
    memcpy(buf, f->data + f->pos, want);
    f->pos += want;
    return sz ? want / sz : 0;
}
unsigned long fwrite(const void* buf, unsigned long sz, unsigned long n, FILE* f) {
    /* No write support yet. Fake success for stdout/stderr. */
    if (f == &stdfile_stub) {
        const char* s = buf; for (unsigned long i = 0; i < sz*n; i++) bos_putc(s[i]);
        return n;
    }
    (void)buf; (void)sz; (void)n; return 0;
}
int fseek(FILE* f, long off, int whence) {
    if (!f || f == &stdfile_stub) return -1;
    long target;
    if      (whence == 0) target = off;                    /* SEEK_SET */
    else if (whence == 1) target = (long)f->pos + off;     /* SEEK_CUR */
    else                  target = (long)f->size + off;    /* SEEK_END */
    if (target < 0) target = 0;
    if ((unsigned long)target > f->size) target = (long)f->size;
    f->pos = (unsigned long)target;
    return 0;
}
long ftell(FILE* f)            { return (f && f != &stdfile_stub) ? (long)f->pos : -1; }
int  feof(FILE* f)             { return f && f != &stdfile_stub && f->pos >= f->size; }
int  fgetc(FILE* f) {
    if (!f || f == &stdfile_stub || f->pos >= f->size) return -1;
    if (file_realize(f) != 0) return -1;
    return f->data[f->pos++];
}
char* fgets(char* s, int n, FILE* f) {
    if (!f || f == &stdfile_stub || n <= 0) return 0;
    if (file_realize(f) != 0) return 0;
    int i = 0;
    while (i < n - 1 && f->pos < f->size) {
        char c = (char)f->data[f->pos++];
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}
int fputc(int c, FILE* f) {
    if (f == &stdfile_stub) { bos_putc((char)c); return c; }
    return -1;
}
int fputs(const char* s, FILE* f) {
    if (f == &stdfile_stub) { while (*s) bos_putc(*s++); return 0; }
    return -1;
}
int  fflush(FILE* f)           { (void)f; return 0; }
int  ferror(FILE* f)           { (void)f; return 0; }
void clearerr(FILE* f)         { (void)f; }
int  setvbuf(FILE* f, char* b, int m, unsigned long s) { (void)f; (void)b; (void)m; (void)s; return 0; }
int  rename(const char* a, const char* b) { (void)a; (void)b; return -1; }
int  remove(const char* p)               { (void)p; return -1; }

int fprintf(FILE* f, const char* fmt, ...) {
    /* Only stdout/stderr direction supported. */
    char tmp[512];
    va_list ap; va_start(ap, fmt);
    int n = vformat(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (f == &stdfile_stub) { for (int i = 0; i < n; i++) bos_putc(tmp[i]); }
    return n;
}

/* ---- Watcom-style direct file I/O (open/read/lseek/...) ------ */
/* FastDoom uses these in i_file.c. Implement as a thin layer over
 * the same handle table; FILE* and int fd map 1:1 by slot index. */

int open(const char* path, int flags, ...) {
    (void)flags;
    FILE* f = open_into_heap(path);
    if (!f) return -1;
    return (int)(f - filetab);
}
int close(int fd) {
    if (fd < 0 || fd >= MAX_FILES) return -1;
    return fclose(&filetab[fd]);
}
long read(int fd, void* buf, unsigned long n) {
    if (fd < 0 || fd >= MAX_FILES || !filetab[fd].used) return -1;
    return (long)fread(buf, 1, n, &filetab[fd]);
}
long write(int fd, const void* buf, unsigned long n) {
    (void)fd; (void)buf; return (long)n;     /* writes silently succeed */
}
long lseek(int fd, long off, int whence) {
    if (fd < 0 || fd >= MAX_FILES || !filetab[fd].used) return -1;
    fseek(&filetab[fd], off, whence);
    return (long)filetab[fd].pos;
}
int filelength(int fd) {
    if (fd < 0 || fd >= MAX_FILES || !filetab[fd].used) return -1;
    return (int)filetab[fd].size;
}
int access(const char* path, int mode) {
    (void)mode;
    /* Probe-only mode: bos_read_file with buf=NULL returns the file
     * size if it exists, or -1 if not. No allocation, no read. */
    const char* base = path;
    for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    return (bos_read_file(base, 0, 0) >= 0) ? 0 : -1;
}

/* ---- time stubs --------------------------------------------- */

long time(long* t)   { long v = (long)(ticks_ms() / 1000); if (t) *t = v; return v; }
long clock(void)     { return (long)ticks_ms(); }

/* sscanf stub: declared in boxos_compat.h so dead bench code links. */
int sscanf(const char* s, const char* fmt, ...) { (void)s; (void)fmt; return 0; }

/* strdup: copy a string into freshly-malloc'd memory. */
char* strdup(const char* s) {
    if (!s) return 0;
    unsigned long n = strlen(s);
    char* out = (char*)xmalloc(n + 1);
    if (!out) return 0;
    memcpy(out, s, n + 1);
    return out;
}

int vfprintf(void* f, const char* fmt, va_list ap) {
    (void)f;
    return vformat(0, 0, fmt, ap);
}
int vsnprintf(char* buf, unsigned long n, const char* fmt, va_list ap) {
    return vformat(buf, n, fmt, ap);
}

/* fabs is only called from v_video.c gamma init. SSE/x87 disabled
 * makes float returns impossible, so we expose it as int and the
 * caller's float context implicitly converts (also broken w/o SSE,
 * so we patch the caller instead). Provide a placeholder symbol via
 * inline asm so the linker is happy. */
__asm__(".global fabs\nfabs:\n  ret\n");
