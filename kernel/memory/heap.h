#pragma once
#include <m5os/types.h>
#include <m5os/kmalloc.h>

/*
 * A heap is a contiguous region carved into blocks:
 *
 *   +--------+------------------+--------+------------------+----- ...
 *   | header | payload          | header | payload          |
 *   +--------+------------------+--------+------------------+
 *
 * Header (16 bytes keeps every payload 16-byte aligned):
 *   magic      MAGIC_USED / MAGIC_FREE - catches double free and overruns
 *   size       total block size including header (multiple of 16)
 *   prev_size  size of the physically previous block, 0 for the first
 *   owner      app id that allocated it via umalloc(), 0 = kernel (cleared on free)
 *              ("boundary tag": lets free() find its left neighbour in O(1))
 * Free blocks reuse the start of their payload as doubly linked free-list
 * pointers.  Policy: first fit, split when the remainder can hold a block,
 * coalesce with both neighbours on free.
 */
typedef struct heap_block heap_block_t;

typedef struct {
    const char    *name;
    uintptr_t      base, end;       /* [base, end) */
    heap_block_t  *free_list;
    kmem_stats_t   st;
} heap_t;

void  heap_init(heap_t *h, const char *name, void *base, size_t size);
void *heap_alloc(heap_t *h, size_t size);                        /* kernel-owned (owner 0) */
void *heap_alloc_owned(heap_t *h, size_t size, uint32_t owner);   /* tagged with an app id */
size_t heap_reclaim(heap_t *h, uint32_t owner);                   /* free every block of `owner`; bytes freed */
size_t heap_owned_bytes(heap_t *h, uint32_t owner);
void  heap_free(heap_t *h, void *p);
bool  heap_contains(const heap_t *h, const void *p);
void  heap_stats(heap_t *h, kmem_stats_t *out);
bool  heap_check(heap_t *h);        /* walks every block; verifies the invariants */
