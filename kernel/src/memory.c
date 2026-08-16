/* ============================================================================
 * kernel/memory.c  —  Bump allocator with a minimal free-list
 *
 * Layout:
 *   [ BLOCK_HEADER | user data ... ] ...
 *
 * Free-list: freed blocks are linked via the header.  kmalloc first scans
 * the free list for a large-enough block, then falls back to bump allocation.
 * ========================================================================== */

#include "memory.h"
#include "kio.h"

#define ALIGN8(x)  (((x) + 7u) & ~7u)

typedef struct block_hdr {
    size_t            size;   /* usable bytes (excluding header) */
    bool              free;
    struct block_hdr *next;   /* free-list link */
} block_hdr_t;

#define HDR_SIZE  ALIGN8(sizeof(block_hdr_t))

static uintptr_t   heap_base  = 0;
static uintptr_t   heap_limit = 0;
static uintptr_t   heap_brk   = 0;   /* next free byte (bump pointer) */
static block_hdr_t *free_list = NULL;

void mem_init(uintptr_t start, size_t size)
{
    heap_base  = ALIGN8(start);
    heap_brk   = heap_base;
    heap_limit = heap_base + size;
    free_list  = NULL;
    kprintf("[MEM] heap %p – %p  (%lu KB)\n",
            (void *)heap_base, (void *)heap_limit, size / 1024);
}

void *kmalloc(size_t size)
{
    if (size == 0) return NULL;
    size = ALIGN8(size);

    /* ── Scan free list ─────────────────────────────────────────────────── */
    block_hdr_t **pp = &free_list;
    while (*pp) {
        block_hdr_t *b = *pp;
        if (b->size >= size) {
            /* Remove from free list */
            *pp = b->next;
            b->free = false;
            b->next = NULL;
            return (void *)((uintptr_t)b + HDR_SIZE);
        }
        pp = &b->next;
    }

    /* ── Bump allocate ──────────────────────────────────────────────────── */
    if (heap_brk + HDR_SIZE + size > heap_limit) {
        kprintf("[MEM] OUT OF MEMORY!\n");
        return NULL;
    }
    block_hdr_t *b = (block_hdr_t *)heap_brk;
    heap_brk += HDR_SIZE + size;

    b->size = size;
    b->free = false;
    b->next = NULL;
    return (void *)((uintptr_t)b + HDR_SIZE);
}

void kfree(void *ptr)
{
    if (!ptr) return;
    block_hdr_t *b = (block_hdr_t *)((uintptr_t)ptr - HDR_SIZE);
    if (b->free) {
        kprintf("[MEM] double-free at %p!\n", ptr);
        return;
    }
    b->free = true;
    b->next = free_list;
    free_list = b;
}

void mem_dump(void)
{
    size_t used  = heap_brk - heap_base;
    size_t total = heap_limit - heap_base;
    kprintf("[MEM] used %lu / %lu bytes (%.0u%%)\n",
            used, total, (uint32_t)(used * 100 / total));

    size_t free_bytes = 0;
    block_hdr_t *b = free_list;
    while (b) { free_bytes += b->size; b = b->next; }
    kprintf("[MEM] free-list: %lu bytes in reclaimed blocks\n", free_bytes);
}
