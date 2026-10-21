# ucore Lab2 物理内存管理与 SLUB —— 算法实现提示词

> 用途：把本文件整段交给 AI 或开发者，作为 `lab2_new` 仓库的实现任务说明。
> 本文件只描述“实现什么、遵守什么约束”，测试设计见同目录的 `test_prompt.md`。

---

## 0. 角色与背景

你是一名 RISC-V 操作系统内核开发者，在基于 ucore 的 lab2 代码库中工作（仓库根目录执行所有命令）。

环境事实：

- 构建/运行：`make`、`make test`、`make qemu`、`make grade`。
- 工具链：`riscv64-unknown-elf-*`；模拟器：`qemu-system-riscv64`。
- 启动流程：`kern/init/entry.S` 建立 Sv39 的 1 GiB 大页映射：
  - 虚拟 `0xFFFFFFFFC0000000` → 物理 `0x80000000`；
  - 内核虚拟基址 `0xFFFFFFFFC0200000`，物理基址 `0x80200000`；
  - `PHYSICAL_MEMORY_OFFSET = 0xFFFFFFFF40000000`；
  - QEMU virt 物理内存范围 `0x80000000 ~ 0x88000000`（128 MiB）。
- 物理内存设施：`pages`、`npage`、`nbase`、`va_pa_offset`、`struct Page`（`ref/flags/property/page_link`）、`PG_reserved/PG_property`、`page2pa/pa2page/PADDR/ROUNDUP`。
- `KADDR` 在 `pmm.h` 中被注释；需要物理地址转内核虚拟地址时用
  `(void *)(pa + va_pa_offset)`（即 `KADDR` 的等价形式）。
- 参考实现：`kern/mm/default_pmm.c` 是完整可用的 First-Fit。
- 评分文件：`tools/test_lab2.c`、`tools/inject.py`、`tools/grade.sh` 只读；
  `make grade` 会把测试注入到临时副本的 `pmm_init()` 末尾。

## 1. 通用工作规则

1. 先阅读任务指定文件，复述现有约定、可复用接口和风险点，再设计，不要凭空猜接口。
2. 只修改任务明确允许的文件；不得修改 `basic_check`、`default_check`、`best_fit_check`、`tools/`、Makefile 评分逻辑。
3. C99、`-nostdinc`、`-Werror`，只能使用 `libs/` 与 `kern/` 已有设施，不引入 libc。
4. 先给出设计（数据结构、算法步骤、地址/单位转换、边界处理），再给最小可编译实现。
5. 完成后运行任务指定的验证命令；失败就分析原因再改，不要靠改测试绕过。
6. 汇报必须包含：修改文件、关键不变量、复杂度、边界情况、测试输出、可写进实验报告的要点。
7. 若框架约束与要求冲突，先指出冲突并给出兼容方案，不要擅自修改公共接口。
8. 如果本机缺少工具链或 QEMU，明确说明，并给出逐条验证清单与预期输出，不要假装运行成功。

---

## 2. 任务一：实现 `kern/mm/pmm.c` 的 `page_init()`

`pmm_init()` 已经调用 `page_init()`，但骨架未定义它。

要求：

1. 先设置 `va_pa_offset = PHYSICAL_MEMORY_OFFSET`，之后才能使用 `PADDR`。
2. 从 DTB 读取 `mem_base`、`mem_size`（`get_memory_base/get_memory_size`）。
   若 `mem_base == 0`、`mem_size == 0`、`mem_end <= mem_base`，或 `mem_end` 超出映射/可管理上界
   （`PHYSICAL_MEMORY_END`），用 `panic()` 终止。
3. 计算 `npage = PPN(mem_base + mem_size)`。评分测试期望的正是这个值，不要自行截断或四舍五入。
4. `pages` 指向内核链接符号 `end` 之后、按 `PGSIZE` 向上对齐的虚拟地址。
   确认 `(npage - nbase)` 个 `struct Page` 的空间不越界；`npage <= nbase` 要 panic。
5. 把 `pages` 数组覆盖的所有页描述符先全部初始化为 reserved：
   `flags` 置 `PG_reserved`、`ref=0`、`property=0`。
   必须先完成这一步，因为 `init_memmap` 内部会 `assert(PageReserved(p))`。
6. 计算空闲区物理起点：
   `freemem = ROUNDUP(PADDR(pages) + sizeof(struct Page) * (npage - nbase), PGSIZE)`
   `PADDR` 的输入必须是内核虚拟地址，结果是字节。
7. 若 `freemem >= mem_end` 或 `PPN(freemem) >= npage`：打印错误并 panic，绝不调用 `init_memmap()`。
8. 否则 `init_memmap(pa2page(freemem), (mem_end - freemem) / PGSIZE)`。
9. 用 `cprintf` 输出物理内存范围、`npage`、`pages` 的虚拟/物理地址、`freemem` 等；
   格式符与参数类型严格匹配，避免 `-Werror`。
10. OpenSBI（`0x80000000~0x80200000`）和内核镜像都位于 `freemem` 之前，保持 reserved，不要释放。

必须说明的地址转换（实现前后各给一张对照表）：

| 转换 | 手段 | 单位 |
|---|---|---|
| 内核虚拟 → 物理 | `PADDR(kva) = kva - va_pa_offset` | 字节 |
| 物理 → PPN | `PPN(pa) = pa >> PGSHIFT` | 页 |
| PPN → 物理 | `ppn << PGSHIFT` | 字节 |
| `struct Page*` → 物理 | `page2pa(p) = (p - pages + nbase) << PGSHIFT` | 字节 |
| 物理 → `struct Page*` | `pa2page(pa) = &pages[PPN(pa) - nbase]` | 指针 |

验收：`make` 通过且无警告；`make grade` 输出 `[Test 1] Page Init: PASS`
（若 Best-Fit 尚未实现，Test 2 可能 FAIL，属正常）。

> 已知框架冲突：未实现 Best-Fit 时，`best_fit_pmm.c` 的检查函数引用 `free_list/nr_free`，
> 且评分测试引用 `best_fit_pmm_manager`，整仓无法链接。应先完成任务二，或在临时副本中加最小脚手架，
> 不要为了通过编译去改框架测试。

---

## 3. 任务二：实现 Best-Fit 并接入

文件：`kern/mm/best_fit_pmm.c`、`kern/mm/pmm.c`。

1. 在 `best_fit_pmm.c` 内定义自己的 `static free_area_t` 和宏 `free_list`、`nr_free`。
2. `best_fit_init`：初始化空闲链表，`nr_free=0`。
3. `best_fit_init_memmap`：与 `default_init_memmap` 相同约定：
   - `assert(n>0)`、逐页 `assert(PageReserved(p))`；
   - 清 `flags/property/ref`；
   - 只在块首页 `property=n` + `SetPageProperty`；
   - `nr_free += n`；
   - 按物理地址升序插入。
4. `best_fit_alloc_pages(n)`：
   - `assert(n>0)`，`n > nr_free` 返回 NULL；
   - 遍历整个空闲链表，在 `property >= n` 中选 `property` 最小者；
   - 相同大小取地址最小（链表中先遇到）者，即严格 `<` 比较；
   - 删除整块；若 `property > n`，剩余部分作为新块按地址顺序插回；
   - `nr_free -= n`，清除首页 `PG_property`，返回块首页。
5. `best_fit_free_pages(base,n)`：
   - 逐页 `assert(!PageReserved && !PageProperty)`；
   - 首页 `property=n` + `SetPageProperty`，`nr_free += n`，按地址插入；
   - 先与前块合并（`p + p->property == base`），再与后块合并；
   - 合并只保留新块首页的 `property`，清除被吞并块的 `PG_property`。
6. `best_fit_nr_free_pages`。
7. 定义 `best_fit_pmm_manager`（六个回调，`.check = best_fit_check`）。
8. 在 `pmm.c` 的 `init_pmm_manager()` 中把 `&default_pmm_manager` 改为 `&best_fit_pmm_manager`，
   并包含 `best_fit_pmm.h`（本任务唯一允许修改的其他文件）。

关键理解题：为什么 `best_fit_check` 中先 `free_pages(p0+1,2)`、`free_pages(p0+4,1)`，
再 `alloc_pages(1)` 必须返回 `p0+4`？

- 此时空闲块是 `[p0+1, 2 页]` 与 `[p0+4, 1 页]`；
- Best-Fit 取“最小满足块”，选 1 页块 `p0+4`；
- First-Fit 按地址取第一个满足块，会返回 `p0+1`；
- 因此该断言正是两者的判别器。

验收：`make test` 中 `grading: 6 / 6 points`、`check_alloc_page() succeeded!`；
`make grade` 为 `[Test 1] Page Init: PASS`、`[Test 2] Best-Fit: PASS`，总分 `20/20`。

---

## 4. 任务三：实现 Buddy System

文件：新增 `kern/mm/buddy_pmm.c` / `buddy_pmm.h`；`kern/mm/pmm.c` 增加编译开关。
不要修改 best_fit/default 模块。

### 4.1 数据结构

- `BUDDY_MAX_ORDER = 15`（2^15 页 = 32768 页 = 128 MiB）。
- `static free_area_t buddy_free_area[BUDDY_MAX_ORDER + 1]`：每阶一条按物理地址升序的链表，
  `nr_free` 存该阶**块数**。
- 总空闲页计数 `buddy_nr_free` 供 `nr_free_pages()` 返回。
- 空闲块首页 `property = 2^order` + `PG_property`；块内其余页 `flags=0, property=0`。
- `flags/property` 无法区分“已分配页”和“空闲块内部页”，可在 `buddy_pmm.c` 内增加
  静态元数据数组（按页索引保存 order/state）。要求说明内存开销与初始化方式。
  示例：`static uint8_t buddy_state[32768]`，`0`=已分配/未管理，`1..16`=某阶空闲块首页，
  `0xFF`=空闲块内部页；`buddy_init()` 中 `memset` 清零。不要修改 `struct Page`。

### 4.2 初始化

`init_memmap(base, n)` 的初始区间不一定按大块对齐，必须拆成“按**绝对物理页号**对齐”的 2^k 块：

- 从 `base` 开始，每一步取最大块：`2^k <= remaining`、`idx % 2^k == 0`、`k <= 15`；
- 块内所有页清 reserved、`ref=0`、`property=0`；只有首页 `property=2^k` + `SetPageProperty`；
- 插入对应阶链表，更新该阶块数与总页数。

### 4.3 分配

- `n == 0` 或 `n > 2^15` 返回 NULL。
- `order = ceil(log2(n))`。
- 从 order 到 15 找第一个非空链表；都空返回 NULL。
- 取块后反复分裂：后半 `block + 2^(cur-1)` 作为低一阶空闲块插入低阶链表，
  前半继续向下分裂，直到目标阶。
- 返回目标块首页，清 `PG_property`，全块 `ref=0`。
- `nr_free` 按实际分配的 `2^order` 页扣减（不是按 n），代码注释中明确；
  `free_pages` 使用同样的向上取整规则。

### 4.4 释放与合并

- `order = ceil(log2(n))`，`base` 必须按 `2^order` 页对齐。
- 逐页校验元数据为 ALLOC、非 reserved、非 `PG_property`（拦截重复/重叠/内部页释放）。
- 标记为空闲块（首页 `property=2^order`），总计数加 `2^order`。
- 循环：
  - `buddy_idx = idx ^ (1 << order)`；
  - 要求 `buddy_idx ∈ [nbase, npage)` 且整块不越过 `npage`；
  - buddy 非 reserved；
  - buddy 是同级空闲块首页（元数据 `order+1`）且 `property == 1<<order`；
  - 合并后取地址较小者为新首页，清另一首页的 `PG_property`，`order++` 继续。
- 最后插入对应阶链表，更新该阶计数。
- 必须处理：buddy 在受管区外、buddy 已分配、合并超过 15、重复/非法释放（assert 或元数据校验）。

### 4.5 结构自检

实现 `buddy_check()`：
- 各阶链表按地址有序；
- 每个块页数为 2 的幂、首页 `property` 正确、起始页号按块大小对齐；
- 不同阶/同阶块不重叠；
- 所有空闲块页数之和 == `nr_free_pages()`。

不要修改 `best_fit_check`。

### 4.6 接入

`kern/mm/pmm.c`：

```c
#include <buddy_pmm.h>

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
```

本地验证命令：

```bash
make clean
make DEFS=-DPMM_USE_BUDDY=1 qemu        # 或 make DEFS=-DPMM_USE_BUDDY=1 test
```

注意：`make` 不跟踪 `DEFS` 变化，切换配置前必须先 `make clean`。

验收：默认（不传 DEFS）与 `PMM_USE_BUDDY=1` 都能编译；默认 `make grade` 仍为 20/20；
buddy 模式启动时 `check_alloc_page() succeeded!`。

汇报需包含算法复杂度（设 B 为空闲块数）与改进方向（分离链表、按大小索引、延迟合并等）。

---

## 5. 任务四：实现简化 SLUB

文件：新增 `kern/mm/slub.c` / `slub.h`；在 `kern/init/init.c` 的 `pmm_init()` 之后调用
`slub_init()`。不要修改 best_fit/default 与框架评分测试。

### 5.1 两层架构

- 第一层：`alloc_pages(n)` / `free_pages(base, n)`。
- 第二层：把页块切成固定大小对象，提供 `kmalloc(size_t)` / `kfree(void*)`。

### 5.2 尺寸类与 cache

- 尺寸类：8、16、32、64、128、256、512、1024、2048。
- 每个 cache：`obj_size`、`objs_per_slab`、`nr_slabs`、`free_objs`、
  `partial`（有空闲对象）与 `full` 两条链表。
- 空闲对象用对象前 8 字节存 next；说明与位图的取舍。

### 5.3 slab 头

每个 slab 的首页页首放 `struct slab` 头：magic、cache_id、order、obj_size、
nr_objs、nr_free、first_obj、freelist、prev/next。建议 48 字节，首个对象 16 字节对齐。

大于最大尺寸类的分配走大对象路径：申请 `2^order` 个连续页，首页页首放大对象头
（magic、order、大小、类型），返回首页内对齐后的 payload。

`kfree` 从任意 `kmalloc` 返回的指针定位元数据：把指针按 `PGSIZE` 向下对齐后读取页首 magic。
这要求 payload 必须从首页内部开始（本设计为首页偏移 48）。

地址转换：

```c
static inline void *page_to_kva(struct Page *page) {
    return (void *)(uintptr_t)(page2pa(page) + va_pa_offset);
}
```

说明它等价于 `KADDR`：`KADDR(pa) = pa + va_pa_offset` 并校验 `PPN < npage`；
这里 `page2pa` 的输入来自合法页描述符，物理地址必然有效。

### 5.4 kmalloc / kfree

- `size == 0` 返回 NULL（明确约定）；超过上限返回 NULL。
- 向上取整到尺寸类；优先从 partial slab 分配；没有则 `alloc_pages(1)` 建新 slab。
- 返回地址至少 8 字节对齐（类 ≥16 与大对象为 16 对齐）；不清零。
- 维护统计：已分配对象数、空闲对象数、slab 数、已用页数。
- `kfree(NULL)` 直接返回。
- 校验 magic；把对象挂回空闲链表；slab 从满变 partial；空 slab 的回收策略必须说明
  （可立即 `free_pages`，也可保留一个；保证测试能观察页数恢复或给出保留上限）。
- 非法指针、重复释放至少 assert/打印拒绝，避免静默破坏结构。

### 5.5 接入与限制

- `slub_init()` 在 `pmm_init()` 之后调用。
- 不改变默认 `pmm_manager` 选择，保证 `make grade` 20/20。
- 不引入 libc；可用 `memset/memcpy`。
- 说明并发/中断不在本实验范围；真实内核需要 per-CPU cache、自旋锁/关中断、跨 CPU 远端释放队列。

验收：`make` 通过；默认 `make grade` 20/20；能说明 `kmalloc/kfree` 复杂度，
以及尺寸类、对齐、内部碎片、页回收之间的权衡。

---

## 6. 统一验收与汇报格式

验收命令（按任务裁剪）：

```bash
make clean && make
make clean && make DEFS=-DPMM_USE_BUDDY=1
make clean && make DEFS="-DSLUB_TEST=1"
make grade
```

汇报必须分节：

1. 修改/新增了哪些文件；
2. 关键数据结构与不变量；
3. 地址与单位转换；
4. 复杂度与空间开销；
5. 边界情况清单；
6. 实际测试输出；
7. 可直接写进实验报告的要点；
8. 框架冲突与兼容方案（如有）。
