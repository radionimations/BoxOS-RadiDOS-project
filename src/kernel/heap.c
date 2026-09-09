/* Tiny bump allocator. malloc grows; free is a no-op (apps don't
 * actually free much during a single run). Good enough for Doom's
 * Z_Malloc patterns and our session-1 scope.
 *
 * Heap region: 16 MiB at physical 0x1000000 (= 16 MiB), identity-
 * mapped by map_first_gigabyte(). */

#include "boxos.h"

#define HEAP_BASE  0x1000000ULL
#define HEAP_SIZE  (96ULL * 1024 * 1024)   /* 96 MiB — DOOM Z + WAD + headroom */

static uint8_t* g_next;
static uint8_t* g_end;

void heap_init(void) {
    g_next = (uint8_t*)HEAP_BASE;
    g_end  = (uint8_t*)(HEAP_BASE + HEAP_SIZE);
}

void* heap_alloc(size_t bytes) {
    /* 16-byte align so SSE-style consumers don't trip. */
    size_t aligned = (bytes + 15) & ~(size_t)15;
    /* Detect grossly oversized requests (e.g. negative-as-unsigned). */
    if (aligned > HEAP_SIZE) {
        vga_printf("[heap] reject %lu bytes (insane size)\n", (uint64_t)bytes);
        return NULL;
    }
    if (g_next + aligned > g_end) {
        vga_printf("[heap] OOM: want %lu  used %lu  free %lu\n",
                   (uint64_t)bytes,
                   (uint64_t)(g_next - (uint8_t*)HEAP_BASE),
                   (uint64_t)(g_end  - g_next));
        return NULL;
    }
    void* p = g_next;
    g_next += aligned;
    return p;
}

void* heap_alloc_aligned(size_t bytes, size_t align) {
    /* `align` must be a power of two. Advances g_next up to the next
     * aligned address, then does a normal bump-alloc. The skipped
     * bytes are wasted — fine for our handful of large aligned
     * allocations (page tables, app user-spaces). */
    if (align == 0) align = 1;
    uint64_t a = ((uint64_t)g_next + align - 1) & ~(uint64_t)(align - 1);
    g_next = (uint8_t*)a;
    return heap_alloc(bytes);
}

void heap_free(void* p) { (void)p; /* no-op for now */ }

size_t heap_used(void)  { return (size_t)(g_next - (uint8_t*)HEAP_BASE); }
size_t heap_total(void) { return HEAP_SIZE; }
