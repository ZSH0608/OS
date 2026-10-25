#include <pmm.h>
#include <list.h>
#include <best_fit_pmm.h>
#include <stdio.h>

//lab2 function2-2 2412888

static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void best_fit_check(void);

/* *
 * best_fit_insert_free_block - insert a free block into the free list,
 * keeping the list ordered by physical address (base is a Page pointer).
 * */
//1.按地址升序插入空闲块
static void
best_fit_insert_free_block(struct Page *base) {
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (base < p) {
            list_add_before(le, &(base->page_link));
            return;
        }
    }
    // larger than every block: append at the tail
    list_add_before(&free_list, &(base->page_link));
}

//2.初始化空闲链表，此时还没有空闲页
static void
best_fit_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

//3.从描述符 base 起，连续 n 个物理页，全部变为一个空闲块。 
static void
best_fit_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));//要求进入前仍是 reserved（page_init 先全部标reserved保留）
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    // only the first page of a block carries the block size
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    best_fit_insert_free_block(base);
}

//4.按 Best-Fit 分配，若成功：返回所分配连续块的首页 struct Page *
static struct Page *
best_fit_alloc_pages(size_t n) {
    assert(n > 0);
    //4.1 总空闲页不够，直接失败（不必遍历链表）。
    if (n > nr_free) {
        return NULL;
    }

    // 4.2 选「最小且够大」的块
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n
                && (page == NULL || p->property < page->property)) {
            page = p;
        }
    }
    if (page == NULL) {
        return NULL;
    }

    // 4.3 拆分并更新链表
    list_entry_t *prev = list_prev(&(page->page_link));
    list_del(&(page->page_link));
    if (page->property > n) {
        struct Page *p = page + n;
        p->property = page->property - n;
        SetPageProperty(p);
        list_add(prev, &(p->page_link));
    }
    nr_free -= n;
    ClearPageProperty(page);
    return page;
}

//5.用完释放块，变成空闲块，要合并相邻空闲块
static void
best_fit_free_pages(struct Page *base, size_t n) {
    //5.1 清这 n 页的状态，再建一个空闲块头
    assert(n > 0);//不允许释放 0 页
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));//要求：非 reserved、当前不是空闲块头（避免重复释放等错误状态）
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;//新空闲块大小
    SetPageProperty(base);//标记「这是空闲块头」。
    nr_free += n;//总空闲页数增加。
    best_fit_insert_free_block(base);//按地址升序插入 free_list（空闲链表）

    // 5.2 与前一块合并
    list_entry_t *le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }

    // 5.3 与后一块合并
    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

//6.返回当前空闲页数
static size_t
best_fit_nr_free_pages(void) {
    return nr_free;
}

/*
pmm_manager 结构体：
name: 算法名称
init: 初始化
init_memmap: 初始化内存映射
alloc_pages: 分配页面
free_pages: 释放页面
nr_free_pages: 返回当前空闲页数
check: 检查算法正确性

把六个函数填进 struct pmm_manager。
pmm.c 里 pmm_manager = &best_fit_pmm_manager 后，所有 alloc_pages / free_pages 等都会走到上面的实现。
*/
const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_check,
};

// Framework tests: students must not modify basic_check or best_fit_check.
static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

static void
best_fit_check(void) {
    int score = 0, sumscore = 6;
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // * - - * -
    free_pages(p0 + 1, 2);
    free_pages(p0 + 4, 1);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
    // * - - * *
    assert((p1 = alloc_pages(1)) != NULL);
    assert(alloc_pages(2) != NULL);
    assert(p0 + 4 == p1);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    p2 = p0 + 1;
    free_pages(p0, 5);
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count--, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
    #ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
    #endif
}
