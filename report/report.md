# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab X: [实验名称] |
| **小组成员** | 学号1-姓名1、学号2-姓名2、学号3-姓名3 |
| **完成日期** | YYYY-MM-DD |

### 小组分工

练习如何分工？

| 成员 | 负责的练习/模块 |
|------|----------------|
| 学号1-姓名1 | [练习或模块名称] |
| 学号2-姓名2 | [练习或模块名称] |
| 学号3-姓名3 | [练习或模块名称] |

实验报告如何分工？

---

## 一、实验目的

<!-- 说明本次实验的主要目标和预期成果 -->

本实验的主要目的是：

1. [目的1：例如，理解用户态进程如何通过系统调用进入内核]
2. [目的2：例如，掌握进程创建、执行、退出的完整生命周期]
3. [目的3：例如，学习使用 AI 辅助开发操作系统核心功能]

---

## 二、实验环境

你们使用的 AI 工具

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 学号1-姓名1 | [例如：Cline（VS Code插件） / Claude Code（终端 Agent） / DeepSeek Harness等] | [例如：Claude Sonnet 4.5 / GPT-5.5 / DeepSeek-V4-Flash等] | [可选：特殊配置等] |
| 学号2-姓名2 | | | |
| 学号3-姓名3 | | | |

**说明：**
- **AI 编程工具**：指具体使用的终端工具、编辑器插件、桌面应用或浏览器界面
- **底层模型**：指该工具使用的大语言模型及版本

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

[请用自己的话描述本章实验的核心主线，例如：
- 本章围绕哪个核心主题展开？（例如：用户进程管理、虚拟内存、文件系统等）
- 整个实验解决什么问题？（例如：如何让用户程序运行在操作系统之上）]

### 2.2 功能的逐步实现

[说明本章实验是按照什么顺序逐步实现各个功能的，以及为什么要按这个顺序，例如：

1. 首先实现**功能A** → 为什么首先实现这个？（例如：建立系统调用接口是后续所有用户态功能的基础）
2. 接着再完善**功能B** → 为什么接着实现这个？（例如：有了系统调用后，需要能够加载和启动第一个用户进程）
3. 然后完成**功能C** → 为什么然后实现这个？（例如：有了第一个进程后，需要实现进程复制功能）
4. 最后补充了**功能D** → 为什么最后实现这个？（例如：最后完善进程退出和清理机制，形成完整闭环）]

---

## 四、实验内容与实现

<!-- 
说明：本部分按照 exercises 文档中的练习顺序组织
每个功能模块或者练习中可能包含多项任务，请根据实际情况调整
-->

### 功能模块一：虚拟内存页操作功能

**负责人：** 2413157-张书晗

#### 模块功能描述

**需要实现/修改的函数：**

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm); // kern/mm/pmm.c
void page_remove(pde_t *pgdir, uintptr_t la);                                 // kern/mm/pmm.c
```

**功能说明：**

- 本模块在已有多级页表框架上完成最基本的映射维护：把物理页挂到指定线性地址，以及把已有映射安全撤销。
- `page_insert` 建立或更新映射；`page_remove` 删除映射。二者共同决定线性地址最终对应哪一页物理内存，并保持「页表项内容、物理页引用计数、TLB 状态」三者同步。
- 主要场景：首次建映射、同页更新权限、替换为其他物理页、`get_pte(..., create=1)` 分配中间页表失败返回 `-E_NO_MEM`、以及无映射时 `page_remove` 幂等返回。
- 与其他模块关系：依赖已实现的 `get_pte` / `pte_create` / `tlb_invalidate` 与物理页分配器；为后续页表测试、内核映射检查和线程地址空间逻辑提供基础。本实验以 `/Volumes/T7/lab4_new` 框架为底（而非直接在 lab3 分支上改），并把 lab2/lab3 代码填回标注位置。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown
[PROMPT]
**任务**：修改 `kern/mm/pmm.c`（如声明需同步则同时修改 `kern/mm/pmm.h`），完成「虚拟内存页操作」功能模块。
本模块在已有多级页表框架上提供最基本的映射维护能力：把指定物理页挂到给定线性地址上，以及把已有映射安全撤销。
`page_insert` 负责建立或更新映射，`page_remove` 负责删除映射；二者共同决定某一线性地址最终对应哪一页物理内存，并保证物理页引用计数、页表项内容与 TLB 状态三者一致。

**操作要求**：
- 必须在项目中修改实际文件，而不是仅展示代码片段。
- 保留文件中现有代码；只在 `page_insert` / `page_remove`（及确有必要时的模块内辅助函数）的标记或空实现位置补全。
- 不得修改 `get_pte()` 的既有语义与签名（可调用，不可改写其行为约定）。
- 不得改动物理内存分配器接口的对外契约。
- 不得引入与本模块无关的功能。

**输出要求**：
- 直接进行文件操作。
- 以工程真实代码及下列 `[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]` 为准。

[RELY]
#define PTE_V 0x001
#define PTE_R 0x002
#define PTE_W 0x004
#define PTE_U 0x010
#define E_NO_MEM 4
typedef uintptr_t pte_t;
typedef uintptr_t pde_t;
struct Page { int ref; uint64_t flags; unsigned int property; list_entry_t page_link; };
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
pte_t pte_create(uintptr_t ppn, uint32_t perm);
struct Page *pte2page(pte_t pte);
page_ref / set_page_ref / page_ref_inc / page_ref_dec
alloc_page / free_page
tlb_invalidate / flush_tlb

[GUARANTEE]
必须实现：
```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```
允许添加 static 辅助函数（如 page_remove_pte）。

[SPECIFICATION]
## page_insert
**Pre-Condition**: pgdir/page/la/perm 合法；可用 get_pte(...,1) 定位或创建末级 PTE。
**Post-Condition**: 成功则 la 映射到 page 且权限为 perm，引用计数与 TLB 同步；失败返回 -E_NO_MEM 且不留半完成映射。
**Case 1**: 无旧映射 → 新建映射，ref++，TLB 失效，返回 0。
**Case 2**: 已映射到同一 page → 更新权限，引用计数自洽，TLB 失效，返回 0。
**Case 3**: 已映射到其他 page → 先撤销旧映射再建立新映射，引用计数与 check_pgdir 可观察行为一致。
**Case 4**: 无法获得末级 PTE → 返回 -E_NO_MEM。
**Requirements**: 页表项、引用计数、TLB 三者同步；只描述行为不规定唯一实现步骤。

## page_remove
**Pre-Condition**: 不假定 la 一定有有效映射。
**Post-Condition**: 有映射则删除并同步 ref/TLB；无映射则安静返回（幂等）。
````

#### 实现迭代过程

本模块的实现经历了 2 次关键迭代，过程如下：

---

##### 第一次迭代

按上述提示词生成 `page_insert` / `page_remove`（含 static `page_remove_pte`），并迁入 lab2 的 `page_init`、Best-Fit，以及 lab3 的 trap 入口/处理代码。

**遇到的问题：**
- 问题1：本机 QEMU 使用框架默认 `-device loader` 时 OpenSBI Next Address 为 0，内核进不去；改为 `-kernel` 后仍因 lab4 原始 `entry.S` 缺少 0x80000000 恒等映射与虚址跳转而在开启 Sv39 后取指死循环。
- 问题2：`check_pgdir()` 已通过，但 `check_boot_pgdir()` 在 `strcpy((void*)0x100, ...)` 处整机重启。根因是框架 `get_pte` 给非叶页表项设置了 `PTE_U`，违反 RISC-V「非叶项 R/W/X/U 必须为 0」的约定，硬件页表行走失败。

**问题解决策略：**
- 针对问题1：迁入 lab3 已验证的 `entry.S` 恒等映射 + `.Lvirt_entry` 跳转，并修正 Makefile/`grade.sh` 的 QEMU 启动参数（环境/启动问题，不改提示词主体）。
- 针对问题2：将非叶项改为 `pte_create(ppn, 0)`；同步把 `check_pgdir` 中对 `boot_pgdir_va[0] & PTE_U` 的断言改为检查 `PTE_V`。这属于保留真实硬件语义的修正，与提示词中「TLB/页表一致性」要求一致。

---

##### 第二次迭代

**最终结果：**

经过 2 次迭代，该模块最终：
- ✅ 编译通过（`make`）
- ✅ `check_alloc_page` / `check_pgdir` / `check_boot_pgdir` 全部 succeeded
- ✅ `kmalloc_init` / `check_vmm` 通过；随后进入模块二占位的 `proc_init`（模块二/三尚未用提示词实现）

**关键改进点总结：**
1. 在实现中用 static `page_remove_pte` 统一「撤映射 + 降引用 + TLB 失效」，对应提示词 Case 3 与 `page_remove` 幂等要求。
2. 明确非叶 PTE 不得带 `PTE_U`，否则 `check_boot_pgdir` 的真实 MMU 访问会失败——这是提示词「保留真实语义」在 RISC-V 上的具体落地。
3. 验证阶段以框架自带 `check_pgdir`/`check_boot_pgdir` 为准；完整 `make grade` 还需后续完成模块二/三。

---

### 功能模块二：进程创建及初始化功能

**负责人：** 2411514-蒲富坤

#### 模块功能描述

**需要实现/修改的函数：**

```c
static struct proc_struct *alloc_proc(void);                                        // kern/process/proc.c
void proc_init(void);                                                               // kern/process/proc.c
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);              // kern/process/proc.c
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);           // kern/process/proc.c
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf); // kern/process/proc.c
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);                 // kern/process/proc.c
```

**功能说明：**

- 本模块把内核从「已经能运行内核代码」推进到「已经能创建并登记多个内核线程」：`alloc_proc` 得到干净 PCB，`proc_init` 包装 idleproc 并创建 initproc，`kernel_thread` 整理线程首次运行所需的临时中断帧，`do_fork` 把内核栈、PID、链表、状态位等资源组合成可运行实体，`copy_thread` 布置 trapframe 与 context，`copy_mm` 承担地址空间处理的占位。
- 模块完成后系统里至少要存在 `idleproc` 与 `initproc` 两个关键线程，且 `initproc` 需能在后续调度中被真正切换执行。
- 关键难点是「线程生命周期状态」与「资源挂接顺序」保持一致：`alloc_proc` 必须把 PCB 置于安全空白态，避免后续误用未初始化字段；`proc_init` 中的 idleproc 直接复用当前执行现场（pid/内核栈/current 都有特殊含义）；`kernel_thread` 与 `copy_thread` 需配合 `forkret → forkrets → __trapret` 形成首次运行链路；`do_fork` 需正确处理失败清理、PID 分配、链表挂接与唤醒顺序，否则会出现资源泄漏、僵尸结构或状态错误。
- 与其他模块关系：依赖模块一的 `get_pte`/`boot_pgdir_pa` 与物理页分配器；为模块三的 `schedule`/`proc_run`/`switch_to` 提供可调度实体与首次运行现场。本模块完成后即可满足 `test_lab4_process_create` 的全部断言。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词（完整版另存为 `lab4_module2_proc_create_prompt.md`）：

````markdown
[PROMPT]
**任务**：修改 `kern/process/proc.c`（如声明需同步则同时修改 `kern/process/proc.h`），完成「进程创建及初始化」功能模块。
本模块负责把系统中「已经能运行内核代码」推进到「已经能创建并登记多个内核线程」。`alloc_proc` 得到一个干净的进程控制块；`proc_init` 把当前执行现场包装成 idleproc 并创建第一个真正的内核线程 initproc；`kernel_thread` 把函数入口和参数整理成线程首次运行所需的临时中断帧；`do_fork` 把这些信息和内核栈、PID、链表、状态位等资源真正组合成一个可运行实体；`copy_thread` 把 trapframe 和 context 安放到新线程自己的内核栈上；`copy_mm` 在当前实验阶段承担地址空间处理接口的占位作用。

**操作要求**：
- 必须在项目中修改实际文件，而不是仅展示代码片段。
- 保留文件中现有代码；只在标记为「lab4」/「YOUR CODE」的占位位置补全。
- 不得修改 `get_pid` / `hash_proc` / `find_proc` / `setup_kstack` / `put_kstack` / `forkret` 等既有辅助函数的行为约定。
- 不得改动 `struct proc_struct`、`struct context`、`struct trapframe` 的字段定义。
- 不得引入与本模块无关的功能。

**输出要求**：
- 直接进行文件操作。
- 以工程真实代码及下列 `[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]` 为准。

[RELY]
enum proc_state { PROC_UNINIT, PROC_SLEEPING, PROC_RUNNABLE, PROC_ZOMBIE };
#define PROC_NAME_LEN 15
#define MAX_PROCESS 4096
#define MAX_PID (MAX_PROCESS * 2)
#define KSTACKSIZE (2 * PGSIZE)
#define CLONE_VM 0x00000100
struct proc_struct { state, pid, runs, kstack, need_resched, parent, mm, context, tf, pgdir, flags, name, list_link, hash_link };
struct context { ra, sp, s0~s11 };
struct trapframe { gpr(struct pushregs), status, epc, badvaddr, cause };
extern struct proc_struct *idleproc, *initproc, *current;
extern char bootstack[], bootstacktop[];
extern uintptr_t boot_pgdir_pa;
kmalloc / kfree
alloc_pages / free_pages / page2kva / kva2page
get_pid / hash_proc / find_proc / set_proc_name / get_proc_name
setup_kstack / put_kstack / wakeup_proc / forkret / kernel_thread_entry / forkrets
list_init / list_add / list_next / le2proc
local_intr_save / local_intr_restore
read_csr(sstatus) / SSTATUS_SPP / SSTATUS_SPIE / SSTATUS_SIE
E_NO_MEM / E_NO_FREE_PROC

[GUARANTEE]
必须实现：
```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

[SPECIFICATION]
## alloc_proc
**Pre-Condition**: 无。
**Post-Condition**: 返回一块状态干净、字段已初始化的 PCB：state=PROC_UNINIT、pid=-1、runs=0、kstack=0、need_resched=0、parent=NULL、mm=NULL、context 清零、tf=NULL、pgdir=boot_pgdir_pa、flags=0、name 清零；内存不足返回 NULL。

## proc_init
**Pre-Condition**: pmm/vmm 已初始化；proc_list 与 hash_list 未初始化。
**Post-Condition**: proc_list 与 hash_list 初始化完毕；idleproc（pid=0、RUNNABLE、kstack=bootstack、need_resched=1、parent=NULL、mm=NULL、tf=NULL）与 initproc（pid=1、RUNNABLE、parent=idleproc、mm=NULL、context.ra 与 context.sp 非 0）都已建立，且 current=idleproc。

## kernel_thread
**Pre-Condition**: fn/arg 合法；进程系统已初始化。
**Post-Condition**: 构造临时中断帧（s0=fn、s1=arg、epc=kernel_thread_entry、status 置 SPP/SPIE 并清 SIE），调用 do_fork 返回新线程 pid 或负错误码。

## do_fork
**Pre-Condition**: 进程数未达上限。
**Post-Condition**: 依次完成 alloc_proc → setup_kstack → copy_mm → copy_thread → 挂 proc_list/hash_list（关中断）→ wakeup_proc 七步，返回子进程 pid；任一步失败则回滚已分配资源并返回负错误码。

## copy_thread
**Pre-Condition**: proc->kstack 已分配，tf 有效。
**Post-Condition**: trapframe 放到内核栈顶，a0=0，sp=esp（esp==0 时用栈顶），context.ra=forkret、context.sp=tf。

## copy_mm
**Pre-Condition**: current->mm == NULL。
**Post-Condition**: 本实验不做地址空间复制，返回 0。
````

#### 实现迭代过程

本模块的实现经历了 2 次关键迭代，过程如下：

---

##### 第一次迭代

按上述提示词生成 6 个函数，先落地「标准 7 步 do_fork + 内核栈顶布置 trapframe」的骨架。

**遇到的问题：**
- 问题1：`test_lab4_process_create` 在 `idleproc->context.ra == 0` 处断言失败。根因是 `alloc_proc` 未对 `context` 清零，残留了 kmalloc 未初始化的垃圾值。
- 问题2：`idleproc->kstack` 被误用 `setup_kstack` 分配了新页，导致 `idleproc->kstack == (uintptr_t)bootstack` 断言失败。idleproc 本应直接复用启动阶段已建立的 bootstack，而非像普通线程那样另分配内核栈。

**问题解决策略：**
- 针对问题1：在 `alloc_proc` 中用 `memset(&(proc->context), 0, sizeof(struct context))` 显式清零上下文，并同步清零 `tf`、`name`、`flags` 等字段。
- 针对问题2：`proc_init` 中把 `idleproc->kstack = (uintptr_t)bootstack`，并把 `need_resched` 置 1、`state` 置 `PROC_RUNNABLE`，使其成为「继承当前执行现场」的空闲线程而非新分配栈的线程。

---

##### 第二次迭代

**最终结果：**

经过 2 次迭代，该模块最终：
- ✅ 编译通过（`make`）
- ✅ `test_lab4_process_create` 断言全部满足（idleproc 与 initproc 的 pid/state/parent/context 等字段一致）
- ✅ `proc_init` 返回后 `current == idleproc`，`initproc` 已登记为 pid=1 的就绪线程，等待模块三调度

**关键改进点总结：**
1. 严格区分「idleproc 复用现场」与「initproc 经 do_fork 复制」两条创建路径：前者手动打 pid=0 / bootstack / need_resched，后者走完整 7 步 fork。
2. `copy_thread` 用 `(struct trapframe *)(proc->kstack + KSTACKSIZE) - 1` 把中断帧顶到内核栈顶，并设 `context.ra=forkret`、`context.sp=proc->tf`，保证首次切换能走 `forkret → forkrets → __trapret` 恢复链路。
3. `do_fork` 通过 `local_intr_save/restore` 保护「分配 pid + 挂链表 + nr_process++」这段临界区，失败路径按 kstack → proc 逆序回滚，避免泄漏。

---

### 功能模块三：进程调度功能

**负责人：** 2412888-刚栋冰

#### 模块功能描述

**需要实现/修改的函数：**

```c
void cpu_idle(void);                                         // kern/process/proc.c
void schedule(void);                                         // kern/schedule/sched.c
void proc_run(struct proc_struct *proc);                     // kern/process/proc.c
void switch_to(struct context *from, struct context *to);    // kern/process/switch.S
```

**功能说明：**

- 本模块负责把已经创建出来的内核线程真正交给 CPU 执行：`cpu_idle` 作为空闲线程的执行主体持续等待调度需求；`schedule` 从进程链表选出下一个可运行线程；`proc_run` 在安全临界区内切换当前线程、页表和执行现场；`switch_to` 完成最底层的寄存器上下文保存与恢复。
- `cpu_idle` 在 `current->need_resched` 为真时调用 `schedule()`，否则继续空转等待；它永不返回。
- `schedule` 在关中断临界区内清除 `current->need_resched`，从 `current` 的后继节点开始轮转扫描 `PROC_RUNNABLE`；扫描一周仍无候选时回退 `idleproc`；被选中线程 `runs++`，仅当 `next != current` 时调用 `proc_run`。由于 `idleproc` 不在 `proc_list` 中，以 idle 为起点时必须从链表头开始扫描。
- `proc_run` 在关中断临界区内依次更新 `current`、按需写 Sv39 `satp` 并刷新 TLB、调用 `switch_to(&prev->context, &proc->context)`。`current` 必须在 `switch_to` 之前更新，因为新线程第一次运行经 `forkret` 时需要读取 `current->tf`。
- `switch_to` 只负责保存/恢复 `ra`、`sp`、`s0~s11`，不参与调度策略判断。`struct context` 在 RV64 下各字段 8 字节，偏移依次为 `ra=0`、`sp=8`、`s0=16`、…、`s11=104`。
- 关键难点是切换过程的原子性：`schedule` 的「选择 + 切换」和 `proc_run` 的「`current` + 页表 + 现场」都必须处于 `local_intr_save/restore` 临界区，否则当前线程、页表和实际执行现场可能出现不一致。新线程第一次运行能走到正确入口，依赖模块二预先组织的 `context.ra=forkret`、`context.sp=proc->tf` 以及 `forkret → forkrets → __trapret` 恢复链路。
- 与其他模块关系：依赖模块二的 `idleproc`/`initproc`、`kernel_thread`/`copy_thread` 与首次运行现场；依赖模块一的 `get_pte`/`boot_pgdir_pa` 与页表基础设施。本模块完成后，`initproc` 才能从 `idleproc` 手中接管 CPU，`test_lab4_schedule` 的两个测试线程才能被真正调度执行。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词（完整版另存为 `lab4_module3_sched_prompt.md`）：

````markdown
[PROMPT]
**角色**：你是熟悉 RISC-V Sv39、uCore/rcore 风格内核与上下文切换的实验工程师。

**任务**：在仓库中完成 Lab4「功能模块三：进程调度」，替换以下三处占位实现（必须修改真实文件，而不是只输出代码片段）：

- `kern/process/proc.c`：实现 `void proc_run(struct proc_struct *proc)` 与 `void cpu_idle(void)`，替换文件末尾 `//LAB4 function3 YOUR CODE` 下的占位实现。
- `kern/schedule/sched.c`：实现 `void schedule(void)`，替换文件中的占位实现。
- `kern/process/switch.S`：实现 `void switch_to(struct context *from, struct context *to)`，替换占位汇编。

**系统现状与运行链**：
- 模块二已完成：`idleproc(pid=0)` 复用 `bootstack`、`state=PROC_RUNNABLE`、`need_resched=1`、不在 `proc_list` 中；`initproc(pid=1)` 已完成 `do_fork` 登记，`context.ra=forkret`、`context.sp=proc->tf`。
- `kern/init/init.c` 最后调用 `cpu_idle()`。
- 新线程第一次被 `switch_to` 恢复后，执行 `forkret -> forkrets -> __trapret -> sret -> kernel_thread_entry -> init_main`。
- 时钟中断处理 `kern/trap/trap.c::interrupt_handler` 会把 `current->need_resched` 置 1。
- `tools/test_lab4.c::test_lab4_schedule` 在 `init_main` 开头执行：创建 2 个测试线程，然后连续 8 次 `current->need_resched = 1; schedule();`，要求两个线程都真正运行过并打印 `[Test Schedule] PASS`。测试线程自身会反复 `need_resched = 1; schedule();`。

**操作要求**：
- 只替换上述三处占位块，保留文件中其余代码与注释。
- 不得修改 `struct context` / `struct proc_struct` / `struct trapframe` 的定义、函数签名、公开头文件。
- 不得改写 `wakeup_proc`、`forkret/forkrets/__trapret`、模块一（`page_insert/page_remove/get_pte`）与模块二（`alloc_proc/proc_init/do_fork/copy_thread`）的实现。
- 不得使用 `libs/riscv.h` 中的 `lsatp()`：它是 Sv32 语义（`SATP32_MODE | pgdir >> 12`），在 Sv39 下会得到错误的 `satp`。设置 `satp` 必须参照 `kern/mm/pmm.c::enable_paging()`。
- 不要实现或修改 `do_exit`。测试线程 / `init_main` 返回后进入 `do_exit` panic 属于框架现状，不在本模块范围。
- 不新增文件、全局变量或与本模块无关的改动。

**输出要求**：
- 直接编辑文件；完成后汇报每个文件改了哪几段、为什么。
- 以工程真实代码和下列 `[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]`、`[ACCEPTANCE]` 为准；如与你的通用模板冲突，以本工程事实为准。

[RELY]
// ===== 数据结构与符号（以仓库真实定义为准）=====
struct context { uintptr_t ra; uintptr_t sp; uintptr_t s0; ... uintptr_t s11; };
// RV64：字段连续存放，每个 8 字节；偏移为 ra=0, sp=8, s0=16, s1=24, ..., s11=104

struct proc_struct {
    enum proc_state state; int pid; int runs; uintptr_t kstack;
    volatile bool need_resched; struct proc_struct *parent;
    struct mm_struct *mm; struct context context; struct trapframe *tf;
    uintptr_t pgdir; uint32_t flags; char name[PROC_NAME_LEN+1];
    list_entry_t list_link; list_entry_t hash_link;
};
extern struct proc_struct *idleproc, *initproc, *current;
extern list_entry_t proc_list;
enum proc_state { PROC_UNINIT, PROC_SLEEPING, PROC_RUNNABLE, PROC_ZOMBIE };

// ===== 调度辅助 =====
local_intr_save(intr_flag);      // 保存 sstatus.SIE 到 intr_flag，若原为开则关中断
local_intr_restore(intr_flag);   // 仅当 intr_flag 为真时重新开中断；可嵌套
list_next(le);                   // 返回链表下一项
le2proc(le, list_link);          // list_entry_t* -> struct proc_struct*
// 注意：proc_list 以 &proc_list 为哨兵；idleproc 不在 proc_list 中

// ===== 页表切换 =====
extern uintptr_t boot_pgdir_pa;
#define SATP_MODE_SV39 8
#define RISCV_PGSHIFT 12
// enable_paging() 的实际写法：
//   write_csr(satp, 0x8000000000000000 | (boot_pgdir_pa >> RISCV_PGSHIFT));
// 等价写法：((uint64_t)SATP_MODE_SV39 << 60) | (proc->pgdir >> RISCV_PGSHIFT)
// satp.MODE 位于 bit[63:60]，PPN 位于低位；写 satp 后必须刷新 TLB
static inline void flush_tlb(void) { asm volatile("sfence.vma"); }

// ===== 首次运行链路 =====
// switch_to 恢复 context 后：forkret() -> forkrets(current->tf)
// -> __trapret(RESTORE_ALL + sret) -> kernel_thread_entry -> fn(arg)
// 因此 switch_to 只需忠实保存/恢复 ra, sp, s0-s11

// ===== 测试语义 =====
// test_lab4_schedule 连续 8 次直接调用 schedule()，要求两个测试线程都被调度到；
// 每个测试线程也会主动让出 CPU。调度必须是轮转的，且无死循环。
// grade 还要求输出：[Test Schedule] PASS

[GUARANTEE]
必须实现且仅实现以下函数（签名不可变）：
```c
void cpu_idle(void);                                          // kern/process/proc.c
void proc_run(struct proc_struct *proc);                      // kern/process/proc.c
void schedule(void);                                          // kern/schedule/sched.c
void switch_to(struct context *from, struct context *to);     // kern/process/switch.S
```

[SPECIFICATION]

## switch_to
**Pre-Condition**: `from`、`to` 非空；`from->` 可写；调用发生在内核态。
**Post-Condition**:
- 把当前线程的 `ra`、`sp`、`s0~s11` 按 `struct context` 的字段顺序存入 `from`；
- 从 `to` 载入 `ra`、`sp`、`s0~s11`；
- 执行 `ret` 后控制流到达 `to->ra`，此时 `sp` 已是目标线程的栈。
**Requirements**:
- 进入函数时的 `sp` 必须先被保存再被覆盖；不要依赖当前栈保存中间值。
- 只允许使用 `a0`/`a1` 和 caller-saved 暂存寄存器；不得破坏目标的 `s0~s11`。
- 不参与调度策略判断，不访问 `proc_list`、不调用 C 函数。

## schedule
**Pre-Condition**: `current != NULL`；`proc_list` 已初始化；调用者允许被抢占。
**Post-Condition**:
- 进入临界区后先把 `current->need_resched = 0`；
- 从 `current` 在 `proc_list` 中的下一个节点开始轮转扫描，选出 `state == PROC_RUNNABLE` 的线程；
- 若扫描一周仍无候选，选择 `idleproc`；
- 被选中线程 `runs++`；
- 若选中线程不等于 `current`，调用 `proc_run(next)`；
- 选择与切换全程处于 `local_intr_save/restore` 保护的临界区内。
**Requirements**:
- 扫描必须能在有限步内结束；哨兵 `&proc_list` 不是进程，不能交给 `le2proc`。
- `current == idleproc` 与 `current` 在链表中两种情况都要处理。
- 找不到可运行线程时必须有 idle 回退，不能空转或崩溃。

## proc_run
**Pre-Condition**: `proc != NULL`；`proc->state` 为 `PROC_RUNNABLE`（或 idleproc）；允许从任意内核上下文调用。
**Post-Condition**:
- 若 `proc == current`，直接返回，不做无意义切换；
- 否则在 `local_intr_save/restore` 临界区内：
  1. 令 `current = proc`；
  2. 若目标 `pgdir` 与当前页表不同，写 `satp` 为 `(SATP_MODE_SV39 << 60) | (proc->pgdir >> RISCV_PGSHIFT)`，并执行 `flush_tlb()`；
  3. 调用 `switch_to(&prev->context, &proc->context)` 完成现场切换。
**Requirements**:
- 必须先更新 `current`、再切换页表、最后 `switch_to`，保证新线程首次运行经 `forkret` 时 `current->tf` 已正确。
- 不得在切换后假设局部变量仍属于新线程；`local_intr_restore` 会在旧线程被再次调度回来时执行，这是正确的。
- 当前实验 idleproc 与 initproc 的 `pgdir` 都是 `boot_pgdir_pa`，但仍要按通用情况处理页表切换。
- 禁止在 `proc_run` 内再次调用 `schedule`。

## cpu_idle
**Pre-Condition**: `kern_init` 已调用 `intr_enable()`；`current == idleproc`。
**Post-Condition**: 永不返回；循环检查 `current->need_resched`，为真时调用 `schedule()`；无调度需求时继续等待。
**Requirements**:
- 不要无条件反复调用 `schedule()` 导致空转抢占；只在 `need_resched` 为真时调度。
- 若 `schedule()` 找不到其他可运行线程而选回 idleproc，`cpu_idle` 必须能继续循环等待。
- 保持头文件中的 `__attribute__((noreturn))` 语义，不要 return。

[ACCEPTANCE]
1. `make` 编译、链接通过，无未定义符号。
2. 运行 QEMU 后至少看到：
   - `[Test Schedule] PASS`
   - `this initproc, pid = 1, name = "init"`
   - `To U: "Hello world!!".`
   - `To U: "en.., Bye, Bye. :)"`
3. `make grade` 中 `check Schedule` 输出 OK。
4. 自检：
   - `initproc` 能从 `idleproc` 手中接管 CPU；
   - 两个测试线程都运行过，说明轮转选择有效；
   - 连续多次 `schedule()` 不会死循环、不会重复初始化临界区、不会丢失中断状态。

[TRAPS]
1. 使用 `lsatp()` 设置 Sv39 的 `satp` —— 错误，必须用 `8ULL << 60 | pa >> 12` 的写法。
2. 写 `satp` 后忘记 `flush_tlb()` / `sfence.vma`。
3. 在 `switch_to` 之后才更新 `current` —— 新线程 `forkret` 会读到旧的 `current->tf`。
4. `switch_to` 用当前栈保存中间值，或在恢复 `sp` 之后再保存旧 `sp`。
5. `schedule` 扫描时把哨兵 `&proc_list` 当成进程，或 `current == idleproc` 时从错误位置开始。
6. 忘记清 `need_resched`，导致测试线程反复抢占、无法推进。
7. 在 `schedule` 和 `proc_run` 中重复 `runs++`。
8. 把 `local_intr_restore` 放在 `switch_to` 之前 —— 临界区被提前打开。
9. 在 `proc_run` 中无条件写 `satp` 而不判断页表差异（不致命，但应说明理由）。
10. 把 `do_exit` 的 panic 当成模块三的 bug 去改，扩大改动范围。
````

#### 实现迭代过程

本模块的核心代码一次生成即通过编译与调度测试，迭代主要发生在构建/验证环境。过程如下：

---

##### 第一次迭代

按上述提示词替换三处占位：`switch_to` 保存/恢复 `ra, sp, s0~s11`；`schedule` 在关中断临界区内轮转选择；`proc_run` 更新 `current`、按需切换 `satp`、调用 `switch_to`；`cpu_idle` 检查 `need_resched` 并调度。

**遇到的问题：**
- 问题1：WSL 默认 PATH 中没有 `riscv64-unknown-elf-*`，非交互式 `bash -lc` 又不会加载 `.bashrc` 中的工具链路径，导致无法直接编译。根因是本机交叉工具链位于 `D:\OS\riscv\...`，需要显式加入 PATH。
- 问题2：首次在 WSL 中执行 `make grade` 时，`tools/grade.sh` 报 `Syntax error: ")" unexpected`。根因是该脚本在当前 Windows 克隆中为 CRLF 换行，WSL 的 dash 把 `\r` 当成命令的一部分。
- 说明：以上两个问题都属于构建/验证环境，不涉及模块三内核代码；内核本身的 `make` 已通过。

**问题解决策略：**
- 针对问题1：在 WSL 命令中显式 `export PATH="$PATH:/mnt/d/OS/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin"`，不修改仓库文件。
- 针对问题2：仅对 `tools/grade.sh` 执行 `sed -i 's/\r$//'` 做换行转换；转换后用 `git hash-object --path=code/tools/grade.sh` 与 `git rev-parse HEAD:code/tools/grade.sh` 比对，哈希一致，说明没有内容改动。

---

##### 第二次迭代

**最终结果：**

经过实现与验证，该模块最终：
- ✅ 编译、链接通过（`make`）
- ✅ `check_alloc_page` / `check_pgdir` / `check_boot_pgdir` 继续通过
- ✅ `make grade` 三项全部 OK：`check PMM`、`check Process Create`、`check Schedule`
- ✅ 总分 `30/30`
- ✅ QEMU 输出 `[Test Schedule] PASS`，并打印 `this initproc, pid = 1, name = "init"` 与两行 `To U:` 输出
- 完整串口日志见 `./images/qemu_module3.log`

**关键改进点/核对点总结：**
1. `switch_to` 严格按 `struct context` 的偏移 `0/8/16/…/104` 保存和恢复 `ra, sp, s0~s11`，先保存 `from` 的全部字段再恢复 `to`，且不在切换后的栈上保存中间值。
2. `schedule` 处理了 `idleproc` 不在 `proc_list` 中的特殊情况：以 idle 为起点时从链表头扫描，否则从 `current` 的后继开始；扫描遇到哨兵 `&proc_list` 时跳过，保证有限步终止；无候选时回退 `idleproc`。
3. `proc_run` 的顺序固定为 `current = proc` → 按需写 `satp` + `flush_tlb()` → `switch_to`；`satp` 使用 Sv39 格式 `((uint64_t)SATP_MODE_SV39 << 60) | (pgdir >> RISCV_PGSHIFT)`，没有使用 Sv32 语义的 `lsatp()`。
4. `schedule` 与 `proc_run` 都用 `local_intr_save/restore` 保护临界区；`switch_to` 返回时 `local_intr_restore` 在旧线程被再次调度回来时执行，属于预期行为。
5. `do_exit` 的 panic 是框架占位（`kern/process/proc.c:209`），QEMU 日志末尾出现 `kernel panic ... process exit!!.` 不影响 `make grade` 的三项检查，未扩大改动范围。

---

## 五、测试与验证

本阶段验证范围：功能模块一（虚拟内存页操作）。在填好 lab2/lab3 依赖并实现 `page_insert`/`page_remove` 后执行 `make && make qemu`。

关键输出包括：
- `check_alloc_page() succeeded!`
- `check_pgdir() succeeded!`
- `check_boot_pgdir() succeeded!`
- `kmalloc_init() succeeded!` / `check_vmm() succeeded.`

说明：模块三仍为占位实现，故尚未跑通完整 `make grade` 的 Schedule 项；模块二的 Process Create 项已按 `test_lab4_process_create` 断言实现完成（见下）。

> 注：以上为模块一阶段的历史记录。模块三实现后，完整 `make grade` 的三项检查均已通过，结果见下方「模块三验证（进程调度）」。

**模块一测试截图：**

![测试结果截图](./images/test_result.png)

完整文本日志见：`./images/qemu_module1.log`

---

### 模块二验证（进程创建及初始化）

本模块通过注入到 `proc_init` 末尾的 `test_lab4_process_create()` 进行验证，关键断言：

- `current == idleproc`；
- idleproc：`pid == 0`、`state == PROC_RUNNABLE`、`kstack == bootstack`、`parent == NULL`、`mm == NULL`、`tf == NULL`、`runs == 0`、`flags == 0`、`context.ra == 0`；
- initproc：`pid == 1`、`state == PROC_RUNNABLE`、`parent == idleproc`、`mm == NULL`、`context.ra != 0`、`context.sp != 0`。

`make grade` 对应检查项：`[Test Process Create] PASS`。

> 验证：已在 WSL（riscv64-unknown-elf-gcc 14.2.0 + qemu-system-riscv64）中编译并运行，`[Test Process Create] PASS` 通过。完整文本日志见：`./images/qemu_module2.log`。

---

### 模块三验证（进程调度）

本模块通过注入到 `init_main` 开头的 `test_lab4_schedule()` 验证。测试流程与关键断言：

- `kernel_thread(test_lab4_thread, 1, 0)` 与 `kernel_thread(test_lab4_thread, 2, 0)` 创建两个测试线程；
- 测试线程各自循环 3 次，设置 `test_thread1_ran` / `test_thread2_ran` 标记，然后执行 `current->need_resched = 1; schedule();`；
- `test_lab4_schedule` 连续执行 8 次 `current->need_resched = 1; schedule();`，最后断言两个标记都非 0，并打印 `[Test Schedule] PASS`；
- 通过条件：`[Test Schedule] PASS`，且 initproc 输出 `this initproc, pid = 1, name = "init"`、`To U: "Hello world!!".`、`To U: "en.., Bye, Bye. :)"`。

`make grade` 完整结果：

```text
  -check PMM:                                OK
  -check Process Create:                     OK
  -check Schedule:                           OK
Total Score: 30/30
```

> 验证环境：WSL + `riscv64-unknown-elf-gcc (SiFive GCC-Metal 10.2.0-2020.12.8) 10.2.0` + `qemu-system-riscv64 10.2.1`。完整串口日志见 `./images/qemu_module3.log`。日志末尾的 `kernel panic at kern/process/proc.c:209: process exit!!.` 来自框架中尚未实现的 `do_exit` 占位，发生在三项测试全部 PASS 之后，不影响 grade 结果。

---

## 六、实验总结与收获

### 对操作系统的理解

1. 列出你们认为本实验中重要的知识点，以及与对应的 OS 原理中的知识点，并简要说明二者的含义、关系和差异
2. 列出你们认为 OS 原理中很重要、但在本实验中没有对应上的知识点

### AI 协作开发的经验

[写出你们在与 AI 协作过程中实际收获的经验或者感悟]

---

