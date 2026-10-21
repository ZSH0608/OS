#include <buddy_test.h>
#include <buddy_pmm.h>
#include <pmm.h>
#include <stdio.h>

/* no <stdbool.h> under -nostdinc; defs.h only provides "typedef int bool" */
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

/*
 * Standalone behavioural tests for the buddy pmm.
 *
 * Every test frees what it allocates, so the free lists should return to the
 * exact canonical decomposition they had before the suite started.  The last
 * test compares a full snapshot of every order list with the baseline.
 *
 * Output format: "[Buddy] <name>: PASS|FAIL".  Failures print the pages,
 * orders and counts involved on indented "[Buddy]   ..." lines before the
 * verdict line.
 */

#define BT_MAX_BLOCKS 512
#define BT_MAX_ORDER  BUDDY_MAX_ORDER

struct bt_block {
    uintptr_t ppn;
    size_t size;
};

/*
 * The kernel boot stack is only 8 KiB, so the snapshots live in .bss instead
 * of on the stack (512 * 16 bytes per snapshot).
 */
static struct bt_block bt_scratch[BT_MAX_BLOCKS];
static struct bt_block bt_snap[BT_MAX_BLOCKS];
static struct bt_block bt_baseline[BT_MAX_BLOCKS];
static struct bt_block bt_final[BT_MAX_BLOCKS];
static struct Page *bt_held[BT_MAX_BLOCKS];
static size_t bt_held_req[BT_MAX_BLOCKS];

static void
bt_report(const char *name, bool ok) {
    cprintf("[Buddy] %s: %s\n", name, ok ? "PASS" : "FAIL");
}

static size_t
bt_alloc_size(size_t n) {
    size_t size = 1;
    while (size < n) {
        size <<= 1;
    }
    return size;
}

static bool
bt_overlap(uintptr_t a, size_t asz, uintptr_t b, size_t bsz) {
    return a < b + bsz && b < a + asz;
}

/*
 * Flatten every order list into arr.  The result is grouped by order and,
 * inside an order, keeps the list order (which must be increasing by PPN).
 * Returns the number of blocks, or -1 if a list is inconsistent/too long.
 */
static int
bt_snapshot(struct bt_block *arr, int max) {
    int count = 0;
    for (int order = 0; order <= BT_MAX_ORDER; order ++) {
        int nr = buddy_test_order_count(order);
        for (int i = 0; i < nr; i ++) {
            struct Page *p = buddy_test_order_block(order, i);
            if (p == NULL || count >= max) {
                return -1;
            }
            arr[count].ppn = page2ppn(p);
            arr[count].size = (size_t)1 << order;
            count ++;
        }
    }
    return count;
}

static int
bt_find_block(const struct bt_block *arr, int count, uintptr_t ppn) {
    for (int i = 0; i < count; i ++) {
        if (ppn >= arr[i].ppn && ppn < arr[i].ppn + arr[i].size) {
            return i;
        }
    }
    return -1;
}

static bool
bt_snapshot_equal(const struct bt_block *a, int na,
                  const struct bt_block *b, int nb) {
    if (na != nb) {
        return false;
    }
    for (int i = 0; i < na; i ++) {
        if (a[i].ppn != b[i].ppn || a[i].size != b[i].size) {
            return false;
        }
    }
    return true;
}

static void
bt_counts(int *dst) {
    for (int order = 0; order <= BT_MAX_ORDER; order ++) {
        dst[order] = buddy_test_order_count(order);
    }
}

static bool
bt_counts_equal(const int *a, const int *b) {
    for (int order = 0; order <= BT_MAX_ORDER; order ++) {
        if (a[order] != b[order]) {
            return false;
        }
    }
    return true;
}

/*
 * Test 1: structural invariants.  Catches wrong alignment (bad buddy XOR),
 * missing splits/merges (a non-power-of-two or misaligned block), stale
 * PG_property, overlapping blocks and nr_free bookkeeping errors.
 */
static bool
bt_structure(const char *name, bool announce) {
    struct bt_block *blocks = bt_scratch;
    int nblocks = 0;
    size_t total = 0;
    bool ok = true;

    for (int order = 0; order <= BT_MAX_ORDER; order ++) {
        int nr = buddy_test_order_count(order);
        size_t size = (size_t)1 << order;
        uintptr_t prev = 0;

        for (int i = 0; i < nr; i ++) {
            struct Page *p = buddy_test_order_block(order, i);
            if (p == NULL) {
                cprintf("[Buddy]   %s: order %d block %d missing\n",
                        name, order, i);
                ok = false;
                continue;
            }
            uintptr_t ppn = page2ppn(p);

            if (ppn < nbase || ppn + size > npage) {
                cprintf("[Buddy]   %s: order %d ppn=0x%lx out of range\n",
                        name, order, (unsigned long)ppn);
                ok = false;
            }
            if ((ppn & (size - 1)) != 0) {
                cprintf("[Buddy]   %s: order %d ppn=0x%lx misaligned\n",
                        name, order, (unsigned long)ppn);
                ok = false;
            }
            if (!PageProperty(p) || p->property != size) {
                cprintf("[Buddy]   %s: order %d ppn=0x%lx property=%u expect=%lu\n",
                        name, order, (unsigned long)ppn,
                        p->property, (unsigned long)size);
                ok = false;
            }
            if (PageReserved(p)) {
                cprintf("[Buddy]   %s: order %d ppn=0x%lx is reserved\n",
                        name, order, (unsigned long)ppn);
                ok = false;
            }
            if (i > 0 && ppn <= prev) {
                cprintf("[Buddy]   %s: order %d not sorted (0x%lx then 0x%lx)\n",
                        name, order, (unsigned long)prev, (unsigned long)ppn);
                ok = false;
            }
            if (nblocks >= BT_MAX_BLOCKS) {
                cprintf("[Buddy]   %s: too many blocks\n", name);
                bt_report(name, false);
                return false;
            }

            blocks[nblocks].ppn = ppn;
            blocks[nblocks].size = size;
            nblocks ++;
            total += size;
            prev = ppn;
        }
    }

    for (int i = 0; i < nblocks && ok; i ++) {
        for (int j = i + 1; j < nblocks; j ++) {
            if (bt_overlap(blocks[i].ppn, blocks[i].size,
                           blocks[j].ppn, blocks[j].size)) {
                cprintf("[Buddy]   %s: overlap [0x%lx,%lu] and [0x%lx,%lu]\n",
                        name, (unsigned long)blocks[i].ppn,
                        (unsigned long)blocks[i].size,
                        (unsigned long)blocks[j].ppn,
                        (unsigned long)blocks[j].size);
                ok = false;
                break;
            }
        }
    }

    if (total != nr_free_pages()) {
        cprintf("[Buddy]   %s: sum=%lu nr_free=%lu\n", name,
                (unsigned long)total, (unsigned long)nr_free_pages());
        ok = false;
    }
    if (announce) {
        bt_report(name, ok);
    }
    return ok;
}

static bool
bt_test_initial(void) {
    return bt_structure("initial structure", true);
}

/* Test 2: basic allocation, reuse and page count bookkeeping. */
static bool
bt_test_basic(void) {
    size_t base = nr_free_pages();
    size_t req[3] = {1, 2, 3};
    size_t sz[3] = {0, 0, 0};
    struct Page *p[3] = {NULL, NULL, NULL};
    bool ok = true;
    bool all = true;

    for (int i = 0; i < 3; i ++) {
        sz[i] = bt_alloc_size(req[i]);
        p[i] = alloc_pages(req[i]);
        if (p[i] == NULL) {
            cprintf("[Buddy]   basic: alloc %lu failed\n", (unsigned long)req[i]);
            ok = false;
            all = false;
            continue;
        }
        uintptr_t ppn = page2ppn(p[i]);
        if (ppn < nbase || ppn + sz[i] > npage) {
            cprintf("[Buddy]   basic: ppn=0x%lx out of range\n",
                    (unsigned long)ppn);
            ok = false;
        }
        if ((ppn & (sz[i] - 1)) != 0) {
            cprintf("[Buddy]   basic: ppn=0x%lx not aligned to %lu\n",
                    (unsigned long)ppn, (unsigned long)sz[i]);
            ok = false;
        }
        if (PageProperty(p[i]) || PageReserved(p[i])) {
            cprintf("[Buddy]   basic: ppn=0x%lx still marked free/reserved\n",
                    (unsigned long)ppn);
            ok = false;
        }
    }

    if (all) {
        for (int i = 0; i < 3; i ++) {
            for (int j = i + 1; j < 3; j ++) {
                if (bt_overlap(page2ppn(p[i]), sz[i],
                               page2ppn(p[j]), sz[j])) {
                    cprintf("[Buddy]   basic: blocks %d and %d overlap\n", i, j);
                    ok = false;
                }
            }
        }
        if (nr_free_pages() != base - 7) {
            cprintf("[Buddy]   basic: nr_free=%lu expect=%lu\n",
                    (unsigned long)nr_free_pages(),
                    (unsigned long)(base - 7));
            ok = false;
        }
    }

    for (int i = 0; i < 3; i ++) {
        if (p[i] != NULL) {
            free_pages(p[i], req[i]);
        }
    }
    if (nr_free_pages() != base) {
        cprintf("[Buddy]   basic: free did not restore nr_free (%lu != %lu)\n",
                (unsigned long)nr_free_pages(), (unsigned long)base);
        ok = false;
    }

    if (all) {
        struct Page *q = alloc_pages(1);
        if (q != p[0]) {
            cprintf("[Buddy]   basic: reuse returned 0x%lx expect 0x%lx\n",
                    q ? (unsigned long)page2ppn(q) : 0,
                    (unsigned long)page2ppn(p[0]));
            ok = false;
        }
        if (q != NULL) {
            free_pages(q, 1);
        }
    }
    if (nr_free_pages() != base) {
        ok = false;
    }
    bt_report("basic alloc/free", ok);
    return ok;
}

/*
 * Test 3: request a small block while only larger ones are available and
 * verify every order count after the split.  Catches "forgot to split" and
 * per-order accounting mistakes.
 */
static bool
bt_test_split(void) {
    size_t base = nr_free_pages();
    int before[BT_MAX_ORDER + 1];
    int after[BT_MAX_ORDER + 1];
    int target = 3;
    size_t size = (size_t)1 << target;
    int src = -1;
    bool ok = true;

    bt_counts(before);
    for (int order = target; order <= BT_MAX_ORDER; order ++) {
        if (before[order] > 0) {
            src = order;
            break;
        }
    }
    if (src < 0) {
        cprintf("[Buddy]   split: no free block of order >= %d\n", target);
        bt_report("split", false);
        return false;
    }

    struct Page *big = alloc_pages(size);
    if (big == NULL) {
        cprintf("[Buddy]   split: alloc %lu failed\n", (unsigned long)size);
        bt_report("split", false);
        return false;
    }
    uintptr_t ppn = page2ppn(big);
    if ((ppn & (size - 1)) != 0) {
        cprintf("[Buddy]   split: ppn=0x%lx misaligned\n", (unsigned long)ppn);
        ok = false;
    }

    bt_counts(after);
    for (int order = 0; order <= BT_MAX_ORDER; order ++) {
        int expect = before[order];
        if (order >= target && order < src) {
            expect ++;      // right half of each split
        } else if (order == src) {
            expect --;      // the block that was taken
        }
        if (after[order] != expect) {
            cprintf("[Buddy]   split: order %d count=%d expect=%d\n",
                    order, after[order], expect);
            ok = false;
        }
    }
    if (nr_free_pages() != base - size) {
        cprintf("[Buddy]   split: nr_free=%lu expect=%lu\n",
                (unsigned long)nr_free_pages(), (unsigned long)(base - size));
        ok = false;
    }

    free_pages(big, size);
    bt_counts(after);
    if (!bt_counts_equal(after, before)) {
        cprintf("[Buddy]   split: counts not restored after free\n");
        ok = false;
    }
    if (nr_free_pages() != base) {
        ok = false;
    }
    if (!bt_structure("split structure", true)) {
        ok = false;
    }
    bt_report("split", ok);
    return ok;
}

/*
 * Test 4: split a 32-page block into two order-4 buddies, free them one at a
 * time and verify (a) the first free does not merge with the busy buddy and
 * (b) the second free merges into an order-5 (or larger) block and restores
 * the original counts.
 */
static bool
bt_test_merge(void) {
    size_t base = nr_free_pages();
    int before[BT_MAX_ORDER + 1];
    int after[BT_MAX_ORDER + 1];
    size_t half = 16;
    size_t full = 32;
    struct bt_block *snap = bt_snap;
    bool ok = true;

    bt_counts(before);
    struct Page *big = alloc_pages(full);
    if (big == NULL) {
        cprintf("[Buddy]   merge: alloc %lu failed\n", (unsigned long)full);
        bt_report("merge / busy buddy", false);
        return false;
    }
    uintptr_t ppn = page2ppn(big);
    if ((ppn & (full - 1)) != 0) {
        cprintf("[Buddy]   merge: ppn=0x%lx misaligned\n", (unsigned long)ppn);
        ok = false;
    }

    free_pages(big, half);
    int n = bt_snapshot(snap, BT_MAX_BLOCKS);
    int bi = (n < 0) ? -1 : bt_find_block(snap, n, ppn);
    if (!(bi >= 0 && snap[bi].ppn == ppn && snap[bi].size == half)) {
        cprintf("[Buddy]   merge: busy buddy caused a wrong merge at 0x%lx\n",
                (unsigned long)ppn);
        ok = false;
    }
    bt_counts(after);
    if (after[4] != before[4] + 1) {
        cprintf("[Buddy]   merge: order 4 count=%d expect=%d\n",
                after[4], before[4] + 1);
        ok = false;
    }

    free_pages(big + half, half);
    n = bt_snapshot(snap, BT_MAX_BLOCKS);
    bi = (n < 0) ? -1 : bt_find_block(snap, n, ppn);
    if (!(bi >= 0 && snap[bi].size >= full && snap[bi].ppn <= ppn)) {
        cprintf("[Buddy]   merge: buddies did not coalesce at 0x%lx\n",
                (unsigned long)ppn);
        ok = false;
    }
    if (nr_free_pages() != base) {
        cprintf("[Buddy]   merge: nr_free=%lu expect=%lu\n",
                (unsigned long)nr_free_pages(), (unsigned long)base);
        ok = false;
    }
    bt_counts(after);
    if (!bt_counts_equal(after, before)) {
        cprintf("[Buddy]   merge: order counts not restored\n");
        ok = false;
    }
    if (!bt_structure("merge structure", true)) {
        ok = false;
    }
    bt_report("merge / busy buddy", ok);
    return ok;
}

/*
 * Test 5: free a single page and compare the outcome with the buddy rule.
 * Covers a reserved buddy (first managed page), an allocated buddy and, in
 * general, the "same order free head" requirement.  Catches merging with a
 * reserved/foreign/out-of-range buddy.
 */
static bool
bt_test_merge_rule(void) {
    struct bt_block *snap = bt_snap;
    size_t base = nr_free_pages();
    bool ok = true;

    struct Page *p = alloc_pages(1);
    if (p == NULL) {
        cprintf("[Buddy]   merge rule: alloc 1 failed\n");
        bt_report("merge rule / boundaries", false);
        return false;
    }
    uintptr_t ppn = page2ppn(p);
    uintptr_t bidx = ppn ^ 1;
    bool in_range = (bidx >= nbase && bidx < npage);
    bool reserved = in_range && PageReserved(pa2page(bidx << PGSHIFT));
    bool buddy_free = false;

    // the allocation may split a bigger block and create the buddy, so the
    // buddy state has to be sampled *after* the allocation
    int n = bt_snapshot(snap, BT_MAX_BLOCKS);
    if (n < 0) {
        ok = false;
    } else if (in_range && !reserved) {
        for (int i = 0; i < n; i ++) {
            if (snap[i].ppn == bidx && snap[i].size == 1) {
                buddy_free = true;
            }
        }
    }

    free_pages(p, 1);
    n = bt_snapshot(snap, BT_MAX_BLOCKS);
    int ci = (n < 0) ? -1 : bt_find_block(snap, n, ppn);

    if (buddy_free) {
        if (!(ci >= 0 && snap[ci].size >= 2
                && snap[ci].ppn == (ppn & ~(uintptr_t)1))) {
            cprintf("[Buddy]   merge rule: expected merge at 0x%lx\n",
                    (unsigned long)(ppn & ~(uintptr_t)1));
            ok = false;
        }
    } else {
        if (!(ci >= 0 && snap[ci].ppn == ppn && snap[ci].size == 1)) {
            cprintf("[Buddy]   merge rule: unexpected merge at 0x%lx\n",
                    (unsigned long)ppn);
            ok = false;
        }
    }

    if (reserved) {
        cprintf("[Buddy]   merge rule: reserved buddy at 0x%lx not merged\n",
                (unsigned long)bidx);
    } else if (!in_range) {
        cprintf("[Buddy]   merge rule: out-of-range buddy 0x%lx rejected\n",
                (unsigned long)bidx);
    }
    if (nr_free_pages() != base) {
        ok = false;
    }
    bt_report("merge rule / boundaries", ok);
    return ok;
}

/* Test 6: invalid request sizes and non-power-of-two requests. */
static bool
bt_test_sizes(void) {
    bool ok = true;
    struct Page *p = alloc_pages(0);
    if (p != NULL) {
        cprintf("[Buddy]   sizes: alloc_pages(0) returned 0x%lx\n",
                (unsigned long)page2ppn(p));
        ok = false;
    }

    p = alloc_pages(((size_t)1 << BT_MAX_ORDER) + 1);
    if (p != NULL) {
        cprintf("[Buddy]   sizes: oversized request returned 0x%lx\n",
                (unsigned long)page2ppn(p));
        ok = false;
    }

    size_t req[3] = {3, 5, 6};
    for (int i = 0; i < 3; i ++) {
        size_t size = bt_alloc_size(req[i]);
        size_t before = nr_free_pages();
        struct Page *q = alloc_pages(req[i]);
        if (q == NULL) {
            cprintf("[Buddy]   sizes: alloc %lu failed\n",
                    (unsigned long)req[i]);
            ok = false;
            continue;
        }
        uintptr_t ppn = page2ppn(q);
        if ((ppn & (size - 1)) != 0 || PageProperty(q)) {
            cprintf("[Buddy]   sizes: request %lu ppn=0x%lx bad\n",
                    (unsigned long)req[i], (unsigned long)ppn);
            ok = false;
        }
        if (nr_free_pages() != before - size) {
            cprintf("[Buddy]   sizes: request %lu nr_free=%lu expect=%lu\n",
                    (unsigned long)req[i], (unsigned long)nr_free_pages(),
                    (unsigned long)(before - size));
            ok = false;
        }
        free_pages(q, req[i]);
        if (nr_free_pages() != before) {
            cprintf("[Buddy]   sizes: request %lu free did not restore\n",
                    (unsigned long)req[i]);
            ok = false;
        }
    }
    bt_report("invalid and non-power-of-two sizes", ok);
    return ok;
}

/*
 * Test 7: after a free, the page state must no longer be "allocated", so a
 * second free would hit the assert in buddy_free_pages.  We do not trigger
 * the panic on purpose; checking the state documents the guard.
 */
static bool
bt_test_double_free_guard(void) {
    struct Page *p = alloc_pages(2);
    bool ok = (p != NULL);
    if (p == NULL) {
        cprintf("[Buddy]   double free: alloc failed\n");
    } else {
        free_pages(p, 2);
        for (int i = 0; i < 2; i ++) {
            if (buddy_test_page_state(p + i) == 0) {
                cprintf("[Buddy]   double free: page %d still state 0\n", i);
                ok = false;
            }
        }
    }
    bt_report("double-free guard", ok);
    return ok;
}

/*
 * Test 8: allocate every free page (descending block sizes until order 0),
 * verify nr_free reaches 0 and alloc_page() returns NULL, then release all
 * blocks and check the canonical decomposition is restored.
 */
static bool
bt_test_exhaust(void) {
    size_t base = nr_free_pages();
    struct Page **held = bt_held;
    size_t *rq = bt_held_req;
    int cnt = 0;
    bool ok = true;

    for (int order = BT_MAX_ORDER; order >= 0 && cnt < BT_MAX_BLOCKS; order --) {
        size_t size = (size_t)1 << order;
        struct Page *p;
        while (cnt < BT_MAX_BLOCKS && (p = alloc_pages(size)) != NULL) {
            held[cnt] = p;
            rq[cnt] = size;
            cnt ++;
        }
    }

    if (cnt < BT_MAX_BLOCKS) {
        if (nr_free_pages() != 0) {
            cprintf("[Buddy]   exhaust: nr_free=%lu expect=0\n",
                    (unsigned long)nr_free_pages());
            ok = false;
        }
        struct Page *q = alloc_page();
        if (q != NULL) {
            cprintf("[Buddy]   exhaust: alloc_page returned 0x%lx\n",
                    (unsigned long)page2ppn(q));
            ok = false;
            free_pages(q, 1);
        }
    }

    for (int i = 0; i < cnt; i ++) {
        uintptr_t ppn = page2ppn(held[i]);
        if (ppn < nbase || ppn + rq[i] > npage || PageReserved(held[i])) {
            cprintf("[Buddy]   exhaust: block %d ppn=0x%lx bad\n",
                    i, (unsigned long)ppn);
            ok = false;
        }
        for (int j = i + 1; j < cnt; j ++) {
            if (bt_overlap(ppn, rq[i], page2ppn(held[j]), rq[j])) {
                cprintf("[Buddy]   exhaust: blocks %d and %d overlap\n", i, j);
                ok = false;
            }
        }
    }

    for (int i = cnt - 1; i >= 0; i --) {
        free_pages(held[i], rq[i]);
    }
    if (nr_free_pages() != base) {
        cprintf("[Buddy]   exhaust: nr_free=%lu after free expect=%lu\n",
                (unsigned long)nr_free_pages(), (unsigned long)base);
        ok = false;
    }
    if (!bt_structure("exhaust structure", true)) {
        ok = false;
    }
    bt_report("exhaust free list", ok);
    return ok;
}

/* Test 9: fixed-seed alloc/free stress with accessibility and overlap checks. */
static bool
bt_test_stress(void) {
    size_t base = nr_free_pages();
    uint64_t seed = 0x123456789abcdefULL;
    bool ok = true;

    for (int round = 0; round < 100 && ok; round ++) {
        struct Page *p[3];
        size_t req[3];
        size_t sz[3];
        int k = 0;
        size_t expected = base;

        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        int want = 1 + (int)((seed >> 60) % 3);

        for (int j = 0; j < want; j ++) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            size_t n = 1 + (size_t)((seed >> 55) % 64);
            struct Page *q = alloc_pages(n);

            if (q == NULL) {
                cprintf("[Buddy]   stress: round %d alloc %lu failed\n",
                        round, (unsigned long)n);
                ok = false;
                break;
            }
            req[k] = n;
            sz[k] = bt_alloc_size(n);
            p[k] = q;
            k ++;
            expected -= sz[k - 1];

            uintptr_t ppn = page2ppn(q);
            if ((ppn & (sz[k - 1] - 1)) != 0 || PageProperty(q)) {
                cprintf("[Buddy]   stress: round %d ppn=0x%lx bad\n",
                        round, (unsigned long)ppn);
                ok = false;
            }
            if (nr_free_pages() != expected) {
                cprintf("[Buddy]   stress: round %d nr_free=%lu expect=%lu\n",
                        round, (unsigned long)nr_free_pages(),
                        (unsigned long)expected);
                ok = false;
            }

            // the page must be reachable through the direct map
            volatile uint64_t *word =
                (volatile uint64_t *)(uintptr_t)(page2pa(q) + va_pa_offset);
            word[0] = 0x5A5A000000000000ULL + (uint64_t)round;
            if (word[0] != 0x5A5A000000000000ULL + (uint64_t)round) {
                cprintf("[Buddy]   stress: round %d page not writable\n", round);
                ok = false;
            }
        }

        for (int i = 0; i < k; i ++) {
            for (int j = i + 1; j < k; j ++) {
                if (bt_overlap(page2ppn(p[i]), sz[i],
                               page2ppn(p[j]), sz[j])) {
                    cprintf("[Buddy]   stress: round %d blocks %d/%d overlap\n",
                            round, i, j);
                    ok = false;
                }
            }
        }

        // free in a pseudo-random order
        for (int i = k - 1; i > 0; i --) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            int j = (int)(seed % (uint64_t)(i + 1));
            struct Page *tp = p[i]; p[i] = p[j]; p[j] = tp;
            size_t tn = req[i]; req[i] = req[j]; req[j] = tn;
        }
        for (int i = 0; i < k; i ++) {
            free_pages(p[i], req[i]);
        }
        if (nr_free_pages() != base) {
            cprintf("[Buddy]   stress: round %d not restored (%lu != %lu)\n",
                    round, (unsigned long)nr_free_pages(),
                    (unsigned long)base);
            ok = false;
        }
        if (round % 20 == 19 && !bt_structure("stress checkpoint", true)) {
            ok = false;
        }
    }
    bt_report("stress (fixed seed, 100 rounds)", ok);
    return ok;
}

void
buddy_test(void) {
    struct bt_block *baseline = bt_baseline;
    struct bt_block *final = bt_final;
    size_t base;
    int nb;
    bool all = true;

    cprintf("[Buddy] ===== buddy pmm tests =====\n");

    base = nr_free_pages();
    nb = bt_snapshot(baseline, BT_MAX_BLOCKS);
    if (nb < 0) {
        bt_report("baseline snapshot", false);
        return;
    }

    all = bt_test_initial() && all;
    all = bt_test_basic() && all;
    all = bt_test_split() && all;
    all = bt_test_merge() && all;
    all = bt_test_merge_rule() && all;
    all = bt_test_sizes() && all;
    all = bt_test_double_free_guard() && all;
    all = bt_test_exhaust() && all;
    all = bt_test_stress() && all;

    int nf = bt_snapshot(final, BT_MAX_BLOCKS);
    bool restored = (nf >= 0)
                    && bt_snapshot_equal(baseline, nb, final, nf)
                    && (nr_free_pages() == base);
    bt_report("final state restored", restored);
    all = all && restored;

    cprintf("[Buddy] result: %s\n", all ? "ALL PASS" : "FAILURES");
}
