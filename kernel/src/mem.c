#include "types.h"

static uintptr_t heap_start = 0;
static uintptr_t heap_current = 0;
static uintptr_t heap_end = 0;

void mem_init(uintptr_t start, size_t size)
{
    heap_start = start;
    heap_current = start;
    heap_end = start + size;
}

void* mem_alloc(size_t size)
{
    /* SCTLR_EL1.A (alignment checking) is enabled at boot, so any
     * misaligned load/store data-aborts. Align the block itself, not just
     * its size, since _bss_end (and thus the heap base) isn't guaranteed
     * to land on an 8-byte boundary. */
    heap_current = (heap_current + 7) & ~(uintptr_t)7;
    size = (size + 7) & ~7;

    if (heap_current + size > heap_end) {
        return NULL;
    }

    void* ptr = (void*)heap_current;
    heap_current += size;
    return ptr;
}

void mem_free(void* ptr)
{
    (void)ptr;
}

void mem_get_stats(uintptr_t *used, uintptr_t *total)
{
    if (used)
        *used = heap_current - heap_start;
    if (total)
        *total = heap_end - heap_start;
}