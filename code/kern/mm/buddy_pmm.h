#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

// 2^15 pages = 128 MiB with 4 KiB pages
#define BUDDY_MAX_ORDER 15

extern const struct pmm_manager buddy_pmm_manager;

/*
 * Read-only inspection hooks used by buddy_test.c.  They do not change the
 * pmm_manager interface and are never used on the allocation path.
 */
int buddy_test_order_count(int order);
struct Page *buddy_test_order_block(int order, int index);
int buddy_test_page_state(struct Page *page);

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
