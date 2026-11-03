[PROMPT]
**任务**：修改 `kern/trap/trap.c`，完成「中断与异常处理」模块。
本模块位于 Trap 入口 `__alltraps` 已完成上下文保存并调用 `trap(tf)` 之后：
`trap()` 调用 `trap_dispatch()`，后者按 `tf->cause` 最高位分流——最高位为 1 走
`interrupt_handler(tf)`，为 0 走 `exception_handler(tf)`。本模块负责补全这两个函数，
使系统能够正确处理时钟中断、非法指令异常和断点异常，并与后续
`__trapret → RESTORE_ALL → sret` 的返回机制正确衔接。

**操作要求**：
- 必须在项目中修改实际文件 `kern/trap/trap.c`，而不是只展示代码片段。
- 只补全 `interrupt_handler()` 与 `exception_handler()` 的函数体；保留 `trap()`、
  `trap_dispatch()`、`idt_init()`、`print_trapframe()`、`print_regs()`、
  `print_ticks()` 等已有函数不变。
- 不得修改两个函数的签名：
  `void interrupt_handler(struct trapframe *tf);`
  `void exception_handler(struct trapframe *tf);`
- 不得修改 `struct trapframe` / `struct pushregs` 的布局。
- 不得引入与本模块无关的功能（进程、缺页、系统调用等一律不实现）。
- 仓库中若已存在 lab1 遗留的实现，请把它视为待替换版本，按本规格重写这两个函数；
  特别是：不得保留函数内 `static int ticks` 遮蔽全局 `ticks`，不得把关机次数写成
  字面量 10，不得对两类异常共用固定的 `epc` 偏移。
- 先检查工程中是否已定义 `SHUTDOWN_AFTER`：
  - 已有：直接使用，不得重复定义；
  - 没有：在 `TICK_NUM` 附近补充 `#define SHUTDOWN_AFTER 10`，不得改动其他宏。
- 时钟计数使用 `<clock.h>` 中已有的全局 `extern volatile size_t ticks`
  （`clock_init()` 已将其清零）；不得在 `interrupt_handler()` 内再定义同名变量。
- 可复用文件内已有的 `TICK_NUM`、`num`、`print_ticks()`。

**输出要求**：
- 直接进行文件操作。
- 以工程真实代码及下列 [RELY]、[GUARANTEE]、[SPECIFICATION] 为准。
- 若讲义规定的输出字符串与本文件模板不同，以讲义为准，并在代码注释中注明差异。

[RELY]

```c
/* libs/riscv.h：cause 最高位为 1 表示中断，为 0 表示异常 */
#define IRQ_U_SOFT   0
#define IRQ_S_SOFT   1
#define IRQ_H_SOFT   2
#define IRQ_M_SOFT   3
#define IRQ_U_TIMER  4
#define IRQ_S_TIMER  5
#define IRQ_H_TIMER  6
#define IRQ_M_TIMER  7
#define IRQ_U_EXT    8
#define IRQ_S_EXT    9
#define IRQ_H_EXT    10
#define IRQ_M_EXT    11

#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT          0x3
/* 其余 CAUSE_* 与 IRQ_* 宏均已在 riscv.h 中定义 */
```

```c
/* kern/trap/trap.h —— 布局不得修改 */
struct pushregs {
    uintptr_t zero, ra, sp, gp, tp;
    uintptr_t t0, t1, t2;
    uintptr_t s0, s1;
    uintptr_t a0, a1, a2, a3, a4, a5, a6, a7;
    uintptr_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uintptr_t t3, t4, t5, t6;
};
struct trapframe {
    struct pushregs gpr;   /* 32 * REGBYTES */
    uintptr_t status;      /* 偏移 32 */
    uintptr_t epc;         /* 偏移 33 */
    uintptr_t badvaddr;    /* 偏移 34 */
    uintptr_t cause;       /* 偏移 35 */
};
void trap(struct trapframe *tf);
void print_trapframe(struct trapframe *tf);
```

```c
/* kern/trap/trap.c 中已存在且不得修改的部分 */
#define TICK_NUM 100
volatile size_t num = 0;          /* 已输出 TICK_NUM 轮统计的次数 */

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        interrupt_handler(tf);    /* 中断 */
    } else {
        exception_handler(tf);    /* 异常 */
    }
}

void trap(struct trapframe *tf) { trap_dispatch(tf); }
```

```c
/* kern/driver/clock.h 与 clock.c */
extern volatile size_t ticks;      /* 全局时钟中断计数，clock_init() 已清零 */
void clock_set_next_event(void);   /* 设置下一次 S 态定时器事件 */
```

```c
/* libs/sbi.h */
void sbi_shutdown(void);           /* 关机，不返回 */
```

说明：
- `print_trapframe(tf)` 会完整打印 Trap 现场并正常返回，可直接作为兜底。
- `print_ticks()` 在 `DEBUG_GRADE` 下会打印 "End of Test." 后 `panic`，
  这是评分框架的预期行为，不得修改或绕过。
- 当前工程未定义 `SHUTDOWN_AFTER`，按 [GUARANTEE] 处理。

[GUARANTEE]
必须补全：`interrupt_handler()`、`exception_handler()` 的全部处理逻辑。
禁止修改：两个函数的签名、`trap()`、`trap_dispatch()`、`idt_init()`、
`print_trapframe()`、`print_regs()`、`print_ticks()`、`struct trapframe` 布局。
禁止：在 `interrupt_handler()` 内定义遮蔽全局 `ticks` 的局部变量。
禁止：对非法指令和断点异常写死同一个 `epc` 偏移量。
禁止：处理完异常后不更新 `tf->epc`，导致 `sret` 后重复执行同一条异常指令。
禁止：在 C 处理函数中直接执行 `sret` / `RESTORE_ALL`。

[SPECIFICATION]

## interrupt_handler(struct trapframe *tf)

**Pre-Condition**：`tf` 指向有效的 trapframe；由 `trap_dispatch()` 在
`(intptr_t)tf->cause < 0`（即中断）时调用。

**Post-Condition**：完成本类中断的处理并正常返回；控制流回到
`trap_dispatch()` → `trap()` → `__alltraps` → `__trapret` → `RESTORE_ALL` → `sret`。
除 `ticks`、`num`、定时器状态外，不破坏 `tf` 中的现场。

**Case 1：IRQ_S_TIMER（必须完整实现）**
1. 在分支开头调用 `clock_set_next_event()`，保证后续仍能收到时钟中断；
2. `ticks++`（使用全局 `ticks`）；
3. 当 `ticks % TICK_NUM == 0` 时：`num++`，然后调用 `print_ticks()`；
4. 当 `num >= SHUTDOWN_AFTER` 时：调用 `sbi_shutdown()`；
5. `sbi_shutdown()` 之后不得再执行本分支的后续逻辑。

**Case 2：其他已定义中断（IRQ_*）**
保留模板中已有的分支与类型提示输出，不添加新的处理逻辑。

**Case 3：未识别中断**
`default` 分支调用 `print_trapframe(tf)` 输出完整现场。

**Requirements**：
- 进入函数后先去掉中断位再 `switch`：
  `intptr_t cause = (tf->cause << 1) >> 1;`
  不能直接用未处理的 `tf->cause` 匹配 `IRQ_*`。
- 不得只做计数而不调用 `clock_set_next_event()`，否则系统只会收到一次时钟中断。
- 不得把 `TICK_NUM` / `SHUTDOWN_AFTER` 写成魔法数字。
- 不得在中断处理中调用可能阻塞或进入 `kmonitor` 的函数。

## exception_handler(struct trapframe *tf)

**Pre-Condition**：`tf` 指向有效的 trapframe；由 `trap_dispatch()` 在
`tf->cause` 最高位为 0（即异常）时调用。

**Post-Condition**：
- 非法指令 / 断点异常：按指令长度前移 `tf->epc`，使 `sret` 返回后执行下一条指令；
- 其他异常：输出现场后正常返回；
- 不修改 `tf->cause`、`tf->status`。

**Case 1：CAUSE_ILLEGAL_INSTRUCTION**
逐字输出：
```c
cprintf("Exception Type: Illegal instruction\n");
cprintf("Illegal instruction caught at %p\n", tf->epc);
```
然后 `tf->epc += <该地址处指令的字节长度>`。

**Case 2：CAUSE_BREAKPOINT**
逐字输出：
```c
cprintf("Exception Type: breakpoint\n");
cprintf("ebreak caught at %p\n", tf->epc);
```
然后 `tf->epc += <该地址处指令的字节长度>`。

**Case 3：其他异常**
对模板中未要求单独处理的异常统一调用 `print_trapframe(tf)` 兜底；
可以保留已有 `case` 标签，但不得用空 `break` 静默吞掉异常。
`default` 同样调用 `print_trapframe(tf)`。

**Requirements**：
- 必须在修改 `tf->epc` 之前，从 `tf->epc` 地址读取指令并判断长度，例如：
```c
uint16_t insn = *(volatile uint16_t *)tf->epc;
int len;
if ((insn & 0x3) != 0x3) {
    len = 2;                       /* 16 位压缩指令 */
} else if ((insn & 0x1f) != 0x1f) {
    len = 4;                       /* 32 位标准指令 */
} else if ((insn & 0x7f) == 0x3f) {
    len = 6;                       /* 48 位指令（本实验可仅注释说明） */
} else if ((insn & 0x7f) == 0x7f) {
    len = 8;                       /* 64 位指令（本实验可仅注释说明） */
} else {
    len = 4;                       /* 本实验不会出现，保守处理 */
}
```
- 本实验至少要正确处理 2 字节和 4 字节两种情况；不得假设
  "非法指令一定是 4 字节"或"ebreak 一定是 2 字节"。
- 两条异常各自只前移一次，且前移量来自同一次指令读取；不得重复累加。
- 地址统一用 `%p` 输出，不得用 `%08x` 之类只覆盖低 32 位的格式。
- 若无法可靠判断指令长度，不得盲目修改 `tf->epc`，应调用
  `print_trapframe(tf)` 并注释说明。
- 长度判断建议直接写在函数内；如需抽取辅助函数，只能是本模块新增的
  `static` 小函数，不得改动其他既有函数。

[验收标准]
1. `make` 编译通过；
2. `make qemu` 输出 `(THU.CST) os is loading ...`、`++ setup timer interrupts`，
   随后持续输出 10 次 `100 ticks` 并触发关机；
3. `make grade` 输出 `Total Score: 100/100`；
4. 非法指令 / 断点测试中：
   - 输出格式与上面逐字一致；
   - 每次异常只打印一次，不出现同一地址的重复打印（说明 `epc` 已正确前移）；
   - `sret` 后能继续执行异常指令之后的下一条指令。
