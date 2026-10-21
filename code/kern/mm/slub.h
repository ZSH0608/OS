#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* Global allocator counters, useful for tests. */
struct slub_stats {
    size_t allocated_objs;  // live objects (small + large)
    size_t free_objs;       // free objects sitting in partial slabs
    size_t slab_capacity;   // total object slots in live small slabs
    size_t slabs;           // live small-object slabs
    size_t used_pages;      // pages held by slabs and large objects
    size_t large_objs;      // live large objects
    size_t large_pages;     // pages held by large objects
};

/* Must be called once after pmm_init() has built the page allocator. */
void slub_init(void);

/*
 * kmalloc - allocate at least @size bytes.
 * Returns NULL for size == 0 or size above the supported maximum.
 * Memory is NOT zeroed.  The result is 8-byte aligned (16-byte for the
 * size classes from 16 upwards and for large objects).
 */
void *kmalloc(size_t size);

/* kfree - release a pointer returned by kmalloc.  NULL is ignored. */
void kfree(void *ptr);

void slub_get_stats(struct slub_stats *stats);
void slub_show_stats(void);

/*
 * Usable bytes of a successful kmalloc(size): the size class for small
 * objects, or pages*PGSIZE - header for large objects.  Returns 0 when
 * kmalloc(size) would fail.
 */
size_t slub_alloc_size(size_t size);

#endif /* !__KERN_MM_SLUB_H__ */
