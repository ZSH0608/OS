#include <buddy_pmm.h>
#include <list.h>
#include <string.h>

/*
 * Buddy system physical memory manager.
 *
 * Blocks are indexed by their absolute physical page number (PPN), which is
 * what makes the xor trick work: the buddy of a 2^order aligned block at PPN
 * idx is idx ^ (1 << order).  All blocks handed to this manager are aligned
 * in the same absolute numbering, including the very first one produced by
 * page_init() (it starts at an arbitrary PPN and is decomposed accordingly).
 *
 * struct Page usage (shared convention with the other managers):
 *   - only the first page of a free block has property = 2^order and PG_property;
 *   - every other page of a free block has flags = 0, property = 0;
 *   - allocated pages have flags = 0 and property = 0.
 * Because flags/property cannot tell an allocated page from an interior page
 * of a free block, a small per-page state array is kept here (32 KiB for the
 * 128 MiB default RAM).  It is zeroed by buddy_init(); no struct Page field
 * is added or reinterpreted.
 */

#define BUDDY_MAX_PAGES ((PHYSICAL_MEMORY_END - DRAM_BASE) / PGSIZE)

#define BUDDY_STATE_ALLOC 0     // allocated, reserved, or outside the manager
#define BUDDY_STATE_TAIL  0xFF  // interior page of a free block

static free_area_t buddy_free_area[BUDDY_MAX_ORDER + 1];
static size_t buddy_nr_free = 0;
static uint8_t buddy_state[BUDDY_MAX_PAGES];

/* index of a page descriptor in the managed range [nbase, npage) */
static size_t
buddy_index(struct Page *page) {
    return (size_t)(page2ppn(page) - nbase);
}

/* smallest order whose block (2^order pages) can hold n pages */
static int
buddy_order(size_t n) {
    int order = 0;
    while (((size_t)1 << order) < n) {
        order ++;
    }
    return order;
}

/* insert a free block into the address-ordered list of its order */
static void
buddy_insert(struct Page *block, int order) {
    list_entry_t *head = &(buddy_free_area[order].free_list);
    list_entry_t *le = head;
    while ((le = list_next(le)) != head) {
        struct Page *p = le2page(le, page_link);
        if (block < p) {
            break;
        }
    }
    list_add_before(le, &(block->page_link));
}

static void
buddy_init(void) {
    for (int order = 0; order <= BUDDY_MAX_ORDER; order ++) {
        list_init(&(buddy_free_area[order].free_list));
        buddy_free_area[order].nr_free = 0;
    }
    memset(buddy_state, 0, sizeof(buddy_state));
    buddy_nr_free = 0;
}

/*
 * Split [base, base + n) into maximal aligned power-of-two blocks.  The
 * region does not have to start aligned; the greedy loop below takes the
 * largest block that is compatible with the current absolute PPN.
 */
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    assert(npage - nbase <= BUDDY_MAX_PAGES);
    assert(page2ppn(base) >= nbase);
    assert(page2ppn(base) + n <= npage);

    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        ClearPageReserved(p);
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        buddy_state[buddy_index(p)] = BUDDY_STATE_ALLOC;
    }

    uintptr_t idx = page2ppn(base);
    size_t remaining = n;
    while (remaining > 0) {
        int order = 0;
        while (order < BUDDY_MAX_ORDER) {
            size_t size = (size_t)1 << (order + 1);
            if (size > remaining || (idx & (size - 1)) != 0) {
                break;
            }
            order ++;
        }

        size_t size = (size_t)1 << order;
        struct Page *block = pa2page(idx << PGSHIFT);
        size_t local = buddy_index(block);

        block->property = size;
        SetPageProperty(block);
        buddy_state[local] = (uint8_t)(order + 1);
        for (size_t i = 1; i < size; i ++) {
            buddy_state[local + i] = BUDDY_STATE_TAIL;
        }

        buddy_insert(block, order);
        buddy_free_area[order].nr_free ++;
        buddy_nr_free += size;
        idx += size;
        remaining -= size;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    if (n == 0 || n > ((size_t)1 << BUDDY_MAX_ORDER)) {
        return NULL;
    }

    int order = buddy_order(n);

    // first non-empty list at or above the requested order
    int cur = order;
    while (cur <= BUDDY_MAX_ORDER
            && list_empty(&(buddy_free_area[cur].free_list))) {
        cur ++;
    }
    if (cur > BUDDY_MAX_ORDER) {
        return NULL;
    }

    list_entry_t *le = list_next(&(buddy_free_area[cur].free_list));
    struct Page *block = le2page(le, page_link);
    list_del(le);
    buddy_free_area[cur].nr_free --;

    // split the block until it has exactly 2^order pages
    while (cur > order) {
        cur --;
        size_t half = (size_t)1 << cur;
        struct Page *right = block + half;

        // the upper half becomes a free block one order lower
        right->property = half;
        SetPageProperty(right);
        buddy_state[buddy_index(right)] = (uint8_t)(cur + 1);
        buddy_insert(right, cur);
        buddy_free_area[cur].nr_free ++;

        // the lower half stays the block we keep splitting
        block->property = half;
        SetPageProperty(block);
        buddy_state[buddy_index(block)] = (uint8_t)(cur + 1);
    }

    // hand out the block: nr_free drops by 2^order, not by n
    size_t size = (size_t)1 << order;
    size_t local = buddy_index(block);
    ClearPageProperty(block);
    block->property = 0;
    for (size_t i = 0; i < size; i ++) {
        struct Page *p = block + i;
        p->flags = 0;
        set_page_ref(p, 0);
        buddy_state[local + i] = BUDDY_STATE_ALLOC;
    }
    buddy_nr_free -= size;
    return block;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    if (n == 0) {
        return;
    }
    assert(n <= ((size_t)1 << BUDDY_MAX_ORDER));

    // free_pages rounds the same way alloc_pages does
    int order = buddy_order(n);
    size_t size = (size_t)1 << order;
    uintptr_t idx = page2ppn(base);

    assert(idx >= nbase && idx + size <= npage);
    assert((idx & (size - 1)) == 0);

    size_t local = (size_t)(idx - nbase);
    for (size_t i = 0; i < size; i ++) {
        struct Page *p = base + i;
        assert(!PageReserved(p));
        assert(!PageProperty(p));
        // rejects double frees and frees of interior pages of a free block
        assert(buddy_state[local + i] == BUDDY_STATE_ALLOC);
    }

    for (size_t i = 0; i < size; i ++) {
        struct Page *p = base + i;
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = size;
    SetPageProperty(base);
    buddy_state[local] = (uint8_t)(order + 1);
    for (size_t i = 1; i < size; i ++) {
        buddy_state[local + i] = BUDDY_STATE_TAIL;
    }
    buddy_nr_free += size;

    // merge with buddies while the partner is a free block of the same order
    while (order < BUDDY_MAX_ORDER) {
        uintptr_t buddy_idx = idx ^ ((uintptr_t)1 << order);
        if (buddy_idx < nbase || buddy_idx + size > npage) {
            break;
        }
        size_t buddy_local = (size_t)(buddy_idx - nbase);
        if (buddy_state[buddy_local] != (uint8_t)(order + 1)) {
            break;
        }
        struct Page *buddy = pa2page(buddy_idx << PGSHIFT);
        if (PageReserved(buddy) || !PageProperty(buddy)
                || buddy->property != size) {
            break;
        }

        // the buddy is no longer a block on its own
        list_del(&(buddy->page_link));
        buddy_free_area[order].nr_free --;

        struct Page *block = (buddy < base) ? buddy : base;
        struct Page *other = (buddy < base) ? base : buddy;
        ClearPageProperty(other);
        buddy_state[buddy_index(other)] = BUDDY_STATE_TAIL;

        block->property = (size_t)1 << (order + 1);
        SetPageProperty(block);
        buddy_state[buddy_index(block)] = (uint8_t)(order + 2);

        base = block;
        idx = page2ppn(base);
        order ++;
        size <<= 1;
    }

    buddy_insert(base, order);
    buddy_free_area[order].nr_free ++;
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

/*
 * Structural self check: ordering, alignment, power-of-two sizes, metadata
 * consistency and the total page count.  Overlapping blocks are detected
 * because a block's head must not sit in another block's interior state.
 */
static void
buddy_check(void) {
    size_t total = 0;
    for (int order = 0; order <= BUDDY_MAX_ORDER; order ++) {
        list_entry_t *head = &(buddy_free_area[order].free_list);
        list_entry_t *le = head;
        unsigned int count = 0;
        uintptr_t prev = 0;

        while ((le = list_next(le)) != head) {
            struct Page *p = le2page(le, page_link);
            uintptr_t idx = page2ppn(p);
            size_t size = (size_t)1 << order;
            size_t local = (size_t)(idx - nbase);

            assert(idx >= nbase && idx + size <= npage);
            assert((idx & (size - 1)) == 0);
            assert(!PageReserved(p));
            assert(PageProperty(p));
            assert(p->property == size);
            assert(buddy_state[local] == (uint8_t)(order + 1));
            if (count > 0) {
                assert(idx > prev);
            }
            for (size_t i = 1; i < size; i ++) {
                assert(!PageProperty(p + i));
                assert(buddy_state[local + i] == BUDDY_STATE_TAIL);
            }

            prev = idx;
            count ++;
            total += size;
        }
        assert(count == buddy_free_area[order].nr_free);
    }
    assert(total == buddy_nr_free);
}

/* read-only hooks for buddy_test.c */

int
buddy_test_order_count(int order) {
    if (order < 0 || order > BUDDY_MAX_ORDER) {
        return 0;
    }
    return (int)buddy_free_area[order].nr_free;
}

struct Page *
buddy_test_order_block(int order, int index) {
    if (order < 0 || order > BUDDY_MAX_ORDER || index < 0) {
        return NULL;
    }
    list_entry_t *head = &(buddy_free_area[order].free_list);
    list_entry_t *le = head;
    while ((le = list_next(le)) != head) {
        if (index == 0) {
            return le2page(le, page_link);
        }
        index --;
    }
    return NULL;
}

int
buddy_test_page_state(struct Page *page) {
    assert(page != NULL);
    assert(page >= pages && page < pages + (npage - nbase));
    return (int)buddy_state[buddy_index(page)];
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
