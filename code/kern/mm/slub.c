#include <slub.h>
#include <pmm.h>
#include <memlayout.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

/*
 * A small SLUB-style allocator on top of the page allocator.
 *
 * Layer 1: alloc_pages()/free_pages() hand out contiguous page blocks.
 * Layer 2: fixed size classes split one page into objects; requests above
 *          the largest class use a contiguous 2^order page block.
 *
 * Object metadata is always at the first byte of the first page:
 *   - a small slab starts with "struct slab", then the object array;
 *   - a large object starts with the same header, then its payload.
 * kmalloc() returns a pointer inside the first page, so for every pointer it
 * returned, clearing the low PGSHIFT bits finds its header.  Interior
 * pointers (not returned by kmalloc) are rejected by the alignment checks.
 */

#define SLUB_MAGIC_SLAB  0x534C5542u    // "SLUB"
#define SLUB_MAGIC_LARGE 0x4C415247u    // "LARG"

#define SLUB_ALIGN       16
#define SLUB_NR_CACHES   9
#define SLUB_MAX_SMALL   2048
#define SLUB_MAX_LARGE_ORDER 8          // up to 256 pages = 1 MiB

struct slab {
    uint32_t magic;
    uint16_t cache_id;      // index into slub_caches (small slabs)
    uint16_t order;         // page order (0 for small slabs)
    uint32_t obj_size;      // object size in bytes
    uint32_t nr_objs;       // objects carved out of this slab
    uint32_t nr_free;       // objects currently on the freelist
    uint32_t first_obj;     // offset of the first object from the page start
    void *freelist;         // free object chain (next stored in the object)
    struct slab *prev;      // partial/full list links
    struct slab *next;
};

#define SLUB_HEADER_SIZE ((uint32_t)ROUNDUP(sizeof(struct slab), SLUB_ALIGN))
#define SLUB_MAX_ALLOC \
    (((size_t)1 << SLUB_MAX_LARGE_ORDER) * PGSIZE - SLUB_HEADER_SIZE)

static const size_t slub_size_table[SLUB_NR_CACHES] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048,
};

struct kmem_cache {
    size_t obj_size;
    uint32_t objs_per_slab;
    uint32_t nr_slabs;
    uint32_t free_objs;
    struct slab *partial;   // slabs with at least one free object
    struct slab *full;      // slabs with no free object
};

static struct kmem_cache slub_caches[SLUB_NR_CACHES];
static struct slub_stats slub_global;

/* KADDR equivalent: the page was obtained from alloc_pages(), so page2pa()
 * is already a valid physical address and adding the direct-map offset
 * yields the kernel virtual address (KADDR only adds a PPN range check). */
static inline void *
page_to_kva(struct Page *page) {
    return (void *)(uintptr_t)(page2pa(page) + va_pa_offset);
}

static inline struct Page *
kva_to_page(void *ptr) {
    return pa2page((uintptr_t)ptr - (uintptr_t)va_pa_offset);
}

static void
slab_list_push(struct slab **head, struct slab *s) {
    s->prev = NULL;
    s->next = *head;
    if (*head != NULL) {
        (*head)->prev = s;
    }
    *head = s;
}

static void
slab_list_remove(struct slab **head, struct slab *s) {
    if (s->prev != NULL) {
        s->prev->next = s->next;
    } else {
        *head = s->next;
    }
    if (s->next != NULL) {
        s->next->prev = s->prev;
    }
    s->prev = s->next = NULL;
}

/*
 * Find the slab/large-object header for a pointer, without dereferencing
 * memory outside the direct-mapped RAM window.
 */
static struct slab *
slub_find_header(void *ptr) {
    uintptr_t pa = (uintptr_t)ptr - (uintptr_t)va_pa_offset;
    if (pa < DRAM_BASE || pa >= PHYSICAL_MEMORY_END) {
        return NULL;
    }
    uintptr_t page_pa = pa & ~(uintptr_t)(PGSIZE - 1);
    if (PPN(page_pa) < nbase || PPN(page_pa) >= npage) {
        return NULL;
    }
    return (struct slab *)(uintptr_t)(page_pa + va_pa_offset);
}

void
slub_init(void) {
    memset(slub_caches, 0, sizeof(slub_caches));
    memset(&slub_global, 0, sizeof(slub_global));
    for (int i = 0; i < SLUB_NR_CACHES; i ++) {
        struct kmem_cache *c = &slub_caches[i];
        c->obj_size = slub_size_table[i];
        c->objs_per_slab = (PGSIZE - SLUB_HEADER_SIZE) / c->obj_size;
        assert(c->objs_per_slab > 0);
        c->nr_slabs = 0;
        c->free_objs = 0;
        c->partial = c->full = NULL;
    }
}

/* Carve a fresh one-page slab and build its free list (objects pushed in
 * reverse so the first allocation returns the lowest address). */
static struct slab *
slub_new_slab(struct kmem_cache *c) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }
    struct slab *s = (struct slab *)page_to_kva(page);
    s->magic = SLUB_MAGIC_SLAB;
    s->cache_id = (uint16_t)(c - slub_caches);
    s->order = 0;
    s->obj_size = (uint32_t)c->obj_size;
    s->nr_objs = c->objs_per_slab;
    s->nr_free = c->objs_per_slab;
    s->first_obj = SLUB_HEADER_SIZE;
    s->freelist = NULL;
    s->prev = s->next = NULL;

    char *base = (char *)s + s->first_obj;
    for (uint32_t i = s->nr_objs; i > 0; i --) {
        void *obj = base + (size_t)(i - 1) * c->obj_size;
        *(void **)obj = s->freelist;
        s->freelist = obj;
    }

    c->nr_slabs ++;
    c->free_objs += s->nr_objs;
    slub_global.slabs ++;
    slub_global.used_pages ++;
    slub_global.free_objs += s->nr_objs;
    slub_global.slab_capacity += s->nr_objs;
    return s;
}

static void *
slub_alloc_small(struct kmem_cache *c) {
    struct slab *s = c->partial;
    if (s == NULL) {
        s = slub_new_slab(c);
        if (s == NULL) {
            return NULL;
        }
        slab_list_push(&c->partial, s);
    }

    void *obj = s->freelist;
    assert(obj != NULL);
    s->freelist = *(void **)obj;
    s->nr_free --;
    c->free_objs --;
    slub_global.free_objs --;
    slub_global.allocated_objs ++;

    if (s->nr_free == 0) {
        slab_list_remove(&c->partial, s);
        slab_list_push(&c->full, s);
    }
    return obj;
}

static void
slub_free_small(struct slab *s, void *ptr) {
    if (s->cache_id >= SLUB_NR_CACHES) {
        panic("slub: kfree bad slab cache id");
    }
    struct kmem_cache *c = &slub_caches[s->cache_id];
    size_t off = (size_t)((char *)ptr - (char *)s);

    if (off < s->first_obj || off >= PGSIZE
            || (off - s->first_obj) % s->obj_size != 0
            || (off - s->first_obj) / s->obj_size >= s->nr_objs) {
        panic("slub: kfree pointer 0x%lx is not an object",
              (unsigned long)(uintptr_t)ptr);
    }

    // a valid free object is not already on the freelist
    for (void *p = s->freelist; p != NULL; p = *(void **)p) {
        if (p == ptr) {
            panic("slub: double free of 0x%lx",
                  (unsigned long)(uintptr_t)ptr);
        }
    }

    bool was_full = (s->nr_free == 0);
    *(void **)ptr = s->freelist;
    s->freelist = ptr;
    s->nr_free ++;
    c->free_objs ++;
    slub_global.free_objs ++;
    slub_global.allocated_objs --;

    if (s->nr_free == s->nr_objs) {
        // empty slab: give the page back immediately
        if (was_full) {
            slab_list_remove(&c->full, s);
        } else {
            slab_list_remove(&c->partial, s);
        }
        c->nr_slabs --;
        c->free_objs -= s->nr_objs;
        slub_global.slabs --;
        slub_global.free_objs -= s->nr_objs;
        slub_global.slab_capacity -= s->nr_objs;
        slub_global.used_pages --;
        free_pages(kva_to_page(s), 1);
    } else if (was_full) {
        slab_list_remove(&c->full, s);
        slab_list_push(&c->partial, s);
    }
}

static void
slub_free_large(struct slab *s, void *ptr) {
    if (s->order > SLUB_MAX_LARGE_ORDER) {
        panic("slub: kfree bad large order");
    }
    if ((void *)((char *)s + s->first_obj) != ptr) {
        panic("slub: kfree 0x%lx is not the large object start",
              (unsigned long)(uintptr_t)ptr);
    }
    size_t pages = (size_t)1 << s->order;
    slub_global.allocated_objs --;
    slub_global.large_objs --;
    slub_global.used_pages -= pages;
    slub_global.large_pages -= pages;
    free_pages(kva_to_page(s), pages);
}

static void *
slub_alloc_large(size_t size) {
    size_t need = size + SLUB_HEADER_SIZE;
    size_t pages = 1;
    while (pages * PGSIZE < need) {
        pages <<= 1;
    }
    int order = 0;
    while (((size_t)1 << order) < pages) {
        order ++;
    }

    struct Page *page = alloc_pages(pages);
    if (page == NULL) {
        return NULL;
    }
    struct slab *s = (struct slab *)page_to_kva(page);
    s->magic = SLUB_MAGIC_LARGE;
    s->cache_id = 0xFFFF;
    s->order = (uint16_t)order;
    s->obj_size = (uint32_t)size;
    s->nr_objs = 1;
    s->nr_free = 0;
    s->first_obj = SLUB_HEADER_SIZE;
    s->freelist = NULL;
    s->prev = s->next = NULL;

    slub_global.allocated_objs ++;
    slub_global.large_objs ++;
    slub_global.used_pages += pages;
    slub_global.large_pages += pages;
    return (void *)((char *)s + s->first_obj);
}

size_t
slub_alloc_size(size_t size) {
    if (size == 0) {
        return 0;
    }
    if (size <= SLUB_MAX_SMALL) {
        int idx = 0;
        while (slub_size_table[idx] < size) {
            idx ++;
        }
        return slub_size_table[idx];
    }
    if (size > SLUB_MAX_ALLOC) {
        return 0;
    }
    size_t need = size + SLUB_HEADER_SIZE;
    size_t pages = 1;
    while (pages * PGSIZE < need) {
        pages <<= 1;
    }
    return pages * PGSIZE - SLUB_HEADER_SIZE;
}

void *
kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (size <= SLUB_MAX_SMALL) {
        int idx = 0;
        while (slub_size_table[idx] < size) {
            idx ++;
        }
        return slub_alloc_small(&slub_caches[idx]);
    }
    if (size > SLUB_MAX_ALLOC) {
        return NULL;
    }
    return slub_alloc_large(size);
}

void
kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    struct slab *s = slub_find_header(ptr);
    if (s == NULL) {
        cprintf("slub: kfree invalid pointer 0x%lx\n",
                (unsigned long)(uintptr_t)ptr);
        return;
    }
    if (s->magic == SLUB_MAGIC_SLAB) {
        slub_free_small(s, ptr);
    } else if (s->magic == SLUB_MAGIC_LARGE) {
        slub_free_large(s, ptr);
    } else {
        panic("slub: kfree 0x%lx has bad magic 0x%x",
              (unsigned long)(uintptr_t)ptr, s->magic);
    }
}

void
slub_get_stats(struct slub_stats *stats) {
    assert(stats != NULL);
    *stats = slub_global;
}

void
slub_show_stats(void) {
    cprintf("slub: allocated=%lu free=%lu capacity=%lu slabs=%lu "
            "used_pages=%lu large=%lu large_pages=%lu\n",
            (unsigned long)slub_global.allocated_objs,
            (unsigned long)slub_global.free_objs,
            (unsigned long)slub_global.slab_capacity,
            (unsigned long)slub_global.slabs,
            (unsigned long)slub_global.used_pages,
            (unsigned long)slub_global.large_objs,
            (unsigned long)slub_global.large_pages);
}
