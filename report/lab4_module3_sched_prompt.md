# Lab4 功能模块三提示词（进程调度）+ Challenge 提示词

> 使用方式：优先使用「提示词 A」一次性实现；若模型漏项或编译失败，改用「提示词 B」分步实现；出现断言/死循环时使用「提示词 C」。Challenge 使用「提示词 D」，并追加「提示词 D+」做交叉核验。
>
> 使用前请确认 AI 能直接读取仓库（Claude Code / Cursor / IDE Agent）；若只能对话，请把提示词 A 中 `[RELY]` 涉及的四个源文件一并附上。

---

## 一、提示词 A：主实现提示词（一次性完成四个函数）

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

---

## 二、提示词 B：分步实现（模型一次做不对时使用）

按 `switch_to → schedule → proc_run → cpu_idle` 的顺序分四次提问，每次只改一个文件/函数，便于定位错误。

### B1：switch_to

````markdown
[PROMPT]
只修改 `kern/process/switch.S`，实现 `void switch_to(struct context *from, struct context *to)`。

约束与事实：
- 本工程是 RV64；`struct context` 字段顺序为 ra, sp, s0, s1, ..., s11，各 8 字节，偏移 0/8/16/.../104。
- `switch_to` 必须把当前现场保存到 `from`，再从 `to` 恢复并 `ret` 到 `to->ra`。
- 不得使用当前栈保存中间值，因为 sp 会被切换。
- 只使用 a0/a1 与 caller-saved 暂存寄存器；不得破坏目标 s0-s11。
- `switch_to` 之后，新线程会经 `forkret -> forkrets -> __trapret -> sret` 进入线程函数，因此 `ra`/`sp` 必须严格对应 `context` 中的值。

输出：直接改文件，并在代码中用注释标出每个 sd/ld 对应的字段；说明为什么先保存 a0/a1 指向的所有字段再恢复。
````

### B2：schedule

````markdown
[PROMPT]
只修改 `kern/schedule/sched.c`，实现 `void schedule(void)`。

约束与事实：
- `proc_list` 是双向链表，哨兵为 `&proc_list`；`idleproc` 不在其中。
- 使用 `local_intr_save/restore` 保护整个选择与切换过程。
- 先把 `current->need_resched = 0`。
- 从当前线程的下一个节点开始轮转扫描 `PROC_RUNNABLE`；若 `current == idleproc`，从链表头开始。
- 扫描一周无候选则选择 `idleproc`；被选中者 `runs++`；仅当 `next != current` 时调用 `proc_run(next)`。
- 不得把哨兵交给 `le2proc`；不得死循环。

输出：直接改文件；说明扫描终止条件、idle 回退条件，以及为什么临界区必须覆盖 `proc_run`。
````

### B3：proc_run + cpu_idle

````markdown
[PROMPT]
只修改 `kern/process/proc.c` 中模块三占位块，实现 `proc_run` 与 `cpu_idle`。

约束与事实：
- `proc_run(proc)`：若 `proc == current` 直接返回；否则在 `local_intr_save/restore` 内依次：
  `current = proc` → 若 `proc->pgdir != prev->pgdir` 则写 satp 并 flush_tlb → `switch_to(&prev->context, &proc->context)`。
- 设置 satp 必须写成 `write_csr(satp, ((uint64_t)SATP_MODE_SV39 << 60) | (proc->pgdir >> RISCV_PGSHIFT))`，并在之后 `flush_tlb()`；不得使用 `lsatp()`。
- `cpu_idle`：`while (1) { if (current->need_resched) schedule(); }`，不得返回。
- 不得修改模块二的函数，不得实现 `do_exit`。
- 说明：`switch_to` 返回时可能已经切换到新线程；`local_intr_restore` 会在旧线程再次被调度时执行，这是预期行为。

输出：直接改文件；逐条解释 `current`、`satp`、`switch_to` 三者的顺序为何不能交换。
````

---

## 三、提示词 C：调试与验收提示词

出现编译失败、断言失败、死循环或 `make grade` 不通过时，把实际日志贴给 AI，而不是让它“重写一遍”。

````markdown
[PROMPT]
下面是本工程模块三实现后的真实现象（编译/运行日志见下）。请做根因分析，不要整段重写。

已知实现：
- `switch_to` 保存/恢复 ra, sp, s0-s11；
- `schedule` 在 local_intr_save 内轮转选择 RUNNABLE，找不到则 idleproc，选中者 runs++，next != current 时 proc_run；
- `proc_run` 在临界区内 current=proc、按需写 satp + flush_tlb、switch_to(&prev->context, &proc->context)；
- `cpu_idle` 在 need_resched 时 schedule()。

现象：
```
<在此粘贴 make / make qemu / make grade 的完整相关输出>
```

请按以下顺序回答：
1. 现象对应的最可能根因（按概率排序，最多 3 个），并指出是哪个函数/哪一行；
2. 每个根因的验证方法（加什么打印、看哪个符号、用什么 gdb/objdump 命令）；
3. 最小修复补丁（只改必要的行）；
4. 回归验证步骤：应出现哪些输出、`make grade` 的期望结果；
5. 如果无法从日志判断，明确列出需要补充的信息，不要猜测。

特别检查以下高频问题：
- 是否把 `satp` 写成了 Sv32 格式；
- `current` 是否在 `switch_to` 之后才更新；
- `switch_to` 是否在恢复 `sp` 后才保存旧 `sp`；
- `schedule` 是否把 `&proc_list` 当成进程或无法终止扫描；
- 是否忘记清 `need_resched` 或重复 `runs++`；
- `local_intr_restore` 是否在 `switch_to` 之前执行。
````

---

## 四、提示词 D：Challenge 任务（开关中断 / 分页模式 / get_pte）

Challenge 是“读代码 + 写报告”，应显式要求 AI 只读、引用行号、禁止编造。

````markdown
[PROMPT]
**任务**：只读分析本仓库代码，完成 Lab4 扩展练习 Challenge 的四问，输出可直接写入实验报告的中文 Markdown。不得修改任何源文件。

**必读文件**（请实际打开并引用 `file:line`，不要凭记忆回答）：
- `kern/sync/sync.h`、`kern/driver/intr.c`、`libs/riscv.h`（`read_csr/set_csr/clear_csr`、`SSTATUS_SIE`、`satp` 相关宏）
- `kern/trap/trapentry.S`（SAVE_ALL / RESTORE_ALL / sret 对 sstatus 的处理）
- `kern/mm/mmu.h`（`PDX1/PDX0/PTX`、`PTE_*`、`PTE_ADDR`、`PTE_PPN_SHIFT`）
- `kern/mm/pmm.c`（`get_pte`、`enable_paging`、`tlb_invalidate`）
- `kern/init/entry.S`（`boot_page_table_sv39` 的构建方式）

**Q1. 开关中断机制**
说明 `local_intr_save(intr_flag); ... local_intr_restore(intr_flag);` 是如何实现开关中断的，至少覆盖：
- `__intr_save` 如何读取并判断 `sstatus.SIE`，为什么返回值表示“进入临界区前中断是否开启”；
- 为什么不能简单地分别调用 `intr_disable()` / `intr_enable()`；
- 嵌套临界区、临界区内发生中断并 `sret` 返回时，`sstatus.SIE/SPIE` 如何配合；
- 该宏实现的一个潜在写法问题（注意 `local_intr_restore` 宏的展开形式）及其后果；
- 与本模块 `schedule`/`proc_run` 原子性的关系。

**Q2. Sv32 / Sv39 / Sv48 的异同**
给出一张对比表，至少包含：虚拟地址位宽、页表级数、每级 VPN 位数、PTE 大小、每张页表可容纳项数、`satp.MODE` 值、物理地址 PPN 位宽、支持的页大小/大页层级、地址规范化要求。
并结合 `kern/mm/mmu.h` 说明本工程为何使用 `PDX1(la)`、`PDX0(la)`、`PTX(la)` 三个索引，`PDX1SHIFT=30`、`PDX0SHIFT=21`、`PTXSHIFT=12` 分别对应 Sv39 的哪一级。

**Q3. get_pte 中两段相似代码的成因**
阅读 `kern/mm/pmm.c::get_pte`，解释：
- 两段 `if (!(*pdepX & PTE_V)) { alloc_page; set_page_ref; memset; *pdepX = pte_create(...); }` 分别在处理哪一级页表；
- 为什么它们形式几乎一样：这与 Sv32/Sv39/Sv48 的页表遍历结构有什么关系；
- 为什么本工程只有两段而不是 Sv39 应有的三段（结合 `boot_page_table_sv39` 的静态构建与根页表作用）；
- 如果换成 Sv48，按当前写法应增加什么；如果根页表也允许动态创建，代码结构应如何变化；
- 一个潜在边界：当某一级页表项是“大页叶子”（如启动页表中的 1 GiB 映射）时，`get_pte` 把它当作下一级页表指针会发生什么？本工程是否会在这种地址上调用 `get_pte`？

**Q4. get_pte 是否应拆分**
结合 `get_pte(pgdir, la, create)`、`get_page(pgdir, la, ptep_store)`、`page_insert/page_remove` 的实际调用方式，讨论：
- 当前“查找 + 按需分配”合一的优点与问题（重点：`NULL` 返回值语义、错误码、只读查询的副作用、页表页泄漏、并发/中断安全）；
- 拆成“纯查找 walk”与“分配并挂接 alloc”两种接口的收益与代价；
- 如果保持现有签名不变，内部可以如何重构以减少重复；
- 给出你的结论：本实验应保持还是拆分，生产内核通常怎么做，理由是什么。

**输出格式**：
1. 「结论摘要」5 条以内；
2. Q1–Q4 逐问作答，每问先给结论，再给代码依据（`file:line`）与机制解释；
3. 必要的表格、页表遍历示意图（可用文本箭头表示）；
4. 最后一节「本实验的具体结论」：这些机制分别如何支撑 `switch_to`、`proc_run`、`schedule`；
5. 如果某处无法从仓库代码确认，单独列出「未确认点」，禁止臆造。
````

### D+：交叉核验提示词（让 AI 自检报告）

````markdown
[PROMPT]
请以“助教批改实验报告”的标准审查下面这份 Challenge 答案：
1. 逐条核对每个论断是否能在 `kern/sync/sync.h`、`kern/driver/intr.c`、`kern/mm/mmu.h`、`kern/mm/pmm.c`、`kern/init/entry.S` 中找到依据；标出无依据或与代码矛盾的句子；
2. 检查 Sv32/Sv39/Sv48 对比表中的数字是否准确（VA 位宽、级数、VPN 位数、PTE 大小、satp.MODE、PPN 位宽、大页大小）；
3. 检查是否混淆了“本工程实现”与“RISC-V 规范”：本工程根页表静态、`get_pte` 把大页叶子当表指针等问题必须明确说明；
4. 给出修改后的最终版，保留原有结构，只改错误与不严谨之处；
5. 列出仍然存疑、需要查阅 RISC-V Privileged Spec 的条目。

待审查答案：
```
<粘贴 Q1–Q4 答案>
```
````

---

## 五、人工验收清单（不必发给 AI）

实现完成后逐项核对：

- [ ] `kern/process/switch.S` 的保存/恢复偏移为 0, 8, 16, …, 104，顺序为 `ra, sp, s0..s11`。
- [ ] `switch_to` 中先保存 `from` 全部字段，再恢复 `to`；没有在恢复 `sp` 后回头写旧栈。
- [ ] `schedule` 的临界区覆盖“清 need_resched + 选择 + `proc_run`”；扫描能终止；`idleproc` 不在 `proc_list` 这一事实被正确处理。
- [ ] `schedule` 找不到可运行线程时回退 `idleproc`；`next == current` 时不调用 `proc_run`。
- [ ] `proc_run` 顺序为 `current = proc` → 页表（如需）→ `switch_to`。
- [ ] `satp` 写法为 Sv39（`8ULL << 60 | pa >> 12`），且写后执行 `flush_tlb()`；全仓库没有调用 `lsatp()`。
- [ ] `cpu_idle` 只在 `need_resched` 时 `schedule()`，且永不返回。
- [ ] `make` 无警告级错误；QEMU 输出 `[Test Schedule] PASS`；`make grade` 的 `check Schedule` 为 OK。
- [ ] 没有修改模块一、模块二、`do_exit`、结构体定义或测试脚本。

### 常见失败现象与优先排查点

| 现象 | 优先排查 |
|---|---|
| 链接报 `switch_to` 未定义/重复定义 | 文件实际为 `kern/process/switch.S`；占位块是否被完整替换；`.globl` 是否保留 |
| 卡在 `cpu_idle` 不再输出 | `schedule` 是否从未选中 initproc；`need_resched` 是否被清零；`proc_list` 遍历起点/终止条件 |
| 第一次切换 triple fault / 取指异常 | `satp` 是否写成 Sv32；是否漏 `flush_tlb`；`context.sp` 是否被 switch_to 破坏 |
| 反复打印 / 测试线程不推进 | `need_resched` 未清零；`schedule` 总选回当前线程；`runs++` 位置导致选择偏差 |
| `[Test Schedule] PASS` 后 panic | 框架 `do_exit` 占位导致，属预期，不在模块三范围 |
| 两个测试线程只有一个运行 | 轮转起点错误；`current == idleproc` 分支处理错误；哨兵被当成进程 |

---

## 六、建议的提示词迭代记录模板（写入实验报告）

每个模块保留“提示词版本 → 现象 → 根因 → 修改”的闭环，便于报告体现迭代过程：

```markdown
### 第 N 次迭代
- 提示词改动：
- 生成/修改内容：
- 实际现象（make / qemu / grade 输出）：
- 根因：
- 修复：
- 回归结果：
```
