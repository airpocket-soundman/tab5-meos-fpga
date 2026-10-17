#include "heap.h"
#include <m5os/arch.h>
#include <m5os/kernel.h>

#define ALIGN            16u
#define HDR_SIZE         16u
#define MIN_BLOCK        32u                 /* header + 16 bytes of payload (room for free-list links) */
#define MAGIC_USED       0xA110C8EDu
#define MAGIC_FREE       0xF4EEB10Cu

struct heap_block {
    uint32_t magic;
    uint32_t size;
    uint32_t prev_size;
    uint32_t owner;
    /* free blocks only (inside the payload): */
    heap_block_t *next_free;
    heap_block_t *prev_free;
};

static inline size_t align_up(size_t v) { return (v + (ALIGN - 1)) & ~(size_t)(ALIGN - 1); }
static inline void *payload(heap_block_t *b) { return (uint8_t *)b + HDR_SIZE; }
static inline heap_block_t *from_payload(void *p) { return (heap_block_t *)((uint8_t *)p - HDR_SIZE); }

static heap_block_t *next_block(const heap_t *h, heap_block_t *b)
{
    uintptr_t n = (uintptr_t)b + b->size;
    return n < h->end ? (heap_block_t *)n : NULL;
}

static heap_block_t *prev_block(heap_block_t *b)
{
    return b->prev_size ? (heap_block_t *)((uintptr_t)b - b->prev_size) : NULL;
}

static void list_insert(heap_t *h, heap_block_t *b)
{
    b->magic = MAGIC_FREE;
    b->prev_free = NULL;
    b->next_free = h->free_list;
    if (h->free_list) h->free_list->prev_free = b;
    h->free_list = b;
}

static void list_remove(heap_t *h, heap_block_t *b)
{
    if (b->prev_free) b->prev_free->next_free = b->next_free;
    else              h->free_list = b->next_free;
    if (b->next_free) b->next_free->prev_free = b->prev_free;
}

void heap_init(heap_t *h, const char *name, void *base, size_t size)
{
    uintptr_t b = ((uintptr_t)base + (ALIGN - 1)) & ~(uintptr_t)(ALIGN - 1);
    uintptr_t e = ((uintptr_t)base + size) & ~(uintptr_t)(ALIGN - 1);
    h->name = name;
    h->base = b;
    h->end  = e;
    h->free_list = NULL;
    h->st = (kmem_stats_t){ .total = e - b };

    heap_block_t *first = (heap_block_t *)b;
    first->size = (uint32_t)(e - b);
    first->prev_size = 0;
    list_insert(h, first);
    h->st.free = first->size;
}

void *heap_alloc(heap_t *h, size_t size) { return heap_alloc_owned(h, size, 0); }

void *heap_alloc_owned(heap_t *h, size_t size, uint32_t owner)
{
    if (size == 0 || size > h->st.total) {
        h->st.fail_count++;
        return NULL;
    }
    size_t need = align_up(size) + HDR_SIZE;
    if (need < MIN_BLOCK) need = MIN_BLOCK;

    uint32_t irq = arch_irq_disable();

    heap_block_t *b = h->free_list;
    while (b && b->size < need) b = b->next_free;      /* first fit */
    if (!b) {
        h->st.fail_count++;
        arch_irq_restore(irq);
        return NULL;
    }
    list_remove(h, b);

    if (b->size - need >= MIN_BLOCK) {                  /* split: tail stays free */
        heap_block_t *rest = (heap_block_t *)((uintptr_t)b + need);
        rest->size = b->size - (uint32_t)need;
        rest->prev_size = (uint32_t)need;
        rest->owner = 0;
        b->size = (uint32_t)need;
        heap_block_t *after = next_block(h, rest);
        if (after) after->prev_size = rest->size;
        list_insert(h, rest);
    }
    b->magic = MAGIC_USED;
    b->owner = owner;

    h->st.used += b->size;
    h->st.free -= b->size;
    if (h->st.used > h->st.peak_used) h->st.peak_used = h->st.used;
    h->st.alloc_count++;

    arch_irq_restore(irq);
    return payload(b);
}

void heap_free(heap_t *h, void *p)
{
    heap_block_t *b = from_payload(p);
    uint32_t irq = arch_irq_disable();

    if (b->magic == MAGIC_FREE) kernel_panic("kfree: double free");
    if (b->magic != MAGIC_USED) kernel_panic("kfree: corrupt block header");

    h->st.used -= b->size;
    h->st.free += b->size;
    b->owner = 0;

    heap_block_t *nxt = next_block(h, b);
    if (nxt && nxt->magic == MAGIC_FREE) {              /* merge right */
        list_remove(h, nxt);
        b->size += nxt->size;
        nxt->magic = 0;
    }
    heap_block_t *prv = prev_block(b);
    if (prv && prv->magic == MAGIC_FREE) {              /* merge left */
        list_remove(h, prv);
        prv->size += b->size;
        b->magic = 0;
        b = prv;
    }
    heap_block_t *after = next_block(h, b);
    if (after) after->prev_size = b->size;
    list_insert(h, b);

    arch_irq_restore(irq);
}

bool heap_contains(const heap_t *h, const void *p)
{
    return (uintptr_t)p >= h->base + HDR_SIZE && (uintptr_t)p < h->end;
}

void heap_stats(heap_t *h, kmem_stats_t *out)
{
    uint32_t irq = arch_irq_disable();
    *out = h->st;
    out->largest_free = 0;
    for (heap_block_t *b = h->free_list; b; b = b->next_free) {
        if (b->size - HDR_SIZE > out->largest_free) out->largest_free = b->size - HDR_SIZE;
    }
    arch_irq_restore(irq);
}

/* Walk physically; every block must chain exactly to the heap end, prev_size
 * tags must match, no two free blocks may be adjacent (coalescing invariant),
 * and the accounting must add up. */
bool heap_check(heap_t *h)
{
    uint32_t irq = arch_irq_disable();
    bool ok = true;
    size_t used = 0, freeb = 0;
    uint32_t prev_size = 0;
    bool prev_free = false;
    uintptr_t a = h->base;
    while (a < h->end) {
        heap_block_t *b = (heap_block_t *)a;
        if ((b->magic != MAGIC_USED && b->magic != MAGIC_FREE) ||
            b->size < MIN_BLOCK || (b->size & (ALIGN - 1)) || a + b->size > h->end ||
            b->prev_size != prev_size) { ok = false; break; }
        bool is_free = (b->magic == MAGIC_FREE);
        if (is_free && prev_free) { ok = false; break; }
        if (is_free) freeb += b->size; else used += b->size;
        prev_free = is_free;
        prev_size = b->size;
        a += b->size;
    }
    if (ok && (a != h->end || used != h->st.used || freeb != h->st.free)) ok = false;
    arch_irq_restore(irq);
    return ok;
}

size_t heap_owned_bytes(heap_t *h, uint32_t owner)
{
    size_t n = 0;
    uint32_t irq = arch_irq_disable();
    for (uintptr_t a = h->base; a < h->end; a += ((heap_block_t *)a)->size) {
        heap_block_t *b = (heap_block_t *)a;
        if (b->magic == MAGIC_USED && b->owner == owner) n += b->size;
    }
    arch_irq_restore(irq);
    return n;
}

/* Free every block tagged with `owner`.  After each free the blocks around it may
 * merge, so the walk restarts from the beginning (the number of owned blocks is small). */
size_t heap_reclaim(heap_t *h, uint32_t owner)
{
    size_t total = 0;
    for (;;) {
        void *victim = NULL;
        uint32_t irq = arch_irq_disable();
        for (uintptr_t a = h->base; a < h->end; a += ((heap_block_t *)a)->size) {
            heap_block_t *b = (heap_block_t *)a;
            if (b->magic == MAGIC_USED && b->owner == owner) { victim = payload(b); total += b->size; break; }
        }
        arch_irq_restore(irq);
        if (!victim) return total;
        heap_free(h, victim);
    }
}
