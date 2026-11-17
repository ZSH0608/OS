# Lab4 扩展练习 Challenge 答案

> 分析范围：`kern/sync/sync.h`、`kern/driver/intr.c`、`libs/riscv.h`、`kern/trap/trapentry.S`、`kern/mm/mmu.h`、`kern/mm/pmm.c`、`kern/init/entry.S`。以下结论均以本仓库真实代码为依据，引用格式为 `文件:行号`。

## 结论摘要

1. `local_intr_save/restore` 的本质是「保存 `sstatus.SIE` 原值 → 必要时关中断 → 恢复原值」，不是无条件开/关中断；因此可嵌套，也不会在原本关中断的上下文里错误地打开中断。
2. Sv32/Sv39/Sv48 的页表遍历算法相同，差异集中在虚拟地址位宽、页表级数、VPN 划分、PTE 大小、`satp.MODE` 与大页层级。
3. `get_pte` 中两段相似代码，是 Sv39 三个层级中「根页表以下两级」的同一套「查表项 → 缺表则分配 → 填非叶 PTE → 下降」操作的手工展开；本工程根页表是静态构建的，所以恰好只有两段。
4. `get_pte` 把「查找」和「按需分配」合并在一个函数里，在本实验规模下可用，但存在 `NULL` 语义二义、分配失败不回滚、不识别大页叶子等缺陷。建议本实验保持接口不变、内部重构；从内核设计角度应拆分。
5. 本工程的启动页表用 1 GiB 叶大页映射内核区，`get_pte` 会把这种叶大页误当成下一级页表指针，因此不能对内核高半址的大页区间调用 `get_pte`；`check_boot_pgdir` 已经显式跳过该遍历。

---

## Q1：`local_intr_save/restore` 如何实现开关中断

### 1.1 代码事实

```c
// kern/sync/sync.h:8-26
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

```c
// kern/driver/intr.c:5-8
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
```

`SSTATUS_SIE` 是 S 态全局中断使能位，定义于 `libs/riscv.h:46`（`0x00000002`）。`read_csr/set_csr/clear_csr` 分别展开为 `csrr / csrrs / csrrc` 指令（`libs/riscv.h:210-225`）。

### 1.2 返回值语义

`__intr_save()` 的返回值不是「是否成功关中断」，而是「进入临界区之前，中断是否本来是开着的」：

| 进入前 `SIE` | `__intr_save()` 返回 | 返回后 `SIE` | `local_intr_restore(flag)` 行为 |
|---|---|---|---|
| 1（开） | 1 | 0（已关） | `flag == 1`，重新置 1 |
| 0（关） | 0 | 0（不变） | `flag == 0`，什么也不做 |

因此它实现的是「保存并恢复原状态」，而不是「先关后无条件开」。

### 1.3 为什么不能直接调用 `intr_disable()/intr_enable()`

如果写成：

```c
intr_disable();
/* critical section */
intr_enable();
```

会有两个问题：

1. **丢失调用者原有状态**：调用者可能本来就在关中断上下文中（例如 trap 处理、另一个临界区内部）。无条件 `intr_enable()` 会提前打开中断，破坏外层临界区。
2. **不可嵌套**：内层临界区结束后会把中断打开，外层剩余部分不再原子。

`local_intr_save/restore` 通过返回旧 `SIE` 解决这两个问题。嵌套示例：

```c
bool a, b;
local_intr_save(a);   // a = 1，SIE: 1 -> 0
    local_intr_save(b); // b = 0，SIE 仍为 0
    local_intr_restore(b); // b == 0，不做任何事
local_intr_restore(a); // a == 1，SIE: 0 -> 1
```

内层不会误开中断，外层结束时才恢复到进入前的状态。

### 1.4 与 trap 入口/返回的配合

RISC-V 硬件在发生 trap 时会把 `SIE` 存入 `SPIE`，并清零 `SIE`；`sret` 返回时再从 `SPIE` 恢复 `SIE`。本工程的 trap 入口/出口显式保存和恢复了整个 `sstatus`：

- `SAVE_ALL` 把 `sstatus` 保存到 trapframe：`kern/trap/trapentry.S:42-52`；
- `RESTORE_ALL` 在恢复通用寄存器之前把 `sstatus` 写回：`kern/trap/trapentry.S:58-63`；
- `__trapret` 最后执行 `sret`：`kern/trap/trapentry.S:114-118`。

这带来两个关键性质：

- 如果 trap 发生在临界区内部（`SIE == 0`），硬件保存的 `SPIE == 0`，`sret` 返回后 `SIE` 仍为 0，临界区不会被意外打开；
- 如果 trap 发生在临界区外部（`SIE == 1`），`SPIE == 1`，`sret` 返回后中断恢复使能。

如果 trap 路径只保存通用寄存器而不保存 `sstatus`，`sret` 就可能把临界区内部的中断重新打开，原子性被破坏。这正是「保存现场」不仅是保存 GPR 的原因。

### 1.5 宏实现的一个潜在问题

`local_intr_save` 使用了标准的 `do { ... } while (0)` 包装，但：

```c
#define local_intr_restore(x) __intr_restore(x);
```

它没有 `do { ... } while (0)`，而且分号写在宏内部。若这样使用：

```c
if (cond)
    local_intr_restore(flag);
else
    ...
```

展开后等价于：

```c
if (cond)
    __intr_restore(flag);;
else
    ...
```

会出现「`else` 没有匹配的 `if`」的编译错误，或者在更复杂的宏上下文中产生悬挂 `else` 风险。本仓库的调用点都把 `local_intr_restore` 当作独立语句使用（例如 `kern/schedule/sched.c`、`kern/process/proc.c`），所以当前不会触发；但从宏设计角度，应写成：

```c
#define local_intr_restore(x) do { __intr_restore(x); } while (0)
```

### 1.6 与功能模块三的关系

`schedule` 在「清 `need_resched` → 选下一个线程 → 调 `proc_run`」全程使用 `local_intr_save/restore`；`proc_run` 在「更新 `current` → 切换 `satp` → `switch_to`」全程使用同一对宏。原因是这三步必须对外表现为一个原子动作：

- 若在选出 `next` 之后、切换之前被时钟中断打断，可能再次进入 `schedule`，造成 `current`、页表、内核栈三者不一致；
- `proc_run` 中 `current`、`satp`、`sp/ra` 的更新顺序也不能被中断插入。

注意：`switch_to` 返回时，代码已经运行在**旧线程**被再次调度回来的上下文中，所以 `proc_run` 末尾的 `local_intr_restore(intr_flag)` 会在旧线程恢复执行时执行，这是预期行为。新线程第一次运行走的是 `forkret → forkrets → __trapret → sret`，由 trapframe 中的 `sstatus` 恢复中断状态（`kernel_thread` 在 `kern/process/proc.c:303-305` 中设置 `SPP/SPIE` 并清 `SIE`），不会永久继承关中断状态。

---

## Q2：Sv32 / Sv39 / Sv48 的异同

### 2.1 对比表

| 项目 | Sv32 | Sv39 | Sv48 |
|---|---|---|---|
| 适用架构 | RV32 | RV64 | RV64 |
| 虚拟地址位宽 | 32 | 39 | 48 |
| 页表级数 | 2 | 3 | 4 |
| VPN 划分（高位→低位） | 10 + 10 | 9 + 9 + 9 | 9 + 9 + 9 + 9 |
| 页内偏移 | 12 | 12 | 12 |
| PTE 大小 | 4 字节 | 8 字节 | 8 字节 |
| 每张 4 KiB 页表项数 | 1024 | 512 | 512 |
| PTE 中 PPN 位宽 | 22 | 44 | 44 |
| 物理地址位宽 | 34 | 56 | 56 |
| `satp.MODE` | 1 | 8 | 9 |
| 可映射页大小 | 4 KiB、4 MiB | 4 KiB、2 MiB、1 GiB | 4 KiB、2 MiB、1 GiB、512 GiB |
| 虚拟地址空间 | 4 GiB | 2^39 = 512 GiB | 2^48 = 256 TiB |
| 地址规范化 | 无 | bit[63:39] 必须等于 bit[38] | bit[63:48] 必须等于 bit[47] |

补充：Sv57 为 5 级、57 位 VA、`MODE=10`；Sv64 为 6 级、`MODE=11`。本实验使用 Sv39（`libs/riscv.h:140`）。

### 2.2 `satp` 布局与本工程的写法

RISC-V 的 `satp` 在 RV32 与 RV64 下布局不同，本仓库的宏定义给出了字段位置（`libs/riscv.h:131-143`）：

| 字段 | RV32（Sv32） | RV64（Sv39/48/57） |
|---|---|---|
| MODE | bit[31] | bit[63:60] |
| ASID | bit[30:22]（9 位） | bit[59:44]（16 位） |
| PPN | bit[21:0]（22 位） | bit[43:0]（44 位） |

本工程 `enable_paging()` 的写法是：

```c
// kern/mm/pmm.c:158-161
write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
```

`0x8000000000000000` 就是 `8 << 60`，即把 MODE 置为 8（Sv39）。`libs/riscv.h:245-248` 的 `lsatp()` 则是：

```c
write_csr(satp, SATP32_MODE | (pgdir >> RISCV_PGSHIFT)); // SATP32_MODE = 0x80000000
```

它只设置 bit[31]，在 RV64 的 `satp` 中 MODE 字段位于 bit[63:60]，因此 MODE 实际为 0；同时参数是 32 位，PPN 高位也会丢失。所以 `lsatp()` 在 Sv39 实验中不可用，必须使用 `enable_paging()` 的写法（或在 `proc_run` 中用 `((uint64_t)SATP_MODE_SV39 << 60) | (pgdir >> 12)`）。

另外，写 `satp` **不会**自动刷新 TLB。本工程在设置 `satp` 后执行 `sfence.vma`：

- 启动阶段：`kern/init/entry.S:24-31`；
- 整页刷新：`flush_tlb()`，`kern/mm/pmm.h:164-167`；
- 按地址刷新：`tlb_invalidate()`，`kern/mm/pmm.c:360-367`。

### 2.3 页表遍历的统一算法

三种模式下，页表遍历的逻辑完全一致，只是级数和 VPN 位宽不同：

```text
table = satp.PPN << 12
for level = 最高级 downto 0:
    pte = table[VPN[level]]
    if (!pte.V)                 -> page fault
    if (pte.R | pte.W | pte.X)  -> 叶子项，停止
    table = pte.PPN << 12       -> 继续下一级
```

叶子项对应的物理地址由「叶子 PTE 的 PPN + 虚拟地址中尚未使用的低位」拼成。非叶 PTE 的 `R/W/X` 必须为 0，只保留 `V`（以及规范允许的位）；本工程 `get_pte` 用 `pte_create(page2ppn(page), 0)` 填非叶项（`kern/mm/pmm.c:260`、`kern/mm/pmm.c:275`），只置 `V`，这是正确的。

### 2.4 本工程为什么用 `PDX1`、`PDX0`、`PTX`

`kern/mm/mmu.h:28-32`、`kern/mm/mmu.h:56-59` 定义了三个 9 位索引：

| 宏 | 位移 | 掩码 | Sv39 对应 |
|---|---|---|---|
| `PDX1(la)` | `>> 30` | `0x1FF` | VPN[2]，bit[38:30] |
| `PDX0(la)` | `>> 21` | `0x1FF` | VPN[1]，bit[29:21] |
| `PTX(la)` | `>> 12` | `0x1FF` | VPN[0]，bit[20:12] |

`PTSIZE` 为 `4096 * 512 = 2 MiB`（`kern/mm/mmu.h:53`），正好对应 Sv39 中「一级页表项作为叶子时映射 2 MiB」的粒度。

需要注意：`kern/mm/mmu.h:8-25` 顶部的注释仍然是 **Sv32/x86 风格** 的「10 + 10 + 12」和 Sv32 PTE 布局，与下面实际使用的 9 + 9 + 9 宏不一致，是历史遗留注释。阅读时应以宏定义和 `kern/init/entry.S` 的 Sv39 构建代码为准。

### 2.5 本工程启动页表的具体映射

`kern/init/entry.S:66-77` 构建了 `boot_page_table_sv39`，其中两个有效项都是 **1 GiB 叶大页**：

```asm
.zero 8 * 2
.quad (0x80000 << 10) | 0xcf   # VPN2=2，VA 0x80000000 恒等映射到 PA 0x80000000
.zero 8 * (511 - 3)
.quad (0x80000 << 10) | 0xcf   # VPN2=511，内核高半址映射到 PA 0x80000000
```

`0xcf = V|R|W|X|A|D`（没有置 `G`），`0x80000 << 10` 是 PPN=0x80000，即 PA=0x80000000。

本工程 `KERNBASE = 0xFFFFFFFFC0200000`、`PHYSICAL_MEMORY_OFFSET = 0xFFFFFFFF40000000`（`kern/mm/memlayout.h:34-38`），因此 `PADDR(KERNBASE) = 0x80200000`。对 `KERNBASE` 计算：

- `VPN2 = (0xFFFFFFFFC0200000 >> 30) & 0x1FF = 511`；
- 1 GiB 叶项覆盖的 PA 基址为 `0x80000000`；
- 虚拟地址低 30 位为 `0x00200000`，所以最终 PA = `0x80000000 + 0x00200000 = 0x80200000`。

这与内核实际加载地址一致。`VPN2=2` 的恒等映射则用于 `satp` 打开后、跳转到高半址虚拟入口之前仍能取指（`kern/init/entry.S:32-39`）。

---

## Q3：`get_pte` 中两段相似代码的成因

### 3.1 两段代码分别在做什么

`get_pte` 的完整逻辑（`kern/mm/pmm.c:243-278`）：

```c
pde_t *pdep1 = &pgdir[PDX1(la)];
if (!(*pdep1 & PTE_V)) {
    /* 分配一张 4 KiB 页作为下一级页表，清零、ref=1、挂到根页表 */
    *pdep1 = pte_create(page2ppn(page), 0);
}
pde_t *pdep0 = &((pte_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
if (!(*pdep0 & PTE_V)) {
    /* 再分配一张 4 KiB 页作为末级页表，清零、ref=1、挂到上一级页表 */
    *pdep0 = pte_create(page2ppn(page), 0);
}
return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
```

- **第一段**：`pdep1` 是根页表（由 `pgdir` 指向）中的 VPN[2] 项。若无效，分配一张页作为 **level-1 页表**，其表项将用 VPN[1]（`PDX0`）索引。
- **第二段**：`pdep0` 是 level-1 页表中的 VPN[1] 项。若无效，分配一张页作为 **level-0 页表**，其表项将用 VPN[0]（`PTX`）索引。
- 最后返回 level-0 页表中 VPN[0] 对应的**叶子 PTE 指针**，由调用者填入物理页和权限。

### 3.2 为什么两段几乎一模一样

因为它们执行的是同一件事：

```text
在某级页表中取索引 idx 的表项
若 V == 0：
    分配一页物理页
    ref = 1
    清零
    写入非叶 PTE（PPN + V）
把「当前表」下降到该表项指向的下一级页表
```

Sv32/Sv39/Sv48 的页表都是**同构的基数树（radix tree）**，每一级的操作完全相同，区别只是：

- 当前表的基址从哪里来（根是 `pgdir`，下级是 `KADDR(PDE_ADDR(上一级项))`）；
- 用哪个 VPN 字段做索引（`PDX1` / `PDX0` / `PTX`）；
- 总共有几级。

因此，完全通用的实现可以写成一个循环：

```c
pte_t *table = pgdir;
for (level = TOP; level > 0; level--) {
    idx = VPN[level](la);
    if (!(table[idx] & PTE_V))
        table[idx] = alloc_table_pte(...);
    table = KADDR(PDE_ADDR(table[idx]));
}
return &table[VPN[0](la)];
```

当前代码只是把这个循环**手工展开**了。相似的两段正是「同构层级」的直接体现。

### 3.3 为什么本工程只有两段，而不是 Sv39 应有的三段

Sv39 有 3 级页表：根（VPN[2]）、中间（VPN[1]）、末级（VPN[0]）。但 `get_pte` 只可能分配「根以下」的两级，因为：

- **根本身是静态提供的**：`boot_pgdir_va = boot_page_table_sv39`（`kern/mm/pmm.c:221-223`），这张 512 项页表在 `kern/init/entry.S:66-77` 的 `.data` 段中预先构建，包含恒等映射与内核高半址的两个 1 GiB 叶项。
- `get_pte` 的参数 `pde_t *pgdir` 是**按值传入的根页表指针**，函数无法替换根页表本身；它只能在根页表项无效时，在根项下挂一张新的 level-1 页表。
- 因此，能动态分配的中间层级只有 level-1 和 level-0 两层，正好对应两段代码。

一般规律是：

| 模式 | 总级数 | 根是否由调用者提供 | `get_pte` 中相似段数 |
|---|---|---|---|
| Sv32 | 2 | 是 | 1 |
| Sv39 | 3 | 是（本工程静态根） | 2 |
| Sv48 | 4 | 是 | 3 |

也就是说，**相似段数 = 总级数 - 1**。如果将来改为 Sv48，按当前写法应再增加一段（VPN[2] → level-2 表）；更好的做法是改成上面的循环。

### 3.4 如果根页表也允许动态创建

当前 `get_pte` 无法创建根页表，因为 `pgdir` 是按值传入的指针。若允许动态根，需要：

1. 把接口改成 `pde_t **pgdir`，或先由 `mm_create` / `pgdir_alloc_page` 分配根页表再传入；
2. 在 `get_pte` 内增加「根项 → level-2 表」的分配段（Sv48 还要再多一级）；
3. 或者统一改成按 `VPN` 数组循环的版本，根分配放在循环之前由调用者完成。

### 3.5 一个关键边界：大页叶子会被误当成下一级页表

`get_pte` 只检查 `PTE_V`，**没有检查当前项是否是叶子项**（`R/W/X` 是否为 0）。于是：

- 若某一级表项是启动页表中的 1 GiB 叶大页，`V=1`，代码不会分配，而是直接执行
  `KADDR(PDE_ADDR(*pdep1))`，把该叶项的 PPN 当作下一级页表物理地址。
- 以本工程为例，启动页表项 `(0x80000 << 10) | 0xcf` 的 PPN 是 `0x80000`，`PDE_ADDR` 得到 `0x80000000`；`KADDR(0x80000000)` 得到 `0xFFFFFFFFC0000000`，这落在内核高半址映射区，指向内核代码/数据，而不是一张页表。继续读写它就会把内核内存当作页表项破坏。

这正是 `check_boot_pgdir` 中框架注释说明的问题：

```c
// kern/mm/pmm.c:445-449
 * 框架修正：原循环用 int 遍历 KERNBASE，且启动页表是 1GiB 叶大页，
 * get_pte 会把大页项误当成次级页表页。模块一的 4KiB 映射正确性已由
 * check_pgdir 覆盖；此处跳过该大页遍历，直接做低地址 insert/访问测试。
```

本工程当前的 `get_pte` 调用点只使用低地址（`0x0`、`PGSIZE`、`0x100`、`0x100 + PGSIZE` 等），这些地址的根项要么无效（由 `get_pte` 新建表），要么是 4 KiB 映射；`boot_map_segment` 虽然会以 `create=1` 调 `get_pte`（`kern/mm/pmm.c:178`），但当前代码中没有调用它。因此实际运行不会踩到上述大页路径。但这说明 `get_pte` 不是通用的页表遍历器：一旦要在内核 1 GiB 大页区间内建立 4 KiB 映射，就必须先拆分大页，或者换成能识别叶子项的 walker。

### 3.6 非叶 PTE 的权限位

RISC-V 规定非叶 PTE 的 `R/W/X` 必须为 0；本工程 `get_pte` 用 `pte_create(page2ppn(page), 0)` 只置 `V`（`kern/mm/pmm.c:260`、`kern/mm/pmm.c:275`），并在 `kern/mm/pmm.c:256-259` 的注释中说明：如果非叶项带 `U` 或 `R/W/X`，`check_boot_pgdir` 中对 VA `0x100` 的真实访问会因硬件页表行走失败而缺页重启。这与 Sv32/Sv39/Sv48 的共同规则一致。

---

## Q4：`get_pte` 将查找与分配合并，是否应该拆分

### 4.1 当前接口与调用方式

```c
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
```

本仓库的调用点（`grep` 结果均位于 `kern/mm/pmm.c`）：

| 调用点 | `create` | 用途 |
|---|---|---|
| `boot_map_segment` `kern/mm/pmm.c:178` | 1 | 建立启动映射，需要创建页表 |
| `get_page` `kern/mm/pmm.c:284` | 0 | 只查找，用于查询某地址对应的 `struct Page` |
| `page_remove` `kern/mm/pmm.c:318` | 0 | 只查找，删除已有映射 |
| `page_insert` `kern/mm/pmm.c:326` | 1 | 建立/更新映射，需要创建页表 |
| `check_pgdir` `kern/mm/pmm.c:393,399,403,416` | 0 | 验证查找结果 |

`get_page`、`page_remove` 已经通过 `create=0` 获得了「纯查找」语义，说明当前设计并非完全没有区分；但区分方式只是一个布尔参数。

### 4.2 合并写法的优点

1. **调用者简单**：`page_insert`/`boot_map_segment` 一次调用就能拿到可写的叶子 PTE，不需要自己逐级分配。
2. **集中维护不变量**：页表页的 `ref=1`、清零、非叶项只置 `V` 等规则只在一个函数里实现，避免调用者重复写错。
3. **减少代码量**：对教学内核而言，少一层抽象，便于阅读。
4. `create=0` 时确实可以当查找函数用，覆盖了 `get_page`/`page_remove` 的需求。

### 4.3 合并写法的问题

1. **`NULL` 返回值语义二义**  
   - `create=0` 时，`NULL` 表示「中间页表不存在 / 地址未映射」；  
   - `create=1` 时，`NULL` 表示「`alloc_page` 失败（内存不足）」。  
   调用者无法区分「没有映射」和「分配失败」。`page_insert` 只能把两者都映射为 `-E_NO_MEM`（`kern/mm/pmm.c:326-329`）；`page_remove`/`get_page` 则把两者都当作「没有映射」处理。对错误处理和诊断都不利。

2. **查找函数带隐藏副作用**  
   函数名是 `get_pte`，但 `create=1` 时会分配物理页、改页表、增加引用计数。若调用者本意只是查询，却误传 `create=1`，就会产生不必要的分配；代码审阅时也不容易看出「查询」路径是否可能分配。

3. **分配失败不回滚，可能泄漏页表页**  
   第一段若已分配 level-1 页表并挂到根项，第二段 `alloc_page` 失败时直接 `return NULL`，已分配的 level-1 页表仍以 `ref=1` 挂在根页表上，没有回收。当前 `page_insert` 失败路径只返回错误，不会释放它。  
   此外，`page_remove`/`page_remove_pte` 只释放映射的**数据页**（`kern/mm/pmm.c:303-314`），不会释放页表页；页表页一旦建立就一直存在。`check_pgdir` 在末尾手动 `free_page(pde2page(...))` 两张页表页并断言空闲页数量恢复（`kern/mm/pmm.c:430-436`），正说明页表页需要显式回收。

4. **缺少并发/中断保护**  
   `get_pte` 的「检查 `V` → 分配 → 写入」不是原子操作，函数本身也没有加锁。当前实验是单 hart、主要在初始化阶段调用，问题不明显；但在 SMP 或允许中断重入的场景下，两个 walker 可能同时看到 `V=0` 并各自分配一张页表，造成泄漏或覆盖。

5. **不识别叶子项/大页**  
   如 Q3.5 所述，函数只检查 `V`，不检查 `R/W/X`。遇到 1 GiB/2 MiB 叶大页时会把叶项当表指针，既不能正确拆分大页，也不能安全返回错误。这使它无法作为通用的页表 walker 使用。

6. **难以测试和错误注入**  
   没有错误码、没有「本次分配了哪些页表页」的输出，失败路径难以精确测试；也不便于实现按需分页、预分配策略或内存配额。

### 4.4 拆分方案

一种清晰的分层是：

```c
/* 纯查找：不分配，不修改页表 */
pte_t *walk_pte(pde_t *pgdir, uintptr_t la);          // 无映射返回 NULL

/* 创建：必要时分配，返回 0 / -E_NO_MEM */
int alloc_pte(pde_t *pgdir, uintptr_t la, pte_t **ptep);
```

或者保留一个返回值更丰富的版本：

```c
enum pte_result { PTE_ABSENT, PTE_PRESENT, PTE_HUGE, PTE_BAD };
enum pte_result get_pte_ex(pde_t *pgdir, uintptr_t la, bool create,
                           pte_t **ptep);
```

拆分后的分工：

- `page_remove` / `get_page` 使用 `walk_pte`，绝不分配；
- `page_insert` / `boot_map_segment` 使用 `alloc_pte`，失败时能得到明确的 `-E_NO_MEM`，并按需回滚已分配的页表页；
- 页错误处理可以在 `walk_pte` 判定 `PTE_ABSENT` 后，再调用 `alloc_pte`，符合「先判断、后分配」的常见内核流程；
- 大页可以在 walker 中返回 `PTE_HUGE`，由上层决定拆分或拒绝。

代价：

- 接口变多，调用者需要选择；
- 若调用者先 `walk` 再 `alloc`，可能遍历两次；
- 教学实验中需要同步修改调用点和测试。

### 4.5 结论

- **对本实验**：建议**保持 `get_pte(pgdir, la, create)` 的签名不变**。原因有三：模块一的规格明确要求不改变 `get_pte` 的既有语义；现有调用点（`page_insert/page_remove/get_page/check_*`）和测试都依赖这个接口；改动接口会扩大与本模块无关的改动范围。  
  但可以在函数内部做低风险重构：把两段重复逻辑抽成一个按级循环或 `get_pte_level(table, idx, create, &err)` 辅助函数，并在失败时回滚已分配的页表页、区分「不存在」与「内存不足」。
- **从内核设计角度**：应该拆分「查找」和「分配」。生产内核（如 Linux）通常把页表遍历（`pgd_offset`/`p4d_offset`/...、`follow_pte`）与缺页/分配处理（`handle_pte_fault`、`__pte_alloc`）分开，并配合锁、错误码、大页和迁移机制。合并写法适合规模很小的教学内核，但不是通用的页表抽象。
- **一句话**：接口上可以保留 `create` 布尔参数作为教学折中；实现上应把「每级操作」抽象掉，并把「不分配」与「分配失败」用返回码区分开。是否拆成两个公开函数，取决于是否愿意为此修改调用者；本实验不建议改公开接口。

---

## 本实验的具体结论（与功能模块三的衔接）

1. **开关中断**：`schedule` 和 `proc_run` 用 `local_intr_save/restore` 包住「选择 + `current` 更新 + `satp` 切换 + `switch_to`」，保证调度决策与执行现场切换原子。`switch_to` 本身不保存 `sstatus`；第一次运行由 `forkrets → __trapret → sret` 从 trapframe 恢复中断状态，后续运行由各线程自己的 `local_intr_restore` 恢复。
2. **Sv39 页表切换**：`proc_run` 写 `satp` 时使用 `(SATP_MODE_SV39 << 60) | (pgdir >> 12)`，并在写后 `flush_tlb()`；不能使用 `lsatp()`。本实验所有线程共享 `boot_pgdir_pa`，所以实际不会频繁触发页表切换，但通用逻辑必须正确。
3. **根页表与 `get_pte` 的边界**：启动页表是静态的 Sv39 根表，内核区由 1 GiB 叶大页覆盖。`get_pte` 只适用于根表以下、需要 4 KiB 粒度映射的低地址区域；不要对内核 1 GiB 大页区间调用它。`check_boot_pgdir` 已经为此跳过 KERNBASE 遍历。
4. **接口取舍**：本实验保留 `get_pte` 的 `create` 参数；若后续要实现用户态按需分页或大页拆分，应先把页表遍历与页表分配拆开，并补上锁、错误码和页表页回收。

## 未确认点

- 本文的 `satp` 字段宽度、PTE 位布局与大页大小来自 RISC-V Privileged Spec 的通用定义，并与仓库 `libs/riscv.h:131-143`、`kern/mm/mmu.h` 的宏一致；若报告要求逐字引用规范，建议再核对所用版本的 Spec 原文。
- `check_boot_pgdir` 的注释明确说明「`get_pte` 会把 1 GiB 叶大页误当次级页表页」；本文通过代码路径确认了这一点，但没有实际触发该破坏性路径（触发会导致内核内存被当作页表改写）。
- `boot_map_segment` 当前在仓库中没有调用点；如果未来恢复调用并对高半址内核区间建映射，需要先处理大页拆分，否则会触发上述问题。
