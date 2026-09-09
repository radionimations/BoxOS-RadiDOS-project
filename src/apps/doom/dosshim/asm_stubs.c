/* C replacements / stubs for everything FastDoom expected from its
 * 60+ hand-written .asm files. We split them into:
 *
 *   1. Real C: fast-int math, memory copy/set, fixed-point math.
 *      These have correct behavior — same answer the asm gave.
 *
 *   2. Stubs: sound, CPU probes, hardware port I/O, the asm-only
 *      renderer fast paths. These return zero / do nothing. The
 *      engine will *link* and *start*; full visual correctness is
 *      a later session's problem.
 *
 * If a renderer stub is hit at runtime the screen will look weird
 * but the game logic runs fine. */

#include "boxos_app.h"

typedef int   fixed_t;          /* doomtype.h's fixed-point type */
typedef int   int32;
typedef unsigned char byte;

/* =============== 1. fast int math (correct) ===================== */

int Mul10(int x)     { return x * 10; }
int Mul20(int x)     { return x * 20; }
int Mul25(int x)     { return x * 25; }
int Mul75(int x)     { return x * 75; }
int Mul80(int x)     { return x * 80; }
int Mul100(int x)    { return x * 100; }
int Mul160(int x)    { return x * 160; }
int Mul175(int x)    { return x * 175; }
int Mul320(int x)    { return x * 320; }
int Mul409(int x)    { return x * 409; }
int Mul768(int x)    { return x * 768; }
int Mul47000(int x)  { return x * 47000; }
int Mul819200(int x) { return x * 819200; }

int Div3(int x)      { return x / 3; }
int Div10(int x)     { return x / 10; }
int Div35(int x)     { return x / 35; }
int Div100(int x)    { return x / 100; }
int Div101(int x)    { return x / 101; }
int Div128(int x)    { return x >> 7; }
int Div1000(int x)   { return x / 1000; }
int DivSKULLSPEED(int x) { return x / 20; }   /* SKULLSPEED is 20 */

/* DOOM fixed-point: 16.16. FixedMul/Div use 64-bit intermediate. */
#define FRACBITS 16

fixed_t FixedMul(fixed_t a, fixed_t b) {
    return (fixed_t)(((long long)a * (long long)b) >> FRACBITS);
}
fixed_t FixedMulEDX(fixed_t a, fixed_t b)        { return FixedMul(a, b); }
fixed_t FixedMulSquare(fixed_t a)                 { return FixedMul(a, a); }
fixed_t FixedMulShortToInt(fixed_t a, fixed_t b)  { return FixedMul(a, b); }
fixed_t FixedMulHStep(fixed_t a, fixed_t b)       { return FixedMul(a, b); }
fixed_t FixedMulLStep(fixed_t a, fixed_t b)       { return FixedMul(a, b); }

fixed_t FixedDiv2(fixed_t a, fixed_t b) {
    if ((unsigned)((a < 0 ? -a : a) >> 14) >= (unsigned)(b < 0 ? -b : b))
        return (a ^ b) < 0 ? 0x80000000 : 0x7FFFFFFF;
    return (fixed_t)(((long long)a << FRACBITS) / b);
}
fixed_t FixedDiv65536(fixed_t a, fixed_t b)  { return FixedDiv2(a, b); }
fixed_t FixedDivDBITS(fixed_t a, fixed_t b)  { return FixedDiv2(a, b); }

/* =============== 2. block memory ops (correct) ================== */
/* FastDoom convention is (src, dst, n) — matches DOS rep-movsd asm
 * where rsi=src, rdi=dst before the copy. */

void CopyBytes (const void* src, void* dst, int n) { memcpy(dst, src, (unsigned)n); }
void CopyWords (const void* src, void* dst, int n) { memcpy(dst, src, (unsigned)n * 2); }
void CopyDWords(const void* src, void* dst, int n) { memcpy(dst, src, (unsigned)n * 4); }
void SetBytes  (void* dst, int v, int n)            { memset(dst, v, (unsigned)n); }
void SetWords  (void* dst, int v, int n) {
    unsigned short* p = dst; while (n--) *p++ = (unsigned short)v;
}
void SetDWords (void* dst, int v, int n) {
    unsigned* p = dst; while (n--) *p++ = (unsigned)v;
}

/* =============== 3. string ops (correct alias) ================== */

int    stricmp (const char*, const char*);                  /* in libc.c */
int    strcmpi(const char* a, const char* b)            { return stricmp(a, b); }

unsigned long strtoul(const char* s, char** ep, int base) {
    while (*s == ' ' || *s == '\t') s++;
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0') base = 8;
        else                base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    unsigned long v = 0;
    while (*s) {
        int d;
        if      (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (ep) *ep = (char*)s;
    return v;
}

int fscanf(void* f, const char* fmt, ...) { (void)f; (void)fmt; return 0; }

/* =============== 4. CPU / DOS probes (stubs) ==================== */

unsigned GetCPUFeatures(void)               { return 0; }
unsigned GetCPUID(void)                     { return 0x386; }
void     DPMI_LockMemory(void* p, unsigned n) { (void)p; (void)n; }
void*    _dos_getvect(int n)                { (void)n; return 0; }
void     _dos_setvect(int n, void* p)       { (void)n; (void)p; }
void     segread(void* s)                   { (void)s; }
int      ___Argc;
char     __begtext[1];

/* =============== 5. hardware port I/O (stubs) =================== */

unsigned char InByte60h(void)               { return 0; }
unsigned char InByte61h(void)               { return 0; }
void          OutByte20h(int v)             { (void)v; }
void          OutByte61h(int v)             { (void)v; }
void          FastPaletteOut(const void* p) { (void)p; }
void          I_WaitSingleVBL(void)         { /* gfx_blit syncs implicitly */ }
void          I_CopyLine386(void* d, const void* s, int n) { memcpy(d, s, n); }
void          I_CopyLine486(void* d, const void* s, int n) { memcpy(d, s, n); }
void          CMS_SetMode(int m)            { (void)m; }
void          SetMUSPort(int p)             { (void)p; }
void          SetSNDPort(int p)             { (void)p; }
void          AL_SetCard(int c)             { (void)c; }
unsigned char vrambuffer[64000];   /* mode 13h backbuffer */

/* =============== 6. sound subsystem (all stubs) ================= */

int  ASS_Init(int a, int b, int c)           { (void)a;(void)b;(void)c; return 0; }
void ASS_DeInit(void)                        {}
int  SB_Detect(int* a, int* b, int* c)       { (void)a;(void)b;(void)c; return 0; }
int  ENS_Detect(int* a, int* b, int* c)      { (void)a;(void)b;(void)c; return 0; }
int  MPU_Detect(int* a, int* b)              { (void)a;(void)b; return 0; }

void  CD_Init(int n)                         { (void)n; }
void  CD_Exit(void)                          {}
int   CD_Cdrom_data(int n)                   { (void)n; return 0; }
int   CD_GetAudioStatus(void)                { return 0; }
void  CD_Lock(int n)                         { (void)n; }
void  CD_PlayAudio(int a, int b)             { (void)a; (void)b; }
void  CD_ResumeAudio(void)                   {}
void  CD_SetVolume(int v)                    { (void)v; }
void  CD_StopAudio(void)                     {}

int   MUSIC_Continue(void)                   { return 0; }
int   MUSIC_Pause(void)                      { return 0; }
int   MUSIC_SetVolume(int v)                 { (void)v; return 0; }
int   MUSIC_StopSong(void)                   { return 0; }

int   MUS_ChainSong(int a, int b)            { (void)a;(void)b; return 0; }
void  MUS_ImgSC55(void)                      {}
void  MUS_ImgTG300(void)                     {}
void  MUS_LoadMT32(void)                     {}
int   MUS_PlaySong(void* a, int b)           { (void)a;(void)b; return 0; }
int   MUS_RegisterSong(void* a)              { (void)a; return 0; }
void  MUS_ReleaseData(void* a)               { (void)a; }
int   MUS_SongPlaying(void)                  { return 0; }
void  MUS_TextMT32(void)                     {}
void  MUS_TextMU80(void)                     {}
void  MUS_TextSC55(void)                     {}
void  MUS_TextTG300(void)                    {}
void  MUS_YamahaXG(void)                     {}

int   MV_GetVoice(int n)                     { (void)n; return 0; }
void  MV_Kill(int n)                         { (void)n; }
int   MV_PlayRaw(void* a, int b, int c, int d, int e, int f, int g) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; return 0;
}
void  MV_SetVoiceVolume(int a, int b, int c) { (void)a;(void)b;(void)c; }
int   MV_VoicePlaying(int n)                 { (void)n; return 0; }

void  AL_SetMusicCard(int c)                 { (void)c; }
void  GF1_SetMap(void* p)                    { (void)p; }
void  ROLAND1(void)                          {}

/* DMX (FastDoom's sound facade) — most are stubbed inside i_sound.c
 * via #ifdefs but a few escape: */
int   SFX_PlayPatch(void* p, int v, int pan, int pri, int loop, int reserved) {
    (void)p;(void)v;(void)pan;(void)pri;(void)loop;(void)reserved; return 0;
}
int   SFX_Playing(int handle)                { (void)handle; return 0; }
void  SFX_SetOrigin(int handle, int v, int pan) { (void)handle;(void)v;(void)pan; }
void  SFX_StopPatch(int handle)              { (void)handle; }

/* Task scheduler used by sound timers. */
int   TS_ScheduleTask(void (*fn)(void), int rate, int prio, void* p) {
    (void)fn;(void)rate;(void)prio;(void)p; return 0;
}
void  TS_Dispatch(void)                      {}
void  TS_Shutdown(void)                      {}
void  TS_Terminate(int n)                    { (void)n; }

int   TrackBeginPosition(void* a)            { (void)a; return 0; }
int   TrackLength(void* a)                   { (void)a; return 0; }

/* =============== 7. renderer fast paths (portable C) ============ */
/* Real implementations of the inner pixel loops. Replaces the
 * no-op stubs. Walls get textured columns; floors/ceilings get
 * flat-colored spans (sample one center pixel of the 64x64 flat
 * texture, fill the row). Fuzz columns use the classic "draw black
 * every other row" effect. Low/Potato modes alias to the same
 * implementation; visually identical, slightly slower than ideal
 * but correct.
 *
 * Globals (defined elsewhere in DOOM source):
 *   dc_yl, dc_yh, dc_x, dc_iscale, dc_texturemid, dc_source,
 *   dc_colormap, centery, ylookup[], columnofs[],
 *   ds_y, ds_x1, ds_x2, ds_source, ds_colormap. */

#define R_FRACBITS  16
#define R_SCRWIDTH  320

extern unsigned char* dc_colormap;
extern int            dc_x, dc_yl, dc_yh;
extern int            dc_iscale, dc_texturemid;
extern unsigned char* dc_source;
extern int            centery;
extern unsigned char* ylookup[];
extern int            columnofs[];

extern int            ds_y, ds_x1, ds_x2;
extern unsigned char* ds_colormap;
extern unsigned char* ds_source;

#ifndef FLATPIXELCOLOR
/* Standard "average" pixel of a 64x64 flat: middle-ish. */
#define FLATPIXELCOLOR (32 * 64 + 32)
#endif

/* --- standard textured wall column ----------------------------- */
void R_DrawColumnBackbuffer(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    int fracstep = dc_iscale;
    int frac     = dc_texturemid + (dc_yl - centery) * fracstep;
    do {
        *dest = dc_colormap[dc_source[(frac >> R_FRACBITS) & 127]];
        dest += R_SCRWIDTH;
        frac += fracstep;
    } while (count--);
}

/* Sky column: no colormap (full bright). */
void R_DrawColumnBackbufferSkyFullDirect(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    int fracstep = dc_iscale;
    int frac     = dc_texturemid + (dc_yl - centery) * fracstep;
    do {
        *dest = dc_source[(frac >> R_FRACBITS) & 127];
        dest += R_SCRWIDTH;
        frac += fracstep;
    } while (count--);
}

/* Low-detail column: each pixel rendered as 2x1 (covers two adjacent
 * x-positions). columnofs[dc_x] takes care of the doubled stride. */
void R_DrawColumnLowBackbuffer(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    int fracstep = dc_iscale;
    int frac     = dc_texturemid + (dc_yl - centery) * fracstep;
    do {
        unsigned char c = dc_colormap[dc_source[(frac >> R_FRACBITS) & 127]];
        dest[0] = c; dest[1] = c;
        dest += R_SCRWIDTH;
        frac += fracstep;
    } while (count--);
}

void R_DrawColumnLowBackbufferSkyFullDirect(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    int fracstep = dc_iscale;
    int frac     = dc_texturemid + (dc_yl - centery) * fracstep;
    do {
        unsigned char c = dc_source[(frac >> R_FRACBITS) & 127];
        dest[0] = c; dest[1] = c;
        dest += R_SCRWIDTH;
        frac += fracstep;
    } while (count--);
}

/* Flat (single-color) column — used for some debug or texture-less
 * surfaces. Sample the texture mid-point. */
void R_DrawColumnBackbufferFlat(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    unsigned char c = dc_colormap[dc_source[64]];
    do { *dest = c; dest += R_SCRWIDTH; } while (count--);
}
void R_DrawColumnLowBackbufferFlat(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    unsigned char c = dc_colormap[dc_source[64]];
    do { dest[0] = c; dest[1] = c; dest += R_SCRWIDTH; } while (count--);
}

/* Aliases — call into the canonical variants. */
void R_DrawColumnBackbufferDirect(void)             { R_DrawColumnBackbuffer(); }
void R_DrawColumnBackbufferFastLEA(void)            { R_DrawColumnBackbuffer(); }
void R_DrawColumnBackbufferMMX(void)                { R_DrawColumnBackbuffer(); }
void R_DrawColumnBackbufferRoll(void)               { R_DrawColumnBackbuffer(); }
void R_DrawColumnLowBackbufferDirect(void)          { R_DrawColumnLowBackbuffer(); }
void R_DrawColumnLowBackbufferFastLEA(void)         { R_DrawColumnLowBackbuffer(); }
void R_DrawColumnPotatoBackbuffer(void)             { R_DrawColumnLowBackbuffer(); }
void R_DrawColumnPotatoBackbufferDirect(void)       { R_DrawColumnLowBackbuffer(); }
void R_DrawColumnPotatoBackbufferFlat(void)         { R_DrawColumnLowBackbufferFlat(); }
void R_DrawColumnPotatoBackbufferSkyFullDirect(void){ R_DrawColumnLowBackbufferSkyFullDirect(); }

/* --- fuzz (spectre) column: write black on alternating rows ----- */
void R_DrawFuzzColumnBackbuffer(void) {
    int count = dc_yh - dc_yl;
    if (count < 0) return;
    unsigned char* dest = ylookup[dc_yl] + columnofs[dc_x];
    int parity = (dc_yl + dc_x) & 1;
    int y = 0;
    do {
        if (((y + parity) & 1) == 0) *dest = 0;
        dest += R_SCRWIDTH;
        y++;
    } while (count--);
}
void R_DrawFuzzColumnFlatBackbuffer(void)        { R_DrawFuzzColumnBackbuffer(); }
void R_DrawFuzzColumnLowBackbuffer(void)         { R_DrawFuzzColumnBackbuffer(); }
void R_DrawFuzzColumnFlatLowBackbuffer(void)     { R_DrawFuzzColumnBackbuffer(); }
void R_DrawFuzzColumnPotatoBackbuffer(void)      { R_DrawFuzzColumnBackbuffer(); }
void R_DrawFuzzColumnFlatPotatoBackbuffer(void)  { R_DrawFuzzColumnBackbuffer(); }

/* --- spans (floors/ceilings): flat-color the whole row using the
 * 64x64 flat texture's center pixel and the current colormap.
 * Trades visual fidelity (no perspective-textured floors) for
 * correctness; everything else in the world view is real. */
void R_DrawSpanBackbuffer(void) {
    int count = ds_x2 - ds_x1 + 1;
    if (count <= 0) return;
    unsigned char c = ds_colormap[ds_source[FLATPIXELCOLOR]];
    unsigned char* dest = ylookup[ds_y] + columnofs[ds_x1];
    while (count--) *dest++ = c;
}
void R_DrawSpanBackbufferMMX(void)            { R_DrawSpanBackbuffer(); }
void R_DrawSpanBackbufferPentium(void)        { R_DrawSpanBackbuffer(); }
void R_DrawSpanBackbufferRoll(void)           { R_DrawSpanBackbuffer(); }
void R_DrawSpanLowBackbuffer(void)            { R_DrawSpanBackbuffer(); }
void R_DrawSpanLowBackbufferPentium(void)     { R_DrawSpanBackbuffer(); }
void R_DrawSpanPotatoBackbuffer(void)         { R_DrawSpanBackbuffer(); }
void R_DrawSpanPotatoBackbufferPentium(void)  { R_DrawSpanBackbuffer(); }

/* "Patch" functions — runtime self-modifying code in FastDoom's
 * renderer that overwrites immediates inside the asm loops. With
 * the asm gone, these patches have nothing to patch. No-op them. */
void R_PatchCenteryLinearDirect(int v)        { (void)v; }
void R_PatchCenteryLinearHighKN(int v)        { (void)v; }
void R_PatchCenteryLinearLowDirect(int v)     { (void)v; }
void R_PatchCenteryLinearLowKN(int v)         { (void)v; }
void R_PatchCenteryLinearPotatoDirect(int v)  { (void)v; }
void R_PatchColumnofsHighPentium(int v)       { (void)v; }
void R_PatchColumnofsLowPentium(int v)        { (void)v; }
void R_PatchColumnofsPotatoPentium(int v)     { (void)v; }
void R_PatchFuzzColumnLinearHigh(int v)       { (void)v; }
void R_PatchFuzzColumnLinearLow(int v)        { (void)v; }
void R_PatchFuzzColumnLinearPotato(int v)     { (void)v; }
void R_PatchLinearHigh(int v)                 { (void)v; }
void R_PatchLinearLow(int v)                  { (void)v; }
void R_PatchLinearPotato(int v)               { (void)v; }
