#include <slub_test.h>
#include <slub.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>

/* no <stdbool.h> under -nostdinc */
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

/*
 * Standalone SLUB tests.  Every test frees what it allocated, so the page
 * baseline and the allocator counters must be restored at the end (this
 * implementation releases empty slabs immediately, so no retention quota
 * is needed).
 *
 * Output: "[SLUB] <name>: PASS|FAIL"; failures print an indented detail line
 * with addresses, sizes and counters.
 */

#define ST_MAX_OBJS 256

struct st_obj {
    void *ptr;
    size_t req;     // requested size
    size_t size;    // usable size reported by slub_alloc_size()
    uint8_t seed;   // pattern seed
};

static struct st_obj st_pool[ST_MAX_OBJS];  // cross-slab test
static struct st_obj st_live[ST_MAX_OBJS];  // stress test

static void
st_report(const char *name, bool ok) {
    cprintf("[SLUB] %s: %s\n", name, ok ? "PASS" : "FAIL");
}

static void
st_pattern(void *ptr, size_t size, uint8_t seed) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i ++) {
        p[i] = (uint8_t)(seed + (uint8_t)(i * 131));
    }
}

static bool
st_verify(const void *ptr, size_t size, uint8_t seed) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0; i < size; i ++) {
        if (p[i] != (uint8_t)(seed + (uint8_t)(i * 131))) {
            return false;
        }
    }
    return true;
}

static bool
st_overlap(uintptr_t a, size_t asz, uintptr_t b, size_t bsz) {
    return a < b + bsz && b < a + asz;
}

static bool
st_aligned(const void *p, size_t size) {
    // 8-byte class only guarantees 8-byte alignment, the rest 16
    return size == 8 ? ((uintptr_t)p & 7) == 0
                     : ((uintptr_t)p & 15) == 0;
}

static bool
st_stats_equal(const struct slub_stats *a, const struct slub_stats *b) {
    return a->allocated_objs == b->allocated_objs
        && a->free_objs == b->free_objs
        && a->slab_capacity == b->slab_capacity
        && a->slabs == b->slabs
        && a->used_pages == b->used_pages
        && a->large_objs == b->large_objs
        && a->large_pages == b->large_pages;
}

/*
 * Invariants:
 *   free_objs + (allocated_objs - large_objs) == slab_capacity
 *   used_pages == slabs + large_pages
 */
static bool
st_stats_ok(const char *tag, bool announce) {
    struct slub_stats st;
    slub_get_stats(&st);
    bool ok = true;

    if (st.allocated_objs < st.large_objs) {
        ok = false;
    } else if (st.free_objs + (st.allocated_objs - st.large_objs)
               != st.slab_capacity) {
        ok = false;
    }
    if (st.used_pages != st.slabs + st.large_pages) {
        ok = false;
    }

    if (!ok) {
        cprintf("[SLUB]   %s: allocated=%lu free=%lu capacity=%lu "
                "slabs=%lu used_pages=%lu large=%lu large_pages=%lu\n",
                tag,
                (unsigned long)st.allocated_objs,
                (unsigned long)st.free_objs,
                (unsigned long)st.slab_capacity,
                (unsigned long)st.slabs,
                (unsigned long)st.used_pages,
                (unsigned long)st.large_objs,
                (unsigned long)st.large_pages);
    }
    if (announce) {
        st_report(tag, ok);
    }
    return ok;
}

/* Test 1: every size class, full-object patterns, reuse and page recovery. */
static bool
st_test_classes(void) {
    size_t pages0 = nr_free_pages();
    bool ok = true;

    for (int c = 0; c < 9 && ok; c ++) {
        size_t cls = (size_t)8 << c;
        int n = (cls >= 1024) ? 4 : 8;
        struct st_obj objs[8];

        for (int i = 0; i < n; i ++) {
            objs[i].ptr = NULL;
            void *p = kmalloc(cls);
            size_t asz = slub_alloc_size(cls);
            if (p == NULL || asz != cls) {
                cprintf("[SLUB]   classes: cls=%lu alloc=%p asz=%lu\n",
                        (unsigned long)cls, p, (unsigned long)asz);
                ok = false;
                continue;
            }
            if (!st_aligned(p, asz)) {
                cprintf("[SLUB]   classes: cls=%lu %p misaligned\n",
                        (unsigned long)cls, p);
                ok = false;
            }
            objs[i].ptr = p;
            objs[i].req = cls;
            objs[i].size = asz;
            objs[i].seed = (uint8_t)(c * 31 + i * 7);
            st_pattern(p, asz, objs[i].seed);
        }

        for (int i = 0; i < n; i ++) {
            if (objs[i].ptr == NULL) {
                continue;
            }
            if (!st_verify(objs[i].ptr, objs[i].size, objs[i].seed)) {
                cprintf("[SLUB]   classes: cls=%lu object %d corrupted\n",
                        (unsigned long)cls, i);
                ok = false;
            }
            for (int j = i + 1; j < n; j ++) {
                if (objs[j].ptr != NULL
                        && st_overlap((uintptr_t)objs[i].ptr, objs[i].size,
                                      (uintptr_t)objs[j].ptr, objs[j].size)) {
                    cprintf("[SLUB]   classes: cls=%lu objects %d/%d overlap\n",
                            (unsigned long)cls, i, j);
                    ok = false;
                }
            }
        }

        for (int i = 0; i < n; i ++) {
            if (objs[i].ptr != NULL) {
                kfree(objs[i].ptr);
            }
        }
        if (nr_free_pages() != pages0) {
            cprintf("[SLUB]   classes: cls=%lu pages=%lu expect=%lu\n",
                    (unsigned long)cls, (unsigned long)nr_free_pages(),
                    (unsigned long)pages0);
            ok = false;
        }
        if (!st_stats_ok("classes stats", false)) {
            ok = false;
        }
    }

    if (!st_stats_ok("size classes stats", true)) {
        ok = false;
    }
    st_report("size classes", ok);
    return ok;
}

/* Test 2: sizes around class boundaries and the large-object path. */
static bool
st_test_boundaries(void) {
    static const size_t sizes[] = {
        1, 7, 8, 9, 15, 16, 17, 31, 32, 33,
        63, 64, 65, 127, 128, 129, 255, 256, 257,
        511, 512, 513, 1023, 1024, 1025,
        2047, 2048, 2049, 4095, 4096, 4097, 8191, 8192,
    };
    size_t pages0 = nr_free_pages();
    bool ok = true;

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i ++) {
        size_t req = sizes[i];
        void *p = kmalloc(req);
        size_t asz = slub_alloc_size(req);

        if (p == NULL || asz < req) {
            cprintf("[SLUB]   boundaries: req=%lu ptr=%p asz=%lu\n",
                    (unsigned long)req, p, (unsigned long)asz);
            ok = false;
            if (p != NULL) {
                kfree(p);
            }
            continue;
        }
        if (!st_aligned(p, asz)) {
            cprintf("[SLUB]   boundaries: req=%lu %p misaligned\n",
                    (unsigned long)req, p);
            ok = false;
        }
        if (req <= 2048 && (asz & (asz - 1)) != 0) {
            cprintf("[SLUB]   boundaries: req=%lu class %lu not pow2\n",
                    (unsigned long)req, (unsigned long)asz);
            ok = false;
        }
        st_pattern(p, req, (uint8_t)(i * 13 + 5));
        if (!st_verify(p, req, (uint8_t)(i * 13 + 5))) {
            cprintf("[SLUB]   boundaries: req=%lu pattern mismatch\n",
                    (unsigned long)req);
            ok = false;
        }
        kfree(p);
        if (nr_free_pages() != pages0) {
            cprintf("[SLUB]   boundaries: req=%lu pages=%lu expect=%lu\n",
                    (unsigned long)req, (unsigned long)nr_free_pages(),
                    (unsigned long)pages0);
            ok = false;
        }
        if (!st_stats_ok("boundary stats", false)) {
            ok = false;
        }
    }

    if (kmalloc(0) != NULL) {
        cprintf("[SLUB]   boundaries: kmalloc(0) != NULL\n");
        ok = false;
    }
    if (slub_alloc_size(0) != 0) {
        cprintf("[SLUB]   boundaries: slub_alloc_size(0) != 0\n");
        ok = false;
    }
    // 1 GiB is far above the documented limit
    if (kmalloc((size_t)1 << 30) != NULL) {
        cprintf("[SLUB]   boundaries: oversized allocation succeeded\n");
        ok = false;
    }
    if (slub_alloc_size((size_t)1 << 30) != 0) {
        cprintf("[SLUB]   boundaries: oversized size not rejected\n");
        ok = false;
    }

    if (!st_stats_ok("size boundaries stats", true)) {
        ok = false;
    }
    st_report("size boundaries", ok);
    return ok;
}

/* Test 3: more objects than one slab holds, all freed afterwards. */
static bool
st_test_cross_slab(void) {
    size_t pages0 = nr_free_pages();
    size_t cls = 64;
    int n = 200;
    bool ok = true;

    for (int i = 0; i < n; i ++) {
        void *p = kmalloc(cls);
        if (p == NULL) {
            cprintf("[SLUB]   cross-slab: alloc %d failed\n", i);
            ok = false;
            n = i;
            break;
        }
        st_pool[i].ptr = p;
        st_pool[i].req = cls;
        st_pool[i].size = slub_alloc_size(cls);
        st_pool[i].seed = (uint8_t)(i * 3 + 1);
        st_pattern(p, st_pool[i].size, st_pool[i].seed);
    }

    struct slub_stats st;
    slub_get_stats(&st);
    // (4096 - 48) / 64 = 63 objects per slab -> 200 objects need 4 slabs
    if (ok && st.slabs != 4) {
        cprintf("[SLUB]   cross-slab: slabs=%lu expect=4\n",
                (unsigned long)st.slabs);
        ok = false;
    }
    if (ok && st.allocated_objs != (size_t)n) {
        cprintf("[SLUB]   cross-slab: allocated=%lu expect=%d\n",
                (unsigned long)st.allocated_objs, n);
        ok = false;
    }

    for (int i = 0; i < n; i ++) {
        if (!st_verify(st_pool[i].ptr, st_pool[i].size, st_pool[i].seed)) {
            cprintf("[SLUB]   cross-slab: object %d corrupted\n", i);
            ok = false;
        }
        for (int j = i + 1; j < n; j ++) {
            if (st_overlap((uintptr_t)st_pool[i].ptr, st_pool[i].size,
                           (uintptr_t)st_pool[j].ptr, st_pool[j].size)) {
                cprintf("[SLUB]   cross-slab: objects %d/%d overlap\n", i, j);
                ok = false;
            }
        }
    }

    for (int i = 0; i < n; i ++) {
        kfree(st_pool[i].ptr);
    }
    if (nr_free_pages() != pages0) {
        cprintf("[SLUB]   cross-slab: pages=%lu expect=%lu\n",
                (unsigned long)nr_free_pages(), (unsigned long)pages0);
        ok = false;
    }
    if (!st_stats_ok("cross-slab stats", true)) {
        ok = false;
    }
    st_report("cross slab", ok);
    return ok;
}

/* Test 4: large objects around and above PGSIZE, including page boundaries. */
static bool
st_test_large(void) {
    static const size_t sizes[] = {4095, 4096, 4097, 8192, 16384};
    size_t pages0 = nr_free_pages();
    bool ok = true;

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i ++) {
        size_t req = sizes[i];
        void *p = kmalloc(req);
        size_t asz = slub_alloc_size(req);

        if (p == NULL || asz < req) {
            cprintf("[SLUB]   large: req=%lu ptr=%p asz=%lu\n",
                    (unsigned long)req, p, (unsigned long)asz);
            ok = false;
            if (p != NULL) {
                kfree(p);
            }
            continue;
        }
        if (!st_aligned(p, asz)) {
            cprintf("[SLUB]   large: req=%lu %p misaligned\n",
                    (unsigned long)req, p);
            ok = false;
        }
        st_pattern(p, req, (uint8_t)(i * 23 + 9));
        if (!st_verify(p, req, (uint8_t)(i * 23 + 9))) {
            cprintf("[SLUB]   large: req=%lu pattern mismatch\n",
                    (unsigned long)req);
            ok = false;
        }
        // touch the bytes around the first page boundary explicitly
        if (req > 4100) {
            uint8_t *b = (uint8_t *)p;
            for (size_t off = 4090; off < 4102; off ++) {
                if (b[off] != (uint8_t)((uint8_t)(i * 23 + 9)
                                        + (uint8_t)(off * 131))) {
                    cprintf("[SLUB]   large: req=%lu byte %lu bad\n",
                            (unsigned long)req, (unsigned long)off);
                    ok = false;
                    break;
                }
            }
        }
        kfree(p);
        if (nr_free_pages() != pages0) {
            cprintf("[SLUB]   large: req=%lu pages=%lu expect=%lu\n",
                    (unsigned long)req, (unsigned long)nr_free_pages(),
                    (unsigned long)pages0);
            ok = false;
        }
        if (!st_stats_ok("large stats", false)) {
            ok = false;
        }
    }
    if (!st_stats_ok("large object stats", true)) {
        ok = false;
    }
    st_report("large objects", ok);
    return ok;
}

/* Test 6: the safe rejection paths (double free is a panic path, see below). */
static bool
st_test_exceptions(void) {
    struct slub_stats a, b;
    bool ok = true;

    slub_get_stats(&a);
    kfree(NULL);
    if (kmalloc(0) != NULL) {
        cprintf("[SLUB]   exceptions: kmalloc(0) != NULL\n");
        ok = false;
    }
    if (kmalloc((size_t)1 << 30) != NULL) {
        cprintf("[SLUB]   exceptions: oversized allocation succeeded\n");
        ok = false;
    }

    // both are outside the direct-mapped RAM window and must be rejected
    // without dereferencing anything
    kfree((void *)(uintptr_t)0x1000);
    kfree((void *)(uintptr_t)(va_pa_offset + 0x1000));

    slub_get_stats(&b);
    if (!st_stats_equal(&a, &b)) {
        cprintf("[SLUB]   exceptions: stats changed after invalid frees\n");
        ok = false;
    }
    cprintf("[SLUB]   exception note: double free is detected by assert/panic\n");
    st_report("NULL/invalid pointer handling", ok);
    return ok;
}

static uint64_t
st_next(uint64_t seed) {
    return seed * 6364136223846793005ULL + 1442695040888963407ULL;
}

/* Test 5: mixed-size, fixed-seed stress with live-object verification. */
static bool
st_test_stress(void) {
    size_t pages0 = nr_free_pages();
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    int nlive = 0;
    bool ok = true;

    for (int round = 0; round < 200 && ok; round ++) {
        seed = st_next(seed);
        bool do_alloc = (nlive == 0)
                        || (nlive < ST_MAX_OBJS && ((seed >> 63) & 1) != 0);
        int cnt = 1 + (int)((seed >> 60) % 3);

        if (do_alloc) {
            for (int j = 0; j < cnt && nlive < ST_MAX_OBJS; j ++) {
                seed = st_next(seed);
                size_t req = 1 + (size_t)((seed >> 40) % 4096);
                if (((seed >> 20) & 7) == 0) {
                    req = 8192;     // occasional large object
                }
                void *p = kmalloc(req);
                if (p == NULL) {
                    cprintf("[SLUB]   stress: round %d alloc %lu failed\n",
                            round, (unsigned long)req);
                    ok = false;
                    break;
                }
                size_t asz = slub_alloc_size(req);
                if (asz < req || !st_aligned(p, asz)) {
                    cprintf("[SLUB]   stress: round %d req=%lu bad size/align\n",
                            round, (unsigned long)req);
                    ok = false;
                }
                for (int k = 0; k < nlive; k ++) {
                    if (st_overlap((uintptr_t)p, asz,
                                   (uintptr_t)st_live[k].ptr,
                                   st_live[k].size)) {
                        cprintf("[SLUB]   stress: round %d new object overlaps %d\n",
                                round, k);
                        ok = false;
                    }
                }
                st_live[nlive].ptr = p;
                st_live[nlive].req = req;
                st_live[nlive].size = asz;
                st_live[nlive].seed = (uint8_t)(seed ^ (uint64_t)nlive);
                st_pattern(p, asz, st_live[nlive].seed);
                nlive ++;
            }
        } else {
            for (int j = 0; j < cnt && nlive > 0; j ++) {
                seed = st_next(seed);
                int idx = (int)(seed % (uint64_t)nlive);
                if (!st_verify(st_live[idx].ptr, st_live[idx].size,
                               st_live[idx].seed)) {
                    cprintf("[SLUB]   stress: round %d object %d corrupted\n",
                            round, idx);
                    ok = false;
                }
                kfree(st_live[idx].ptr);
                st_live[idx] = st_live[-- nlive];
            }
        }

        if (!st_stats_ok("stress stats", false)) {
            ok = false;
        }
    }

    for (int i = 0; i < nlive; i ++) {
        if (!st_verify(st_live[i].ptr, st_live[i].size, st_live[i].seed)) {
            cprintf("[SLUB]   stress: object %d corrupted at teardown\n", i);
            ok = false;
        }
        kfree(st_live[i].ptr);
    }
    nlive = 0;

    if (nr_free_pages() != pages0) {
        cprintf("[SLUB]   stress: pages=%lu expect=%lu\n",
                (unsigned long)nr_free_pages(), (unsigned long)pages0);
        ok = false;
    }
    if (!st_stats_ok("stress final stats", true)) {
        ok = false;
    }
    st_report("mixed stress (fixed seed, 200 rounds)", ok);
    return ok;
}

static bool
st_test_final(size_t pages0, const struct slub_stats *base) {
    struct slub_stats st;
    slub_get_stats(&st);
    bool ok = (nr_free_pages() == pages0) && st_stats_equal(&st, base);
    if (!ok) {
        cprintf("[SLUB]   final: pages=%lu expect=%lu\n",
                (unsigned long)nr_free_pages(), (unsigned long)pages0);
        cprintf("[SLUB]   final: allocated=%lu/%lu free=%lu/%lu slabs=%lu/%lu\n",
                (unsigned long)st.allocated_objs,
                (unsigned long)base->allocated_objs,
                (unsigned long)st.free_objs,
                (unsigned long)base->free_objs,
                (unsigned long)st.slabs,
                (unsigned long)base->slabs);
    }
    st_report("final state restored", ok);
    return ok;
}

void
slub_test(void) {
    struct slub_stats base;
    size_t pages0;
    bool all = true;

    cprintf("[SLUB] ===== slub tests =====\n");
    pages0 = nr_free_pages();
    slub_get_stats(&base);

    all = st_test_classes() && all;
    all = st_test_boundaries() && all;
    all = st_test_cross_slab() && all;
    all = st_test_large() && all;
    all = st_test_exceptions() && all;
    all = st_test_stress() && all;
    all = st_test_final(pages0, &base) && all;

    cprintf("[SLUB] result: %s\n", all ? "ALL PASS" : "FAILURES");
}
