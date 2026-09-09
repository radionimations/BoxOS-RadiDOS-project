/* Generate three minimal but plausible .EXE files to exercise the
 * BoxOS EXE inspector. None contain real code -- just enough header
 * structure that exe_inspect() reports the correct format. Built
 * and invoked at host build time, NOT compiled into the kernel.
 *
 *   make_test_exes <output-dir> */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void put16(FILE* f, uint16_t v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}
static void put32(FILE* f, uint32_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}
static void putzero(FILE* f, int n) { while (n--) fputc(0, f); }

static void mz_header(FILE* f, uint32_t lfanew_or_zero, uint16_t pages) {
    fputc('M', f); fputc('Z', f);
    put16(f, 128);                /* e_cblp                       */
    put16(f, pages);              /* e_cp                         */
    put16(f, 0);                  /* e_crlc                       */
    put16(f, 4);                  /* e_cparhdr (64-byte hdr)      */
    put16(f, 16);                 /* e_minalloc                   */
    put16(f, 0xFFFF);             /* e_maxalloc                   */
    put16(f, 0);                  /* e_ss                         */
    put16(f, 0x0100);             /* e_sp                         */
    put16(f, 0);                  /* e_csum                       */
    put16(f, 0x0000);             /* e_ip  (entry IP)             */
    put16(f, 0x0000);             /* e_cs  (entry CS)             */
    put16(f, 0x0040);             /* e_lfarlc                     */
    put16(f, 0);                  /* e_ovno                       */
    putzero(f, 32);               /* reserved + OEM fields        */
    /* offset is now 0x3C — write e_lfanew. */
    put32(f, lfanew_or_zero);
}

static void make_dos(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    mz_header(f, 0, 1);           /* one page; no extended hdr   */
    /* Pad to 512 bytes total. We've written 0x40, need 0x1C0 more. */
    putzero(f, 512 - 0x40);
    fclose(f);
}

static void make_ne(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    mz_header(f, 0x40, 2);
    /* NE header at file offset 0x40. */
    fputc('N', f); fputc('E', f);
    fputc(5,  f); fputc(21, f);   /* linker version              */
    put16(f, 0); put16(f, 0);     /* entry table off/len         */
    put32(f, 0);                  /* CRC                          */
    put16(f, 0x8002);             /* flags                        */
    put16(f, 0);                  /* auto data segment            */
    put16(f, 0);                  /* heap                         */
    put16(f, 0);                  /* stack                        */
    put32(f, 0);                  /* CS:IP                        */
    put32(f, 0);                  /* SS:SP                        */
    put16(f, 1);                  /* segment count                */
    put16(f, 0);                  /* module ref count             */
    put16(f, 0);                  /* non-resident name table sz   */
    put16(f, 0);                  /* segment table off            */
    put16(f, 0);                  /* resource table off           */
    put16(f, 0);                  /* resident name table off      */
    put16(f, 0);                  /* module ref table off         */
    put16(f, 0);                  /* imported names table off     */
    put32(f, 0);                  /* non-resident names off       */
    put16(f, 0);                  /* movable entries              */
    put16(f, 4);                  /* logical sector alignment     */
    put16(f, 0);                  /* resource segment count       */
    fputc(0x02, f);               /* target OS = Windows          */
    putzero(f, 9);                /* rest of NE header            */
    /* Pad to a couple of pages. */
    putzero(f, 1024 - 0x40 - 64);
    fclose(f);
}

static void make_pe(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    mz_header(f, 0x80, 2);
    /* DOS stub program: 0x40..0x7F = 64 bytes of zeros. */
    putzero(f, 0x80 - 0x40);
    /* PE header at file offset 0x80. */
    fputc('P', f); fputc('E', f); fputc(0, f); fputc(0, f);
    /* COFF File Header */
    put16(f, 0x014C);             /* Machine = i386               */
    put16(f, 1);                  /* NumberOfSections             */
    put32(f, 0);                  /* TimeDateStamp                */
    put32(f, 0);                  /* PointerToSymbolTable         */
    put32(f, 0);                  /* NumberOfSymbols              */
    put16(f, 224);                /* SizeOfOptionalHeader         */
    put16(f, 0x010F);             /* Characteristics: EXE+32-bit  */
    /* Optional Header (PE32, 0x10B) */
    put16(f, 0x010B);             /* Magic                        */
    fputc(1, f); fputc(0, f);     /* linker major/minor           */
    put32(f, 0x1000);             /* SizeOfCode                   */
    put32(f, 0);                  /* SizeOfInitializedData        */
    put32(f, 0);                  /* SizeOfUninitializedData      */
    put32(f, 0x1000);             /* AddressOfEntryPoint (RVA)    */
    put32(f, 0x1000);             /* BaseOfCode                   */
    put32(f, 0x2000);             /* BaseOfData                   */
    put32(f, 0x00400000);         /* ImageBase                    */
    put32(f, 0x1000);             /* SectionAlignment             */
    put32(f, 0x0200);             /* FileAlignment                */
    put16(f, 4); put16(f, 0);     /* OS major/minor               */
    put16(f, 0); put16(f, 0);     /* image major/minor            */
    put16(f, 4); put16(f, 0);     /* subsys major/minor           */
    put32(f, 0);                  /* Win32VersionValue            */
    put32(f, 0x2000);             /* SizeOfImage                  */
    put32(f, 0x0200);             /* SizeOfHeaders                */
    put32(f, 0);                  /* CheckSum                     */
    put16(f, 0x0003);             /* Subsystem = Console          */
    put16(f, 0);                  /* DllCharacteristics           */
    put32(f, 0x100000);           /* SizeOfStackReserve           */
    put32(f, 0x1000);             /* SizeOfStackCommit            */
    put32(f, 0x100000);           /* SizeOfHeapReserve            */
    put32(f, 0x1000);             /* SizeOfHeapCommit             */
    put32(f, 0);                  /* LoaderFlags                  */
    put32(f, 16);                 /* NumberOfRvaAndSizes          */
    putzero(f, 16 * 8);           /* data directories             */
    /* Pad up so the file is a couple pages. */
    putzero(f, 1024 - 0x80 - 24 - 224);
    fclose(f);
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : "build/test_exes";
    char path[256];
    snprintf(path, sizeof(path), "%s/HELLODOS.EXE", dir); make_dos(path);
    snprintf(path, sizeof(path), "%s/WIN1HELL.EXE", dir); make_ne (path);
    snprintf(path, sizeof(path), "%s/HELLO32.EXE", dir);  make_pe (path);
    fprintf(stderr, "wrote test EXEs into %s\n", dir);
    return 0;
}
