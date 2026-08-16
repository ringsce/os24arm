#include "os2.h"

static ULONG heap_current = 0;
static ULONG heap_end = 0;

void mem_init(ULONG start, ULONG size)
{
    heap_current = start;
    heap_end = start + size;
}

void* mem_alloc(ULONG size)
{
    if (heap_current == 0) return NULL;
    size = (size + 15) & ~15;
    if (heap_current + size > heap_end) return NULL;
    void *ptr = (void*)heap_current;
    heap_current += size;
    return ptr;
}

void mem_free(void *ptr) { (void)ptr; }
void scheduler_init(void) { }
void blk_init(void) { }
