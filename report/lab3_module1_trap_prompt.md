# Lab3 功能模块一最终提示词

````markdown
[PROMPT]
**任务**：修改 `kern/trap/trapentry.S`，完成「Trap 上下文恢复与 Trap 入口」模块。
本模块负责在已有 `SAVE_ALL` 把 Trap 现场保存到当前内核栈上的 `struct trapframe` 之后，建立汇编入口与 C 语言 `trap(struct trapframe *tf)` 的正确调用关系，并实现与 `SAVE_ALL` 对应的 `RESTORE_ALL`，使系统形成完整闭环：
Trap 进入 → 上下文保存 → C 语言处理 → 上下文恢复 → `sret` 返回原执行流。

**操作要求**：
- 必须在项目中修改实际文件 `kern/trap/trapentry.S`，而不是仅展示代码片段。
- 保留文件中现有代码；只补全 `RESTORE_ALL` 宏，以及 `__alltraps` 中 `SAVE_ALL` 之后、`__trapret` 之前需要补全的位置。
- 不得修改已提供的 `SAVE_ALL` 宏语义（仅允许工具链兼容的 CSR 别名替换）。
- 不得修改 `struct trapframe` / `struct pushregs` 的布局与定义，不得修改 `void trap(struct trapframe *tf)` 的函数签名。
- 不得改动 `__trapret` 中已有的 `RESTORE_ALL` 调用与随后的 `sret` 控制流结构。
- 不得引入与本模块无关的功能（时钟处理、异常分类等属于其他模块）。

**输出要求**：
- 直接进行文件操作。
- 以工程真实代码及下列 [RELY]、[GUARANTEE]、[SPECIFICATION] 为准。

[RELY]
#if __riscv_xlen == 64
# define STORE    sd
# define LOAD     ld
# define LOG_REGBYTES 3
#else
# define STORE    sw
# define LOAD     lw
# define LOG_REGBYTES 2
#endif
#define REGBYTES (1 << LOG_REGBYTES)

struct trapframe {
    struct pushregs gpr;   // 0..31 * REGBYTES
    uintptr_t status;      // 32
    uintptr_t epc;         // 33
    uintptr_t badvaddr;    // 34，对应 sbadaddr/stval（同 CSR 0x143）
    uintptr_t cause;       // 35
};

// SAVE_ALL：原始 sp 进 sscratch → 下移 36*REGBYTES → 存 x0/x1/x3..x31 →
// 把 sscratch 中的原始 sp 写入偏移 2 → 存 sstatus/sepc/stval/scause。
// 结束后当前 sp 即 trapframe 指针。

void trap(struct trapframe *tf);

[GUARANTEE]
必须补全：RESTORE_ALL 宏；__alltraps 中 SAVE_ALL 之后对 trap() 的调用衔接。
禁止修改 trap() 签名、trapframe 布局、__trapret 的 RESTORE_ALL+sret 顺序。

[SPECIFICATION]
## RESTORE_ALL
**Pre-Condition**: sp 指向完整 trapframe（可能已被 trap() 更新）。
**Post-Condition**: 通用寄存器与 sstatus/sepc 恢复为帧中最终值；x2(sp) 从 gpr.sp 恢复且必须最后恢复；可安全 sret。
**Case 1**: 与 SAVE_ALL 互逆恢复；先 CSR，再通用寄存器，最后 sp。
**Requirements**: 不与 SAVE_ALL 布局冲突；不规定唯一指令序列。

## __alltraps
**Pre-Condition**: SAVE_ALL 后 sp 指向 trapframe。
**Post-Condition**: 以 a0 传递 trapframe 指针并调用 trap()；返回后进入 __trapret。
**Case 1**: move a0,sp；jal trap。
**Requirements**: 遵守 RISC-V 调用约定；不破坏后续恢复所需信息。
````
