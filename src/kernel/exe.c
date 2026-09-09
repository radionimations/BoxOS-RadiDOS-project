/* Read-only EXE format inspector.
 *
 * BoxOS apps are flat 64-bit binaries that we run directly via the
 * loader. Real Windows / DOS .EXE files are layered formats:
 *
 *   - DOS MZ:    "MZ" at offset 0, 16-bit real-mode code.
 *   - Win 1.0/3.x NE: "MZ" stub, plus "NE" header at offset
 *     [0x3C]; 16-bit segmented Windows code.
 *   - Win 9x/NT PE: "MZ" stub, plus "PE\0\0" header at offset
 *     [0x3C]; 32-/64-bit code.
 *   - LE / LX:   OS/2 + Win 9x VxD; left for completeness.
 *
 * For now we just *describe* the file. Actually executing any of
 * these requires either an 8086 emulator (MZ/NE), a Windows 1.x
 * runtime (NE + USER/KERNEL/GDI shims), or a Win32 subsystem (PE).
 * The on-disk header parse is the foundation we'll grow on top of. */

#include "boxos.h"

#define EXE_HEADER_BYTES 1024

/* Parsed values; out fields are 0 / "" if not applicable. */
struct exe_info {
    int      kind;              /* see EXE_KIND_* below                */
    uint32_t hdr_offset;        /* offset of the secondary (NE/PE) hdr */
    uint32_t image_size;        /* estimated image size in bytes       */
    uint16_t mz_cs;             /* relocation CS                        */
    uint16_t mz_ip;             /* entry IP                             */
    uint16_t pe_machine;        /* IMAGE_FILE_MACHINE_*                 */
    uint16_t pe_subsystem;      /* IMAGE_SUBSYSTEM_*                    */
    uint16_t pe_dll;            /* characteristics: IMAGE_FILE_DLL set? */
    uint32_t pe_entry_rva;      /* optional-header entry point (RVA)    */
    uint16_t ne_target_os;      /* NE: 0x02 = Win, 0x01 = OS/2          */
    uint16_t ne_n_segments;     /* NE: number of segments               */
};

#define EXE_KIND_NONE  0
#define EXE_KIND_MZ    1   /* plain DOS .EXE                            */
#define EXE_KIND_NE    2   /* New Executable (Win 1.x..3.x, OS/2 1.x)   */
#define EXE_KIND_PE    3   /* Portable Executable (Win 9x+, NT, etc.)   */
#define EXE_KIND_LE    4   /* Linear Executable (DOS/4GW, VxDs)         */
#define EXE_KIND_LX    5   /* Linear Executable extended (OS/2 2.x+)    */

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Inspect the EXE at `name` in the current app cwd. Returns 0 on
 * success (out filled in), -1 if the file doesn't exist or isn't
 * recognisable as any EXE format we know about. */
int exe_inspect(const char* name, struct exe_info* out) {
    if (!out) return -1;
    /* Zero so callers can format unset fields safely. */
    for (uint64_t i = 0; i < sizeof(*out); i++)
        ((uint8_t*)out)[i] = 0;

    static uint8_t hdr[EXE_HEADER_BYTES];
    int sz = fs_read_in(fs_get_app_cwd(), name, hdr, EXE_HEADER_BYTES);
    if (sz < 0) return -1;
    if (sz < 0x40) return -1;

    /* MZ (or rare ZM) signature — every supported format starts with one. */
    if (!((hdr[0] == 'M' && hdr[1] == 'Z') ||
          (hdr[0] == 'Z' && hdr[1] == 'M'))) return -1;

    out->kind        = EXE_KIND_MZ;
    out->mz_ip       = rd16(hdr + 0x14);
    out->mz_cs       = rd16(hdr + 0x16);
    /* MZ image size: e_cp pages of 512 bytes minus what's not in the
     * last page (e_cblp), per the original DOS exe layout. */
    uint16_t e_cp    = rd16(hdr + 0x04);
    uint16_t e_cblp  = rd16(hdr + 0x02);
    uint32_t mz_size = (uint32_t)e_cp * 512;
    if (e_cblp) mz_size -= (512 - e_cblp);
    out->image_size  = mz_size;

    /* Look for an extended header. e_lfanew (offset 0x3C) only exists
     * on extended MZ — older DOS exes have headers shorter than that.
     * The MZ "header size" e_cparhdr (paragraphs of 16 bytes) at
     * offset 0x08 tells us if we're at least 0x40 bytes long. */
    uint16_t e_cparhdr = rd16(hdr + 0x08);
    if ((uint32_t)e_cparhdr * 16 < 0x40) return 0;   /* plain MZ        */

    uint32_t lfanew = rd32(hdr + 0x3C);
    if (lfanew < 0x40 || lfanew + 4 > (uint32_t)sz) return 0;

    const uint8_t* ext = hdr + lfanew;
    out->hdr_offset    = lfanew;

    if (ext[0] == 'N' && ext[1] == 'E') {
        out->kind = EXE_KIND_NE;
        if (lfanew + 0x40 <= (uint32_t)sz) {
            out->ne_n_segments = rd16(hdr + lfanew + 0x1C);
            out->ne_target_os  = (uint16_t)hdr[lfanew + 0x36];
        }
    } else if (ext[0] == 'P' && ext[1] == 'E' && ext[2] == 0 && ext[3] == 0) {
        out->kind = EXE_KIND_PE;
        /* PE\0\0 + 20-byte COFF file header + optional header.
         *   off+4   Machine     (u16)
         *   off+22  Characteristics (u16)
         *   off+24  Magic       (u16)  0x10b PE32, 0x20b PE32+
         *   off+40  AddressOfEntryPoint (RVA)  -- offset 16 inside opt hdr
         *   off+92  Subsystem   (u16) -- offset 68 inside opt hdr
         */
        if (lfanew + 24 + 70 <= (uint32_t)sz) {
            out->pe_machine    = rd16(hdr + lfanew + 4);
            out->pe_dll        = (uint16_t)((rd16(hdr + lfanew + 22) >> 13) & 1);
            uint32_t opt_off   = lfanew + 24;
            out->pe_entry_rva  = rd32(hdr + opt_off + 16);
            out->pe_subsystem  = rd16(hdr + opt_off + 68);
        }
    } else if (ext[0] == 'L' && ext[1] == 'E') {
        out->kind = EXE_KIND_LE;
    } else if (ext[0] == 'L' && ext[1] == 'X') {
        out->kind = EXE_KIND_LX;
    }
    /* else: unknown extension, leave as MZ. */
    return 0;
}

/* Human label. */
const char* exe_kind_label(const struct exe_info* info) {
    switch (info->kind) {
        case EXE_KIND_MZ:  return "DOS executable (MZ, 16-bit real mode)";
        case EXE_KIND_NE:  return "New Executable (Windows 1.x-3.x / OS/2 1.x, 16-bit)";
        case EXE_KIND_PE:  return "Portable Executable (Win 9x / NT / 2000+, 32 or 64-bit)";
        case EXE_KIND_LE:  return "Linear Executable (DOS/4GW, VxD)";
        case EXE_KIND_LX:  return "Linear Executable eXtended (OS/2 2.x+)";
        default:           return "Not a recognised EXE";
    }
}

const char* exe_pe_machine_label(uint16_t m) {
    switch (m) {
        case 0x014C: return "i386 (32-bit x86)";
        case 0x0200: return "Itanium";
        case 0x8664: return "x86_64 (AMD64)";
        case 0x01C0: return "ARM";
        case 0xAA64: return "ARM64";
        default:     return "unknown";
    }
}

const char* exe_pe_subsystem_label(uint16_t s) {
    switch (s) {
        case 1: return "Native (driver)";
        case 2: return "Windows GUI";
        case 3: return "Windows Console";
        case 5: return "OS/2 Console";
        case 7: return "POSIX Console";
        case 9: return "Windows CE GUI";
        default: return "unknown";
    }
}

const char* exe_runnable_message(const struct exe_info* info) {
    /* For now BoxOS can't execute any of these directly. Future: MZ
     * via an 8086 emulator, NE via a Windows-1.x runtime, PE via a
     * Win32 subsystem. */
    switch (info->kind) {
        case EXE_KIND_MZ:
            return "BoxOS 2.0 cannot run DOS .EXE files yet -- needs an "
                   "8086 emulator. Planned for a later release.";
        case EXE_KIND_NE:
            return "BoxOS 2.0 cannot run NE (Win 1.x-3.x) .EXE files yet "
                   "-- needs a 16-bit segmented runtime + USER/KERNEL/GDI "
                   "shims. Planned for 1.x.";
        case EXE_KIND_PE:
            return "BoxOS 2.0 cannot run PE (Win 9x/NT) .EXE files yet "
                   "-- needs a Win32 loader + DLL emulation. Planned for "
                   "BoxOS 3.0.";
        case EXE_KIND_LE:
        case EXE_KIND_LX:
            return "Linear Executable (DOS extender / OS/2). Not on the "
                   "BoxOS roadmap.";
        default:
            return "Not an executable BoxOS recognises.";
    }
}
