# 操作系统实验报告

## 目录

- [实验基本信息](#实验基本信息)
- [一、实验目的](#一实验目的)
- [二、实验环境](#二实验环境)
- [三、实验整体逻辑分析](#三实验整体逻辑分析)
  - [3.1 本章节的逻辑主线](#31-本章节的逻辑主线)
  - [3.2 功能的逐步实现](#32-功能的逐步实现)
- [四、实验内容与实现](#四实验内容与实现)
  - [4.1 功能模块一：物理内存初始化](#41-功能模块一物理内存初始化)
  - [4.2 功能模块二：物理内存分配](#42-功能模块二物理内存分配)
- [五、测试与验证](#五测试与验证)
  - [gdb 调试页表查询过程](#gdb-调试页表查询过程)
  - [Buddy 测试套件](#buddy-测试套件pmm_use_buddy1)
  - [SLUB 测试套件](#slub-测试套件slub_test1)
  - [使用提示词让 AI 生成代码过程中出现的 bug 及解决](#使用提示词让-ai-生成代码过程中出现的-bug-及解决)
- [六、实验总结与收获](#六实验总结与收获)
- [七、知识点总结](#七知识点总结)

---

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 2: 物理内存和页表 |
| **小组成员** | 2413157-张书晗、2411514-蒲富坤、2412888-刚栋冰 |
| **完成日期** | 2026-10-24 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2413157-张书晗 | 功能模块一（page_init）+ 功能模块二 任务二（Best-Fit） |
| 2411514-蒲富坤 | 功能模块二 任务三（Buddy System）+ 扩展任务 Challenge（SLUB） |
| 2412888-刚栋冰 | 功能模块二 任务一（First-Fit 理解）+ Buddy/SLUB 测试套件 + 环境适配 |

实验报告如何分工？

张书晗搭建了本次实验的代码框架，完成物理内存探测（`page_init`）与 Best-Fit 分配器，负责"三、实验整体逻辑分析"与"功能模块一"的撰写；蒲富坤实现 Buddy 伙伴系统与简化 SLUB 对象分配器，负责"功能模块二"中任务三与 SLUB 部分的撰写；刚栋冰负责 First-Fit 算法分析、Buddy/SLUB 两套独立测试套件的设计与 QEMU 10.2 环境适配，并整理"五、测试与验证"与"七、知识点对照"。

---

## 一、实验目的

1. 理解页表的建立和使用方法，理解物理内存的管理方法，理解页面分配算法。
2. 掌握物理内存的发现、初始化和分配流程：从设备树（DTB）获取物理内存范围，用 `page_init()` 建立物理页描述符（`struct Page`），并把可用页面交给物理内存管理器。
3. 在理解 First-Fit 分配流程的基础上，实现并接入 Best-Fit 分配器，掌握连续物理内存分配算法的差异与取舍。
4. 通过 Buddy System 与 SLUB 扩展任务，进一步理解页级分配（伙伴系统）与任意大小内存对象分配（slab/slub）的两层架构。
5. 继续实践"描述需求—生成—验证—迭代"的 AI 协作开发方式，学会为算法实现与测试分别编写可验证的 prompt。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2413157-张书晗 | Codex（Cursor插件），Claude Code（终端 Agent） | gpt-6 Astra, Claude Fable 5.1 | |
| 2411514-蒲富坤 | Claude Code（终端 Agent）, Codex（VS Code插件） | gpt-6 Astra, deepseek-v4-pro | |
| 2412888-刚栋冰 | Claude Code（终端 Agent）, Codex（VS Code插件） | gpt-6 Astra, Claude Fable 5.1 | |

构建环境：交叉工具链 `riscv64-unknown-elf-*`（10.2.0）、模拟器 `qemu-system-riscv64`（10.2），均在 WSL 中运行。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

阅读了本章节的实验指导手册后，我们自己绘制了下框架图：

```
物理地址（DRAM，默认 128MiB；低 → 高）

低地址  ──────────────────────────────────────────────────────┐
      │ OpenSBI（reserved）                                   │  PA = 0x80000000 = mem_base
      │                                                      │  nbase = PPN(DRAM_BASE)
      ├──────────────────────────────────────────────────────┤  PA = 0x80200000
      │ 内核镜像：代码/数据/BSS（reserved）                      │
      │                                                      │
      ├──────────────────────────────────────────────────────┤  KernelEnd = PADDR(end)
      │ 页对齐空隙（ROUNDUP 到 PGSIZE）                         │  ← end 是虚拟地址，须用 PADDR
      ├──────────────────────────────────────────────────────┤  pages 起点（VA 页对齐后的 PA）
      │ Page 描述符数组 pages[]（reserved）                     │  覆盖物理页号 [nbase, npage)
      │   pages[i] ↔ 物理页 (nbase + i)                       │
      ├──────────────────────────────────────────────────────┤  pages 数组物理结束
      │ 页对齐空隙（ROUNDUP 到 PGSIZE）                         │
      ├──────────────────────────────────────────────────────┤  freemem（空闲区物理起点）
      │ 可分配连续物理页（交给 init_memmap / pmm_manager）    │  页数 = (mem_end - freemem)/PGSIZE
      │ ……                                                   │
      │ （末尾不足一页的残余不交给分配器）                         │
高地址 └──────────────────────────────────────────────────────┘  PA = mem_end（默认 0x88000000）
                                                                   npage = PPN(mem_end)（页号上界，exclusive）

```

整体实现链可以概括为：

```
pmm_init() -> init_pmm_manager() -> page_init() -> init_memmap()
           -> alloc_pages()/free_pages()
```

分层的关键在于 `pmm_manager` 这一抽象：上层（`alloc_pages`/`free_pages`/SLUB）只依赖六个回调，不关心底层用的是 First-Fit、Best-Fit 还是 Buddy；底层算法各自维护自己的空闲链表/数组，只需实现同一套接口。这样我们可以在一个实验里同时实现并切换三种分配器，用同一套 `check` 自检逻辑验证它们。

### 3.2 功能的逐步实现

从开机到第一次分配内存:

#### 第一层：Sv39 大页映射（`kern/init/entry.S`）

内核启动时，`entry.S` 用一张预置的三级页表把虚拟地址 `0xFFFFFFFFC0000000` 映射到物理地址 `0x80000000`（1 GiB 大页），随后 `csrw satp` 启用 Sv39 分页：

```asm
    lui     t0, %hi(boot_page_table_sv39)
    li      t1, 0xffffffffc0000000 - 0x80000000
    sub     t0, t0, t1          # 页表物理地址
    srli    t0, t0, 12          # 物理页号
    li      t1, 8 << 60         # MODE = Sv39
    or      t0, t0, t1
    csrw    satp, t0
    sfence.vma
```

从此内核运行在虚拟地址空间，物理地址通过 `PHYSICAL_MEMORY_OFFSET = 0xFFFFFFFF40000000` 直接映射（`KERNBASE = 0x80200000 + offset`）。

#### 第二层：DTB 解析（`kern/driver/dtb.c`）

QEMU 启动时把设备树（DTB）的物理地址通过 `a1` 寄存器传给内核，`entry.S` 将其保存到 `boot_dtb`。`dtb_init()` 解析 DTB 的 `memory` 节点，取出 `reg` 属性中的 `base` 与 `size`，得到物理内存范围 `0x80000000 ~ 0x88000000`（128 MiB），供 `page_init` 查询。

#### 第三层：page_init 建立页描述符（`kern/mm/pmm.c`）

```c
// 1. 设置虚实偏移
va_pa_offset = PHYSICAL_MEMORY_OFFSET;

// 2. 从 DTB 读物理内存范围
uint64_t mem_base = get_memory_base();
uint64_t mem_size = get_memory_size();
uint64_t mem_end = mem_base + mem_size;
if (mem_base == 0 || mem_size == 0 || mem_end <= mem_base
      || mem_end > PHYSICAL_MEMORY_END) {
   panic("page_init: invalid memory range [0x%lx, 0x%lx)",
         (unsigned long)mem_base, (unsigned long)mem_end);
}

// 3. 算总页数 npage
npage = PPN(mem_end); // PPN() 地址右移 12 位 → 页号
if (npage <= nbase) {
   panic("page_init: npage %lu <= nbase %lu",
         (unsigned long)npage, (unsigned long)nbase);
}

// 4. 放置 pages[] 数组
extern char end[];

//ROUNDUP(..., PGSIZE),从 end 起向上对齐到页边界，当作 pages 起点
//ROUNDUP(a, n) : a 向上对齐到 n 的倍数（页对齐常用)
pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

//pages_pa : 这个数组在物理内存里的起始地址（PADDR）
uintptr_t pages_pa = PADDR((uintptr_t)pages); //PADDR(kva) 内核虚拟地址 → 物理地址

//nr_page : 页数差，pages[] 从 nbase 开始，到 npage 结束
size_t nr_page = npage - nbase;
if (pages_pa + nr_page * sizeof(struct Page) > mem_end) {
   panic("page_init: page array overflows physical memory");
}

// 5.全部先标成 reserved，分配器不能随便拿走
for (size_t i = 0; i < nr_page; i ++) {
   struct Page *p = pages + i;
   p->flags = 0;//先清标志位
   p->property = 0;//空闲块大小先清零
   set_page_ref(p, 0);//引用计数清零
   SetPageReserved(p);
}

// 6. 先求 描述符数组 物理结束位置，再 ROUNDUP 到页边界 → freemem：第一个可交给分配器的物理地址
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

// 9. 把空闲区交给物理页管理器
init_memmap(pa2page(freemem), (mem_end - freemem) / PGSIZE);
/*
pa2page(freemem)：空闲区第一页对应的 struct Page *
(mem_end - freemem) / PGSIZE：空闲页个数
*/
```

#### 第四层：pmm_manager 抽象（`kern/mm/pmm.h`）

```c
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
```

`pmm.c` 里 `alloc_pages` 只是转发 `pmm_manager->alloc_pages(n)`。

#### 第五层：First-Fit 与 Best-Fit 分配

First-Fit（`default_pmm.c`）空闲块按地址升序组织，分配时**取第一个满足 `property >= n` 的块**；Best-Fit（`best_fit_pmm.c`）则**遍历整条链表，在满足要求的块中选 `property` 最小者**。两者释放时都要按地址合并相邻空闲块。

#### 第六层：Buddy System（`kern/mm/buddy_pmm.c`）

伙伴系统按2 的幂大小的块组织内存：`BUDDY_MAX_ORDER = 15`（2^15 页 = 128 MiB），每一阶一条链表，分配时向上取整到 `order = ceil(log2(n))`，从高到低逐级分裂；释放时用 XOR 技巧找伙伴：

```c
uintptr_t buddy_idx = idx ^ ((uintptr_t)1 << order);   // 伙伴页号
```

只有当伙伴"在受管范围内、非 reserved、是同级空闲块首页、`property` 匹配"时才合并，`order++` 继续向上尝试。

#### 第七层：SLUB 对象分配（`kern/mm/slub.c`）

SLUB 架在页分配器之上：小对象按尺寸类（8/16/…/2048）把一页切成固定对象，用 `struct slab` 头（48 字节）记录元数据，空闲对象前 8 字节存 next 指针；大于 2048 的请求走大对象路径，直接申请连续的 2^order 页。`kfree` 通过 `ptr & ~(PGSIZE-1)` 向下对齐到页首，读取 magic 区分小对象与大对象。

---

## 四、实验内容与实现

### 4.1 功能模块一：物理内存初始化

#### 任务一：实现物理内存探测与页描述符初始化（page_init）

**负责人：2413157-张书晗**

**需要实现的函数：**：`static void page_init(void) {}`

**使用的 prompt:**

```text
[PROMPT]
**任务**：修改 `kern/mm/pmm.c`，完成物理内存探测与 Page 描述符布局初始化能力。
`pmm_init()` 已调用 `page_init()`，但骨架未定义；本任务补全该能力，使当前 `pmm_manager` 只接管排除 OpenSBI、内核镜像与 Page 描述符数组占用后、按页对齐的完整空闲页面。

**操作要求**：
- 不得改动 `tools/`、评分脚本、Makefile 评分逻辑，以及 `basic_check` / `default_check` / `best_fit_check` 等检查函数。

**输出要求**：

[RELY]
// 宏与常量（须与工程一致）
- `PGSIZE` / `PGSHIFT`：页大小与页号移位
- `PHYSICAL_MEMORY_OFFSET`：内核虚拟地址与物理地址的固定偏移
- `PHYSICAL_MEMORY_END`：框架允许的可管理物理内存上界
- `DRAM_BASE`（通常 `0x80000000`）：物理内存起点；OpenSBI 占用约 `0x80000000~0x80200000`
- `PG_reserved` / `SetPageReserved` / `PageReserved`

// 全局状态
- `struct Page *pages`：Page 描述符数组（内核虚拟地址）
- `size_t npage`：可管理物理内存的页号上界（exclusive）
- `const size_t nbase = DRAM_BASE / PGSIZE`：可管理页号下界
- `uint64_t va_pa_offset`：虚实偏移；未正确设置前不得使用依赖它的转换

// 可用接口与符号
- `get_memory_base()` / `get_memory_size()`：从设备树读取物理内存基址与大小
- `init_memmap(struct Page *base, size_t n)`：把从 `base` 起连续 `n` 个完整空闲页交给当前 `pmm_manager`
- `pa2page` / `PADDR` / `PPN` / `ROUNDUP` / `set_page_ref` / `SetPageReserved`
- 链接符号 `end[]`：内核镜像结束位置（之后才可布局 `pages`）
- `panic()` / `cprintf()`：异常终止与诊断输出（格式符与参数类型严格匹配，避免 `-Werror`）

[GUARANTEE]
可根据需要添加：
- 本文件内的 `static` 辅助函数
- 局部变量与诊断打印

不得新增或更改对外公开的物理内存管理接口；不得为通过编译去改框架测试或评分注入逻辑。

[SPECIFICATION]
## page_init

**Pre-Condition**:
- `pmm_manager` 已完成 `init()`，`init_memmap` 可安全调用。
- 设备树解析设施可用；`get_memory_base/get_memory_size` 可被调用。
- 启动阶段已建立内核虚实映射，但 `va_pa_offset`、`npage`、`pages` 尚未建立可依赖的物理页描述符布局。
- 链接符号 `end` 之后存在可用于放置 Page 描述符数组的内核地址空间。

**Post-Condition**:
- 成功路径下：`va_pa_offset` 正确反映内核虚拟地址与物理地址的转换关系；`npage` 正确表示可管理物理内存的页号上界；`pages` 指向按页对齐、覆盖 `[nbase, npage)` 的描述符数组。
- 当前 `pmm_manager` 只接管：排除 OpenSBI、内核镜像与 Page 描述符数组占用之后、按页对齐的完整空闲页面；上述已用区域对应的描述符保持 reserved。
- 失败路径下：通过 `panic()` 终止初始化，且不得把无效或空集合交给 `init_memmap()`。

  **Case 1（设备树/范围无效）**:
  - 当物理内存基址或大小无效、上界不大于基址、或物理上界超出框架可管理上界时，终止初始化（`panic`），不设置可用空闲页，不调用 `init_memmap()`。

  **Case 2（描述符布局不可建立）**:
  - 当可管理页号上界不大于 `nbase`，或按 `end` 之后页对齐放置的 `(npage - nbase)` 个 `struct Page` 会越出可管理物理内存时，终止初始化，不调用 `init_memmap()`。

  **Case 3（无完整空闲页）**:
  - 当排除描述符数组占用并完成页对齐后，已无可管理的完整空闲页面时，打印诊断信息并 `panic`，绝不调用 `init_memmap()`。

  **Case 4（正常初始化）**:
  - 设备树范围有效且存在完整空闲页时：建立 `va_pa_offset`、`npage`、`pages`；先将描述符数组覆盖范围内的页全部标为 reserved（`ref=0`、`property=0`）；再将剩余完整空闲页注册给当前 `pmm_manager`；并用 `cprintf` 输出物理内存范围、`npage`、`pages` 的虚/实地址及空闲区信息。
  - `npage` 必须与评分对「设备树给出的物理上界对应页号」的期望一致，不得为“图省事”自行截断或四舍五入。

**Requirements**:
- 先具备正确的虚实偏移，再进行依赖该偏移的地址转换。
- 交给 `init_memmap` 之前，相关页描述符必须已是 reserved，以满足管理器断言。
- OpenSBI（约 `0x80000000~0x80200000`）与内核镜像不得被释放为空闲页。
- C99、`-nostdinc`、`-Werror`；只使用 `libs/` 与 `kern/` 已有设施。

**验收**:
- `make` 通过且无警告；`make grade` 输出 `[Test 1] Page Init: PASS`。
- 若 Best-Fit 尚未实现，Test 2 可能 FAIL，属正常。

> 已知框架冲突：未实现 Best-Fit 时，`best_fit_pmm.c` 检查函数引用 `free_list/nr_free`，且评分引用 `best_fit_pmm_manager`，整仓可能无法链接。应先完成任务二，或在临时副本中加最小脚手架；不要为了通过编译去改框架测试。
```

**实现要点**：

1. **先设置 `va_pa_offset = PHYSICAL_MEMORY_OFFSET`**，否则后续 `PADDR` 无法工作。
2. **从 DTB 读内存范围**并做健壮性校验：`mem_base == 0`、`mem_size == 0`、`mem_end <= mem_base` 或 `mem_end > PHYSICAL_MEMORY_END` 时 `panic`。
3. **`npage = PPN(mem_end)`**，评分测试期望的正是这个值；`npage <= nbase` 要 panic。
4. **`pages` 数组放在内核链接符号 `end` 之后、按 `PGSIZE` 向上对齐**，并确认 `(npage - nbase)` 个 `struct Page` 不越出 `mem_end`。
5. **先把所有描述符初始化为 reserved**（`PG_reserved` + `ref=0` + `property=0`）。这一步必须在 `init_memmap` 之前，因为其内部 `assert(PageReserved(p))`。
6. **计算空闲起点** `freemem = ROUNDUP(PADDR(pages) + sizeof(struct Page)*(npage-nbase), PGSIZE)`——注意 `PADDR` 的输入是内核虚拟地址，结果是字节。
7. 若 `freemem >= mem_end` 或 `PPN(freemem) >= npage`，说明没有可用页，panic。
8. 否则 `init_memmap(pa2page(freemem), (mem_end - freemem) / PGSIZE)`，并用 `cprintf` 输出物理内存范围、`npage`、`pages` 的 VA/PA、`freemem` 等（格式符与类型严格匹配，避免 `-Werror`）。

框架图：

```
内核启动
    │
    ▼
pmm_init()
    │
    ├─① init_pmm_manager()
    │       │
    │       ├─ pmm_manager = &best_fit_pmm_manager   （或 buddy 或 slub）
    │       └─ pmm_manager->init()
    │              └─ best_fit_init() / buddy_init()
    │                 （空闲结构就绪，nr_free=0）
    │
    ├─② page_init()
    │       │
    │       ├─ 设置 va_pa_offset、npage、pages[]
    │       ├─ 全部 PageReserved
    │       ├─ 计算 freemem
    │       └─ init_memmap(pa2page(freemem), n):转发到 pmm_manager->init_memmap
    │              └─ pmm_manager->init_memmap(base, n)：pmm_manager结构体是一种物理页分配算法
    │                     └─ best_fit_init_memmap / buddy_init_memmap
    │                        （空闲页进入算法数据结构）
    │
    └─③ check_alloc_page()
            └─ pmm_manager->check()
```

我们启动内核后，`page_init()` 打印出探测到的物理内存布局，结果如下图所示：

![page_init 物理内存输出](../../lab2_report/pic/page_init.png)

**扩展任务 Challenge（思考题）：如果 OS 无法提前知道当前硬件的可用物理内存范围，有何办法获取？**

1. **OpenSBI（bootloader）**扫描包括 DRAM 在内的外设，把结果写成 **DTB**，放在物理内存某处。  
2. 启动时把 DTB 物理地址放在 **`a1`**，内核在 `entry.S` 里存到 `boot_dtb`。  
3. `dtb_init()` 解析 DTB 的 memory 信息，得到 `memory_base` / `memory_size`。  
4. `page_init()` 再通过 `get_memory_base()` / `get_memory_size()` 使用这些范围。

同类思路还有：UEFI/BIOS 的 memory map、板级描述文件；没有固件协助时也可做内存探测，写后读回，但慢且有风险，工程上一般优先用固件。

### 4.2 功能模块二：物理内存分配

#### 任务一：理解 First-Fit 连续物理内存分配算法（思考题）

**负责人：2412888-刚栋冰**

First-Fit 分配器（`kern/mm/default_pmm.c`）用一条按物理地址升序的双向链表（`free_list`）记录所有空闲块，块首页的 `property` 字段存块内页数，`nr_free` 记录空闲页总数。其工作方式如下：

- **初始化空闲页（`default_init` / `default_init_memmap`）**：`default_init` 初始化链表并把 `nr_free` 清零；`default_init_memmap` 把一段连续的 `n` 个页（此前已在 `page_init` 中标记为 reserved）逐页清空 `flags/property/ref`，只保留块首页 `property = n` 并 `SetPageProperty`，然后 `nr_free += n`，最后按地址升序把该块插入 `free_list`。
- **分配连续物理页（`default_alloc_pages`）**：`assert(n > 0)`，若 `n > nr_free` 直接返回 NULL；否则从链表头开始遍历，返回**第一个**满足 `property >= n` 的块；把该块从链表摘除，若 `property > n` 则把剩余尾部 `page + n` 作为新块插回原位置；`nr_free -= n`，清除首页 `PG_property` 后返回。
- **回收页面并合并相邻空闲块（`default_free_pages`）**：逐页 `assert(!PageReserved && !PageProperty)`，清空标志后把 `base` 首页 `property = n` + `SetPageProperty`，按地址插入链表；然后先检查**前一个**块（若 `p + p->property == base` 则合并），再检查**后一个**块（若 `base + base->property == p` 则合并），合并时把被吞并块的 `PG_property` 清除、`property` 累加到新首页。

**原有的 First-Fit 算法是否有进一步的改进空间？**

1. **查找代价**：每次分配都从链表头线性扫描，最坏 \(O(m)\)（\(m\) 为空闲块数）。可用空闲块按大小分级、树/堆，或记录“下次搜索起点”（Next-Fit）降低平均查找成本。
2. **插入代价**：释放时为保持地址有序也要线性找插入点，\(O(m)\)。若块尾也记录块信息，或维护更好的索引，可加速定位与邻接判断。
3. **外部碎片**：First-Fit 倾向用靠前的块，可能留下许多难用的小空洞。Best-Fit / Buddy 等在碎片形态上往往不同，可作对照改进。
4. **合并**：当前合并依赖有序链表上的前驱/后继；邻接判断已是 \(O(1)\)，但整体仍受“先 \(O(m)\) 插入”限制。

为验证 First-Fit 实现正确，我们将 `init_pmm_manager()` 临时切换为 `default_pmm_manager` 后启动内核。输出中出现 `memory management: default_pmm_manager`，随后 `check_alloc_page() succeeded!`（即框架中的 `default_check` / `basic_check` 全部通过），结果如下图所示：

![First-Fit 测试](../../lab2_report/pic/First-Fit测试.png)

#### 任务二：实现 Best-Fit 连续物理内存分配算法

**负责人：2413157-张书晗**

**需要实现的函数：** `static void best_fit_check(void);`

**使用的 prompt：**：

```text
## 任务二：实现 Best-Fit 连续物理内存分配算法

[PROMPT]
**任务**：修改 `kern/mm/best_fit_pmm.c`（必要时仅改 `kern/mm/pmm.c` 的管理器切换），实现并接入 Best-Fit 连续物理页分配器。
本模块应提供与 First-Fit（`default_pmm.c`）相同的 `pmm_manager` 能力：维护按物理地址有序的空闲块链表，支持空闲区注册、连续页分配/释放与相邻空闲块合并；分配策略改为「在所有满足请求大小的空闲块中，选择块长最小者；并列时取地址更小者」。

**操作要求**：
- 必须在项目中修改实际文件，而不是仅展示代码片段。
- 参考 `default_pmm.c` 的完整 First-Fit 实现与约定，在 `best_fit_pmm.c` 中自行设计内部状态与管理函数，并定义 `best_fit_pmm_manager` 完成注册。
- **不得修改** `basic_check()`、`best_fit_check()`，以及 `tools/`、评分脚本、Makefile 评分逻辑。
- 允许在本文件内新增 `static` 辅助函数（如按地址插入空闲块）；不得改动 `struct Page` 布局与公共 `pmm` 接口签名。
- 在 `pmm.c` 的 `init_pmm_manager()` 中将当前管理器切换为 `&best_fit_pmm_manager`，并确保包含 `best_fit_pmm.h`（本任务唯一允许修改的其他文件；若已是 Best-Fit 则保持即可）。

**输出要求**：
- 直接进行文件操作；以工程真实代码与下列 [RELY]、[GUARANTEE]、[SPECIFICATION] 为准。
- 实现前复述：空闲块表示方式、与 First-Fit 的差异点、释放合并不变量。
- 汇报：修改文件、关键不变量、与 First-Fit 的判别场景、验证输出。

[RELY]
// 参考实现
- `kern/mm/default_pmm.c`：完整 First-Fit；Best-Fit 在空闲区组织、拆分、合并约定上应与之兼容，仅分配选块策略不同。

// 数据结构与宏（须与工程一致）
- `struct Page`：`flags` / `property` / `ref` / `page_link`
- `PG_property` / `SetPageProperty` / `ClearPageProperty` / `PageProperty`：
  空闲块**仅首页**置 `PG_property`，且 `property` 表示该连续空闲块的页数
- `free_area_t`：`{ list_entry_t free_list; unsigned int nr_free; }`
- `le2page(le, page_link)`、`list_init` / `list_add` / `list_add_before` / `list_del` / `list_next` / `list_prev` / `list_empty`
- `PageReserved`、`set_page_ref`、`alloc_page`/`free_page` 等上层封装

// 模块约定
- 本模块应使用**自己的** `static free_area_t`，并通过宏暴露 `free_list`、`nr_free`（框架自检会引用同名符号语义）
- 空闲链表按物理地址（`struct Page*` 地址序，等价于物理页序）升序组织
- `page_init()` 交给 `init_memmap` 的页面进入前均为 reserved；`init_memmap` 内会断言该前提

// 外部声明
- `extern const struct pmm_manager best_fit_pmm_manager;`（`best_fit_pmm.h`）

[GUARANTEE]
必须实现/提供的接口：
```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_check,   /* 使用文件中已有框架检查，勿改其实现 */
};
```

**实现要点**：

- **数据结构**：`static free_area_t free_area` + 宏 `free_list`/`nr_free`；空闲块按 `struct Page*` 地址升序，块首页 `property=n` + `PG_property`。
- **分配**：遍历整条链表，在 `property >= n` 中选最小者（严格 `<` 保证同大小取地址更小者）；删除整块，若 `property > n` 把剩余尾部 `page+n` 作为新块插回；`nr_free -= n`，清首页 `PG_property`。
- **释放**：逐页 `assert(!PageReserved && !PageProperty)`，有序插入，先与前块、再与后块合并，合并时清除被吞并块的 `PG_property`。

**你的 Best-Fit 算法是否有进一步的改进空间？**

当前实现分配时**遍历整条空闲链表**，时间复杂度 O(B)（B 为空闲块数），是主要瓶颈；

改进方向：
- ① 用**平衡树/红黑树**按 `property`（块大小）建索引，把找最小满足块降到 O(log B)；
- ② 采用分离空闲链表按大小分桶；
- ③ 与 Buddy 结合消除外部碎片。

#### 任务三：Buddy System（伙伴系统）分配算法

**负责人：2411514-蒲富坤**

**Spec（需求规格）**：把系统可用存储空间划分为大小为 2 的幂的块进行管理。需设计两份 prompt：第一份让 AI 参考现有 ucore 物理内存管理框架，从零生成 Buddy 分配模块并接入内核；第二份让 AI 针对生成的分配算法设计测试函数检验正确性。不提供代码框架和测试函数，实现与测试方案自行设计。


**实现要点**：

- 元数据 `buddy_state[32768]`（32 KiB，放 BSS 不占内核栈）：`0`=已分配/未管理，`1..16`=某阶空闲块首页，`0xFF`=空闲块内部页。
- 初始化按**绝对 PPN** 贪心拆成最大对齐 2^k 块；分配 `ceil(log2(n))` + 逐级分裂；释放 `idx ^ (1<<order)` 找伙伴并合并。
- `buddy_check()` 做结构自检；`buddy_test.c` 提供只读检查接口，不改变分配路径语义。

**测试覆盖了哪些核心行为与边界情况，为什么有效？** 见第五部分"Buddy 测试套件"的 10 项 PASS 清单。这些测试之所以有效，是因为：结构自检直接断言"不变量"（有序、对齐、2 的幂、总页数守恒），分裂/合并测试精确断言**每阶块数**的变化（这是 Buddy 最容易出错的点），压力测试用固定种子可复现，配合"分配后采样"的正确经验，能精确区分"分配器 bug"和"测试自身 bug"。

复杂度：分配 O(order)（分裂 + 常数查找），释放 O(order)（逐级合并），无外部碎片但存在内部碎片（向上取整到 2 的幂）。

#### 扩展任务 Challenge：任意大小的内存单元 SLUB 分配算法

**负责人：2411514-蒲富坤**

**Spec（需求规格）**：实现两层架构的高效内存单元分配——第一层基于页大小分配，第二层基于任意大小分配，可简化实现，体现主体思想即可。需设计两份 prompt：第一份让 AI 从零生成 SLUB 对象分配模块并接入内核；第二份让 AI 设计测试函数检验不同大小对象的分配释放。同样不提供框架与测试。

**实现要点**：

- **两层架构**：`alloc_pages/free_pages` 提供页块；SLUB 把页切成固定对象。
- **slab 头**：`ROUNDUP(sizeof(struct slab), 16)` = 48 字节，首个对象偏移 48；`partial`/`full` 两条链表；空闲对象前 8 字节存 next（免额外位图内存，代价是空闲对象本身不可写数据）。
- **大对象路径**：`size > 2048` 申请 `2^order` 页（最大 order 8，payload ≤ 1 MiB − 48），页首同样放头（magic `LARG`），返回 `页首 + 48`。
- **kfree 定位**：`ptr & ~(PGSIZE-1)` 向下对齐到页首读 magic；`kfree(NULL)` 返回；空 slab 立即 `free_pages`；非法指针窗口外打印返回，magic 错误/重复释放 `panic`。
- **地址转换**：`page_to_kva(page) = page2pa(page) + va_pa_offset`，等价于 `KADDR`（`KADDR(pa) = pa + va_pa_offset` 并校验 `PPN < npage`）。

**测试覆盖了哪些核心行为与边界情况，为什么有效？** SLUB 测试用"写满读回 + 地址不重叠"验证对象切割正确，用"跨 slab 计数"验证 `objs_per_slab` 与 `nr_slabs` 一致性，用"大对象页边界逐字节校验"验证 order 计算，用"统计不变量"（`used_pages == slabs + large_pages` 等）在每次操作后静默检查，能精确捕获元数据定位错误、对齐错误、重复释放、跨 slab 计数错误、页泄漏等典型 bug。

---

## 五、测试与验证

### gdb 调试页表查询过程

我们采用双重调试：一个调试器附加到宿主机上的 QEMU 进程，另一个通过 GDB stub 调试 guest 内的 ucore。

**（1）终端 A：启动暂停的 QEMU**

我们进入临时工程目录，执行：

```bash
qemu-system-riscv64 -machine virt -nographic -m 128M \
  -bios default -kernel bin/ucore.img -s -S
```

其中 `-s` 打开 `localhost:1234` 上的 GDB stub，`-S` 使虚拟机启动后立即停下。随后用 `pgrep -fl qemu-system-riscv64` 查到 QEMU 进程 PID。

**（2）终端 B：主机 LLDB 附加 QEMU 进程**

我们在宿主机上启动 `lldb`，执行 `process attach --pid 14077` 附加到该进程，再用 `process handle SIGPIPE -n true -p true -s false` 忽略 SIGPIPE，确认 `process status` 正常后执行 `continue`，让 QEMU 继续运行以配合 guest 侧调试。结果如下图所示：

![终端 B：主机 LLDB 附加](../../lab2_report/pic/终端%20B：主机%20LLDB%20附加.png)

**（3）终端 C：riscv64-gdb 连接内核并观察地址翻译切换**

我们在工程目录执行：

```bash
riscv64-unknown-elf-gdb \
  -ex 'file bin/kernel' \
  -ex 'set arch riscv:rv64' \
  -ex 'target remote localhost:1234'
```

连接成功后 `$pc = 0x1000`。我们接着：

1. 在物理入口 `b *0x80200000` 并 `continue`，命中 `kern_entry`，`$pc = 0x80200000`；
2. 在 `csrw satp` 处下断（`b *0x80200034`），写前 `$satp = 0x0`，单步后 `$satp = 0x8000000000080205`（MODE=8 表示 Sv39，页表根 PPN=`0x80205`）；
3. 在 `kern_init` 的虚拟地址 `b *0xffffffffc02000d6` 并 `continue`，成功停在 `kern/init/init.c` 的 `kern_init`，此时 `$pc = 0xffffffffc02000d6`，`$satp` 仍为上述值。

这说明：开启页表前取指使用物理地址；开启 Sv39 后，跳转到内核 C 入口已按虚拟地址执行，MMU 翻译已生效。结果如下图所示：

![终端 C：riscv64-gdb 连内核](../../lab2_report/pic/终端%20C：riscv64-gdb%20连内核.png)

### Buddy 测试套件（`PMM_USE_BUDDY=1`）

我们执行 `make clean && make DEFS=-DPMM_USE_BUDDY=1` 后启动内核，输出中出现 `memory management: buddy_pmm_manager`，随后 `buddy_test()` 各项均为 PASS，最终 `[Buddy] result: ALL PASS`，结果如下图所示：

![Buddy 测试](../../lab2_report/pic/伙伴系统分配算法测试.png)

### SLUB 测试套件（`SLUB_TEST=1`）

我们执行 `make clean && make DEFS="-DSLUB_TEST=1"` 后启动内核（底层页分配器仍为 Best-Fit），`slub_init()` 之后跑完 `slub_test()`，最终 `[SLUB] result: ALL PASS`，结果如下图所示：

![SLUB 测试](../../lab2_report/pic/SLUB%20测试.png)


### 使用提示词让 AI 生成代码过程中出现的 bug 及解决

1. **QEMU 10.2 与旧版差异（环境 bug）**：`-device loader` 不再设置启动地址（`Domain0 Next Address = 0`），且 `csrw satp` 后立即按新页表取指触发 `exec_page_fault`（epc=0x80200000）。解决：本地在 `/tmp` 副本中用 `-kernel bin/ucore.img` 替代 `-device loader`，并给启动页表加 1 GiB 恒等映射绕过；**仓库源码与评分逻辑未做这些修改**。

2. **Buddy 合并规则测试首轮 FAIL（测试 bug，非分配器 bug）**：单页分配可能从大块分裂并在 `p+1` 新生成 order-0 空闲伙伴，测试原先用"分配前"快照推导期望，误判为"错误合并"。解决：把 buddy 状态采样改为"分配后"，按断言逐条对照定位到根因，没有靠改测试绕过。

3. **框架既有缺口（链接冲突）**：未实现 Best-Fit 时，`best_fit_pmm.c` 的检查函数引用 `free_list/nr_free`、评分测试引用 `best_fit_pmm_manager`，整仓无法链接。解决：按 `algorithm_prompt.md` 提示，先完成任务二（Best-Fit）或在临时副本加最小脚手架，不修改框架测试。

4. **`make` 不跟踪 `DEFS` 变化（配置 bug）**：切换 `PMM_USE_BUDDY`/`SLUB_TEST` 时若不先 `make clean`，会复用旧编译选项的目标文件，得到"假通过/假失败"。解决：固化到操作记录，切换前必先 `make clean`。

---

## 六、实验总结与收获

### 对操作系统的理解

本实验涉及物理内存管理的核心机制，具体包括：

**1. 页描述符与地址转换**  每个物理页对应一个 `struct Page`（`ref`/`flags`/`property`/`page_link`），页描述符数组 `pages[]` 本身也占用物理内存，需要先建立并保持 reserved。`PADDR`/`PPN`/`page2pa`/`pa2page` 四个转换贯穿始终，关键陷阱是单位（字节 vs 页）与基准（`nbase`，物理内存不从第 0 页开始）。

**2. 分配算法背后的优缺点**  First-Fit 快但外部碎片多；Best-Fit 遍历全链表选最小满足块，碎片更小但 O(B)；Buddy 靠 2 的幂分裂/合并做到 O(order) 且无外部碎片，代价是内部碎片与元数据数组。没有"最好"的算法，只有对不同工作负载的取舍。

**3. SLUB 的两层分配思想**  页分配器解决"连续物理页"，SLUB 解决"小对象碎片与缓存友好"。空闲对象复用前 8 字节存 next 的 trick、空 slab 立即归还、大对象单独走 2^order 路径，都是真实内核 SLUB 的简化缩影。真实 SLUB 还有 per-CPU cache、自旋锁、跨 CPU 远端释放队列等，本实验未涉及（并发/中断不在范围内）。

**4. 设备树的作用**  物理内存布局不是硬编码，而是由 OpenSBI 通过 DTB 传给内核。`dtb.c` 解析 `memory` 节点的 `reg` 属性，让内核在 QEMU 改动内存大小时仍能自适应。

### AI 协作开发的经验

本次从"手写实现"转向"描述需求—生成—验证—迭代"的工作方式，最大体会依然是：**AI 能加速查资料、给方案、对照代码、生成测试，但操作系统实验里真正关键的是自己能否说清目标、看懂输出、判断对错**。

具体经验：

1. **把"实现"和"测试"拆成两份 prompt**：`algorithm_prompt.md` 只描述"实现什么、遵守什么约束"，`test_prompt.md` 只描述"测什么、针对哪些典型 bug"。这样每份 prompt 都可独立验证，避免了"一边实现一边测"导致的自我循环论证。

2. **spec 先于 prompt**：每个任务先写清楚"功能与接口说明、实现约束与可用资源、验收标准"，再把它转化为可交给 AI 的 prompt。spec 是"人想清楚要什么"，prompt 是"让 AI 知道要做什么"——前者决定上限，后者决定下限。

---

## 七、知识点总结

### 重要知识点与 OS 原理的对应

| 本实验知识点 | 对应 OS 原理知识点 | 含义、关系与差异 |
|---|---|---|
| `struct Page` 页描述符、页框 | 内存管理的基本单位——页/页框（frame） | 原理上"页框"是物理内存的固定大小块，实验用 `struct Page` 数组一一对应地描述每个页框；差异在于实验的页框大小固定 4 KiB、且描述符数组本身也要占内存 |
| `PADDR`/`PPN`/`page2pa`/`pa2page` 地址转换 | 逻辑地址/物理地址转换、地址重定位 | 原理上地址转换由硬件（MMU）完成，实验内核态用 Sv39 大页直接映射 + 软件宏做换算；差异在于"页内偏移"用位移（2 的幂）实现，真实系统可能有段式叠加 |
| First-Fit / Best-Fit 分配 | 连续内存分配算法（首次/最佳/最差适应） | 原理与实验完全对应，实验用双向链表组织空闲块；差异是实验只做**物理**连续分配，真实系统还有虚拟连续 vs 物理分散的区别 |
| Buddy System | 伙伴系统（Buddy System） | 原理与实验一致：2 的幂块、分裂/合并、XOR 找伙伴；差异是实验元数据用 `buddy_state` 数组简化，真实实现用位图/树 |
| 内部碎片 / 外部碎片 | 碎片概念（internal/external fragmentation） | 原理上是衡量分配器质量的核心指标；实验通过 Best-Fit（外部碎片小）与 Buddy（内部碎片）直观对比了两者 |
| Sv39 页表、`satp`、1 GiB 大页 | 分页、多级页表、页表基址寄存器 | 原理上多级页表用于稀疏映射，实验只用一张三级页表映射一个 1 GiB 大页；差异是实验尚未实现完整的页表增删改查（那是 lab3 内容） |
| DTB 设备树、`sfence.vma` | 硬件自举、固件接口、TLB 刷新 | 原理上 OS 从固件获取硬件信息；实验通过解析 DTB 的 `memory` 节点获取内存范围，`sfence.vma` 对应刷新 TLB |
| SLUB / `kmalloc`/`kfree` | 内核内存分配、slab/slub 分配器 | 原理上 slab 是内核小对象分配的核心；实验用"尺寸类 + slab 头 + 空闲链表"做了简化版，省略了 per-CPU cache 与着色（coloring） |

### OS 原理中很重要、但本实验没有对应上的知识点

1. **虚拟内存与请求调页**：缺页中断、按需调页（demand paging）、页面置换算法（FIFO / LRU / Clock / 改进 Clock）、工作集模型——本实验只做了物理页管理，尚未建立"虚拟内存"层面的分配。
2. **段页式内存管理的"段"**：本实验只涉及页式，段式（逻辑分段、段描述符）未出现，段页结合的关系也未体现。
3. **内存保护与共享、写时复制（COW）**：进程间内存共享、只读页写时复制、`fork` 的页面复制优化。
4. **完整的多级页表遍历与反向页表**：本实验只用了固定的大页映射，未实现页表的逐级分配、反转页表（inverted page table）等。
5. **页面回收与换页（swap）**：LRU 链表、二次机会、交换分区管理、页换入换出。
6. **内存映射文件与 mmap**：文件内容映射到进程地址空间。
7. **NUMA / 非一致内存访问**：多节点内存的局部性感知分配。
