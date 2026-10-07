# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 1: 最小可执行内核 |
| **小组成员** | 2413157-张书晗、2411514-蒲富坤、2412888-刚栋冰 |
| **完成日期** | 2026-10-07 |

### 小组分工

练习如何分工？

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2413157-张书晗 | |
| 2411514-蒲富坤 | |
| 2412888-刚栋冰 | |

实验报告如何分工？

---

## 一、实验目的

<!-- 说明本次实验的主要目标和预期成果 -->

本实验的主要目的是：

1. 
2. 
3. 

---

## 二、实验环境

你们使用的 AI 工具

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2413157-张书晗 | | | |
| 2411514-蒲富坤 | | | |
| 2412888-刚栋冰 | | | |

**说明：**
- **AI 编程工具**：指具体使用的终端工具、编辑器插件、桌面应用或浏览器界面
- **底层模型**：指该工具使用的大语言模型及版本

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

```
┌──────────────────────────────────────────────────────┐
│ 用户接口层（cprintf）
|
|内核代码中实际调用的入口函数
|核心文件/函数：kern/libs/stdio.c 中的cprintf()
├──────────────────────────────────────────────────────┤← vcprintf() 用来适配参数
│ 格式化引擎层（printfmt）
|
|不包含任何硬件操作，只负责解析 %s、%d 等格式符
|核心文件/函数：libs/printfmt.c 中的 vprintfmt()
├──────────────────────────────────────────────────────┤
│ 驱动抽象层（Console 驱动）
|
|将具体的 SBI 功能号调用隐藏起来，对外提供统一的 cons_putc 函数
|核心文件/函数：kern/driver/console.c 中的 cons_putc()
├──────────────────────────────────────────────────────┤
│ 硬件交互层（SBI 接口）
|
|内核运行在 S-mode，无法直接操作硬件，必须通过 ecall 指令调用 M-mode 的 OpenSBI 固件。
|核心文件/函数：libs/sbi.c 中的 sbi_call()和sbi_console_putchar()
└──────────────────────────────────────────────────────┘ 
  硬件
```

### 2.2 功能的逐步实现

### 第一层：`cprintf（kern/libs/stdio.c）`
```c
int cprintf(const char *fmt, ...) {
    va_list ap;
    int cnt;
    va_start(ap, fmt);      // 初始化可变参数指针，使其指向 fmt 之后的第一个参数
    cnt = vcprintf(fmt, ap); // 将格式化工作委托给 vcprintf
    va_end(ap);             // 清理可变参数状态
    return cnt;
}
```
- `va_list`、`va_start`、`va_end` 定义在实验自制的 `libs/stdarg.h` 中，它们展开为 GCC 内置的 `__builtin_va_start` 等。这些内置功能是编译器直接生成的指令序列，用于在栈上定位可变参数，不属于任何运行时库。

### 第二层：`vcprintf（kern/libs/stdio.c）`
```c
int vcprintf(const char *fmt, va_list ap) {
    int cnt = 0;
    vprintfmt((void *)cputch, &cnt, fmt, ap);
    return cnt;
}
```
- 此函数将字符输出操作抽象为函数指针 `cputch`，传递给通用的格式化引擎 `vprintfmt`。这种设计使格式化逻辑与具体输出设备解耦。

### 第三层：`vprintfmt（libs/printfmt.c）`
- 这是纯算法函数，不包含任何硬件操作。
- 它逐字符扫描格式串 `"%s\n\n"`：
  - 遇到 `%s`：通过 `va_arg(ap, char *)` 取出 `message` 指针，然后循环读取该字符串的每个字符，每读一个就调用一次传入的 `putch` 函数指针（即 `cputch`）。
  - 遇到 `\n`：直接调用 `putch('\n', &cnt)`。
- 数字格式化（`%d`、`%x`）所需的除法运算使用 `libs/riscv.h` 中的 `do_div` 宏实现，字符串长度计算使用 `libs/string.c` 中的 `strnlen`，均不依赖外部库。

### 第四层：`cputch（kern/libs/stdio.c）`
```c
static void cputch(int c, int *cnt) {
    cons_putc(c);   // 调用控制台驱动层
    (*cnt)++;       // 更新已输出字符计数
}
```

### 第五层：`cons_putc（kern/driver/console.c）`
```c
void cons_putc(int c) {
    sbi_console_putchar((unsigned char)c);
}
```
- 在 lab1 阶段，此函数仅做类型转换并转发。预留这一层是为了后续实验中接入键盘输入、串口中断等多设备管理。

### 第六层：`sbi_console_putchar（libs/sbi.c）`
```c
void sbi_console_putchar(unsigned char ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
}
```
- `SBI_CONSOLE_PUTCHAR` 是 OpenSBI 规范定义的功能编号，值为 1。

### 第七层：`sbi_call（libs/sbi.c）`——特权级切换点
```c
uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t ret_val;
    __asm__ volatile (
        "mv x17, %[sbi_type]\n"  // a7 = 功能编号
        "mv x10, %[arg0]\n"      // a0 = 第一个参数（字符值）
        "mv x11, %[arg1]\n"      // a1 = 第二个参数
        "mv x12, %[arg2]\n"      // a2 = 第三个参数
        "ecall\n"                 // 触发环境调用异常，CPU 从 S-mode 陷入 M-mode
        "mv %[ret_val], x10"      // 读取返回值
        : [ret_val] "=r" (ret_val)
        : [sbi_type] "r" (sbi_type), [arg0] "r" (arg0),
          [arg1] "r" (arg1), [arg2] "r" (arg2)
        : "memory"
    );
    return ret_val;
}
```

---

## 四、实验内容与实现

### 练习一

**负责人：** 

![练习1](pic/练习1.png)

- `la sp, bootstacktop`：
  - la = Load Address（伪指令，实际会被展开为 lui + addi 两条指令）
  - sp = Stack Pointer（栈指针寄存器）
  - bootstacktop = 下面定义的内核栈的顶部地址

- `tail kern_init`：
  - tail 是 RISC-V 的尾调用优化伪指令。
  - 它等价于 j kern_init（无条件跳转）。
  - 为什么不用 call？因为 call 会把返回地址压入栈并期望将来返回。但内核入口函数永远不会返回，因为Bootloader 已经结束了，因此用 tail/j 。

---

### 练习二

**负责人：** 

首先，我们开两个终端：终端 A 执行 `make debug`，终端 B 执行 `make gdb` 连接。连接后发现当前 `PC = 0x1000`，GDB 显示 `?? ()` 是因为复位 ROM 不在内核符号表里，说明 RISC-V 在 QEMU virt 上加电后从复位地址 `0x1000` 开始执行。

![上电后](pic/上电后.png)

然后，我们用 `x/8i 0x1000` 反汇编最初几条指令，前 6 条是有效代码，第6条跳转到 `t0`，即进入 OpenSBI；`0x1018` 起已进入数据区，但 GDB 仍按指令解码成 `unimp` 和 `.insn`，这是反汇编器把立即数当指令看，不会被 CPU 执行，因为上一条已经 `jr` 走了。

<img src="pic/反汇编最初几条指令.png" alt="反汇编最初几条指令" width="80%" />


然后，我们用 `x/8gx 0x1000` 查看旁路数据表。小端机器码，对应上面已反汇编的前四条：`0x00000297` → `auipc t0,0`，`0x02828613` → `addi a2,t0,40`， `0xf1402573` → `csrr a0,mhartid`，`0x0202b583` → `ld a1,32(t0)`。左字`0x0182b283`（`ld t0,24(t0)`）和 `0x00028067`（`jr t0`），右字（地址 **0x1018**）：`0x80000000`，OpenSBI 入口，即 `jr` 的目标。


然后，我们连续 `si` 六次。PC 依次经过 `0x1004`、`0x1008`、`0x100c`、`0x1010`、`0x1014`，第 6 步后变为 `0x80000000`，与数据表中的跳转目标一致，最后 `jr t0` 进入 OpenSBI。

![旁路数据表与六次 si](pic/旁路数据表+6次si.png)

然后，我们在 OpenSBI 入口（PC `0x80000000`）执行 `x/4i 0x80200000`，发现此处已经是 `kern_entry`：`auipc`/`mv` 对应源码 `la sp, bootstacktop`，随后 `j kern_init` 对应 `tail kern_init`。

最后，我们执行 `b *0x80200000` 再 `continue`，断点命中 `kern/init/entry.S` 第 7 行的 `kern_entry`（`la sp, bootstacktop`）。`p/x $pc` 得到 `0x80200000`，`x/5i $pc` 显示内核入口指令，再 `si` 三次：先执行 `la` 的前半、再走到 `tail kern_init`、最后进入 `kern_init` 的 `memset`清 BSS。

<img src="pic/OpenSBI%20入口.png" alt="OpenSBI 入口" />

综上所述，完整路径为：`0x1000`（复位 ROM，准备参数并跳转）→ `0x80000000`（OpenSBI 固件初始化）→ `0x80200000`（内核开始执行）。加电后最初几条指令位于 `0x1000`，主要功能是准备参数，并跳转到 `0x80000000` 的 OpenSBI。

---

## 五、测试与验证

---

## 六、实验总结与收获

### 对操作系统的理解

本实验主要涉及 RISC-V 启动过程和内核初始化的关键机制，具体包括以下几个方面：

**1. RISC-V 启动地址与初始指令执行流程**  RISC-V CPU 在加电复位后，会从固定的物理地址 0x1000 开始执行指令。  从 0x1000 到 0x1010 的这几条指令主要完成了以下功能：

* 利用 `auipc` 和 `addi` 计算基地址和偏移，用于定位内核入口地址；
* 通过 `csrr a0, mhartid` 读取当前 hart ID，确定是哪个 CPU 核心；
* 使用 `ld` 从内存中加载内核入口地址；
* 最后用 `jr` 跳转到 0x80000000，将控制权交给 OpenSBI 或内核启动代码。

**2. 栈的初始化与内核入口跳转**  在进入内核之前，需要先设置好内核栈。 `la sp, bootstacktop` 指令将内核预留的栈顶地址加载到栈指针寄存器 sp 中，为内核 C 代码的运行准备好基本环境。  随后，使用 `tail kern_init`（伪指令，实际是跳转）进入内核的 `kern_init` 函数，开始进行进一步的初始化工作。

**3. GDB 调试的使用**  本实验通过 QEMU + GDB 对启动流程进行了精确的跟踪和验证。  常用的调试命令包括：

* `b *0x1000`：在指定地址打断点；
* `si`：单步执行一条指令；
* `x/10i $pc`：查看当前 PC 附近的汇编指令；
* `watch *0x地址`：观察内存地址的变化。  通过这些命令，可以清晰地看到从复位向量到内核入口的完整执行路径。

**4. 汇编伪指令与实际指令的对应关系**  实验中看到 `la`、`tail` 等伪指令在反汇编时会被展开成多条实际机器指令（如 `auipc`、`mv`、`j` 等）。  这说明汇编伪指令只是编程时的简化写法，实际执行时依然是底层的基本指令。

**5. 固定的内核加载地址**  实验中内核被放置在物理地址 0x80200000 处，通过跳转进入内核入口 `kern_entry`，完成从启动阶段到内核运行阶段的切换。  这是 RISC-V 平台上一种常见的固定加载布局，方便在早期没有分页机制的情况下进行跳转。

本实验聚焦在从 0x1000 到内核入口的基本启动流程，一些更深入的内容虽然没有直接涉及，但对于理解整个启动过程非常重要：

**1. OpenSBI 的工作内容**  从 0x80000000 到 0x80200000 的这段空间通常由 OpenSBI 占用。  OpenSBI 会进行底层初始化工作，包括：重定位、清空寄存器、清除 bss 段、设置栈、读取设备树、完成硬件信息传递等，为内核提供标准的运行环境。

**2. QEMU 的架构背景**  本实验使用 QEMU 来模拟 RISC-V 硬件。  QEMU 是一个开源的虚拟机和硬件模拟器，能够模拟多种架构。  它分为两层：

* 上层是虚拟机管理器，负责虚拟机的创建、启动、暂停、删除等；
* 下层是执行器，模拟实际的 CPU、内存和外设。  通过这种方式，实验可以在没有真实 RISC-V 硬件的情况下，对内核启动过程进行完整的调试和验证。

### AI 协作开发的经验

