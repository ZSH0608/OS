#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <buddy_test.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <test_lab2.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
#ifndef PMM_USE_BUDDY
#define PMM_USE_BUDDY 0
#endif

static void init_pmm_manager(void) {
#if PMM_USE_BUDDY
    pmm_manager = &buddy_pmm_manager;
#else
    pmm_manager = &best_fit_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE

/* *
 * page_init - build the page descriptor array and free the usable memory.
 *
 * Address conversions used below (see pmm.h):
 *   virt addr  --PADDR (subtract va_pa_offset)-->  phys addr, in bytes
 *   phys addr  --PPN   (>> PGSHIFT)------------->  physical page number
 *   struct Page* --pa2page(phys addr)----------->  descriptor pointer
 * so pages[] is addressed by virtual addresses while page2pa/pa2page work
 * with physical addresses and page numbers.
 * */
static void page_init(void) {
    // 1. DRAM is direct-mapped at this offset; needed by PADDR from now on
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 2. physical memory range reported by the device tree
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t mem_end = mem_base + mem_size;
    if (mem_base == 0 || mem_size == 0 || mem_end <= mem_base
            || mem_end > PHYSICAL_MEMORY_END) {
        panic("page_init: invalid memory range [0x%lx, 0x%lx)",
              (unsigned long)mem_base, (unsigned long)mem_end);
    }

    // 3. one descriptor per physical page below mem_end
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: npage %lu <= nbase %lu",
              (unsigned long)npage, (unsigned long)nbase);
    }

    // 4. the descriptor array starts after the kernel image, page aligned
    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_pa = PADDR((uintptr_t)pages);
    size_t nr_page = npage - nbase;
    if (pages_pa + nr_page * sizeof(struct Page) > mem_end) {
        panic("page_init: page array overflows physical memory");
    }

    // 5. reserve every descriptor first: OpenSBI, the kernel image and the
    //    page array itself must never be handed out by the allocator
    for (size_t i = 0; i < nr_page; i ++) {
        struct Page *p = pages + i;
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        SetPageReserved(p);
    }

    // 6. first physical page right after the page descriptor array
    uintptr_t freemem = ROUNDUP(pages_pa + nr_page * sizeof(struct Page), PGSIZE);

    // 7. there must be at least one complete page left to manage
    if (freemem >= mem_end || PPN(freemem) >= npage) {
        panic("page_init: no free page, freemem=0x%lx mem_end=0x%lx",
              (unsigned long)freemem, (unsigned long)mem_end);
    }

    // 8. report the detected physical memory layout
    cprintf("Physical Memory: base=0x%016lx, size=0x%016lx, end=0x%016lx\n",
            (unsigned long)mem_base, (unsigned long)mem_size,
            (unsigned long)mem_end);
    cprintf("npage: %lu\n", (unsigned long)npage);
    cprintf("pages: va=0x%016lx, pa=0x%016lx\n",
            (unsigned long)(uintptr_t)pages, (unsigned long)pages_pa);
    cprintf("free memory: pa=0x%016lx, size=0x%016lx, %lu pages\n",
            (unsigned long)freemem, (unsigned long)(mem_end - freemem),
            (unsigned long)((mem_end - freemem) / PGSIZE));

    // 9. hand [freemem, mem_end) over to the current pmm manager
    init_memmap(pa2page(freemem), (mem_end - freemem) / PGSIZE);
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);

#if PMM_USE_BUDDY
    buddy_test();
#endif

    test_page_init();
    test_best_fit();
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
