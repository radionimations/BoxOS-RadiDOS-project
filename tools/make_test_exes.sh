#!/bin/sh
# Generate three minimal but plausible .EXE files to exercise the
# BoxOS EXE inspector. None of them contain real code -- just enough
# header structure that exe_inspect() reports the correct format.
#
# Output: $1 = directory to write into.

set -e
out="${1:-build/test_exes}"
mkdir -p "$out"

# --- helpers --------------------------------------------------------
# le16 / le32: emit little-endian 2/4-byte integers as raw bytes.
le16 () { printf '%b' "$(printf '\\%03o\\%03o' \
  $(($1 & 0xFF)) $((($1 >> 8) & 0xFF)))"; }
le32 () { printf '%b' "$(printf '\\%03o\\%03o\\%03o\\%03o' \
  $(($1 & 0xFF)) $((($1 >> 8) & 0xFF)) \
  $((($1 >> 16) & 0xFF)) $((($1 >> 24) & 0xFF)))"; }
zeroes () { dd if=/dev/zero bs=1 count="$1" 2>/dev/null; }

# --- 1) plain DOS MZ ------------------------------------------------
{
  printf 'MZ'                        # signature
  le16 128                           # e_cblp:    bytes in last page
  le16 1                             # e_cp:      pages of 512 (so 512 bytes total)
  le16 0                             # e_crlc
  le16 4                             # e_cparhdr: 64-byte header (4 paragraphs)
  le16 16                            # e_minalloc
  le16 0xFFFF                        # e_maxalloc
  le16 0                             # e_ss
  le16 0x0100                        # e_sp
  le16 0                             # e_csum
  le16 0x0000                        # e_ip      (entry IP = 0)
  le16 0x0000                        # e_cs      (entry CS = 0)
  le16 0x0040                        # e_lfarlc
  le16 0                             # e_ovno
  zeroes 32                          # reserved + e_oemid + e_oeminfo
  zeroes 0                           # spacer
  # Header is exactly 64 bytes here, no extended header pointer.
  zeroes 448                         # pad up to 512 (one page) of "code"
} > "$out/HELLODOS.EXE"

# --- 2) Win 1.x / 3.x NE (MZ stub + NE header at 0x40) --------------
{
  # MZ stub identical layout, but with e_lfanew set.
  printf 'MZ'
  le16 128                           # e_cblp
  le16 2                             # e_cp     (1024 bytes total)
  le16 0                             # e_crlc
  le16 4                             # e_cparhdr (64-byte header)
  le16 16
  le16 0xFFFF
  le16 0
  le16 0x0100
  le16 0
  le16 0x0000
  le16 0x0000
  le16 0x0040
  le16 0
  zeroes 32                          # reserved
  zeroes 24                          # padding to offset 0x3C
  le32 0x0040                        # e_lfanew = 0x40
  # NE header at 0x40
  printf 'NE'                        # signature
  printf '\005'                      # linker version
  printf '\025'                      # linker revision
  le16 0                             # entry table offset
  le16 0                             # entry table length
  le32 0                             # CRC
  le16 0x8002                        # flags (multiple data, etc.)
  le16 0                             # auto data segment
  le16 0                             # heap size
  le16 0                             # stack size
  le32 0                             # CS:IP entry
  le32 0                             # SS:SP
  le16 1                             # number of segments
  le16 0                             # number of module references
  le16 0                             # non-resident name table size
  le16 0                             # segment table offset
  le16 0                             # resource table offset
  le16 0                             # resident name table offset
  le16 0                             # module reference table offset
  le16 0                             # imported names table offset
  le32 0                             # non-resident names offset
  le16 0                             # number of movable entries
  le16 4                             # logical sector alignment shift
  le16 0                             # number of resource segments
  printf '\002'                      # target OS = Windows
  zeroes 9                           # rest of NE header
  zeroes 384                         # tail padding
} > "$out/WIN1HELL.EXE"

# --- 3) Win32 PE (MZ stub + PE\0\0 + COFF + PE32 optional hdr) ------
{
  printf 'MZ'
  le16 128
  le16 1
  le16 0
  le16 4
  le16 16
  le16 0xFFFF
  le16 0
  le16 0x0100
  le16 0
  le16 0x0000
  le16 0x0000
  le16 0x0040
  le16 0
  zeroes 32
  zeroes 24                          # pad to 0x3C
  le32 0x0080                        # e_lfanew = 0x80
  zeroes 64                          # 0x40..0x7F: stub
  # PE header at 0x80
  printf 'PE\000\000'
  le16 0x014C                        # Machine = i386
  le16 1                             # NumberOfSections
  le32 0                             # TimeDateStamp
  le32 0                             # PointerToSymbolTable
  le32 0                             # NumberOfSymbols
  le16 224                           # SizeOfOptionalHeader
  le16 0x010F                        # Characteristics: EXE + 32-bit
  # Optional header (PE32, magic 0x10b)
  le16 0x010B                        # Magic
  printf '\001\000'                  # MajorLinker, MinorLinker
  le32 0x1000                        # SizeOfCode
  le32 0                             # SizeOfInitializedData
  le32 0                             # SizeOfUninitializedData
  le32 0x1000                        # AddressOfEntryPoint (RVA)
  le32 0x1000                        # BaseOfCode
  le32 0x2000                        # BaseOfData
  le32 0x00400000                    # ImageBase
  le32 0x1000                        # SectionAlignment
  le32 0x0200                        # FileAlignment
  le16 4                             # MajorOSVersion
  le16 0
  le16 0
  le16 0
  le16 4                             # MajorSubsystemVersion
  le16 0
  le32 0                             # Win32VersionValue
  le32 0x2000                        # SizeOfImage
  le32 0x0200                        # SizeOfHeaders
  le32 0                             # CheckSum
  le16 0x0003                        # Subsystem = Windows console
  le16 0                             # DllCharacteristics
  le32 0x100000                      # SizeOfStackReserve
  le32 0x1000                        # SizeOfStackCommit
  le32 0x100000                      # SizeOfHeapReserve
  le32 0x1000                        # SizeOfHeapCommit
  le32 0                             # LoaderFlags
  le32 16                            # NumberOfRvaAndSizes
  zeroes 128                         # data directories (16 * 8)
  zeroes 128                         # tail
} > "$out/HELLO32.EXE"

echo "wrote $out/HELLODOS.EXE $out/WIN1HELL.EXE $out/HELLO32.EXE"
