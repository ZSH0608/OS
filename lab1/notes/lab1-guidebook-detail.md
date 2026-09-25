---
name: lab1-guidebook-detail
description: lab1 指导书全部 12 页逐页内容（最终版）
metadata: 
  node_type: memory
  type: reference
  originSessionId: ef874303-4216-4138-9834-589e56c705fe
  modified: 2026-09-22T11:30:55.368Z
---

来源：`http://8.135.34.58/lab2026/_book/lab1/*.html`（2026-09-22 抓取正文）。lab1 已是最终版，其余 lab 可能更新。

## lab1 定位
"比麻雀更小的麻雀"：搭一个**最小可执行内核**，能在 QEMU 上格式化输出一行信息后死循环，为后续实验打地基。主线：加电→固件→bootloader→内核入口→C 代码→输出。

## 逐页内容
1. **lab1.html（引言）**：麻雀骨架比喻（上百万行 Linux vs 几千行 ucore vs lab1 只留骨架）。
2. **lab1_1_goals（实验目的）**：学 4 件事——用链接脚本描述内存布局；交叉编译生成 elf 再生成内核镜像；用 OpenSBI 当 bootloader + QEMU 模拟；用 OpenSBI 服务格式化打印字符串。
3. **lab1_2_labs（实验内容）**：同上，强调对接 QEMU 启动流程 + OpenSBI 固件。
4. **lab1_2_1_exercise（练习）**：2 个练习（见 [[lab1-task-checklist]]）；含 tips（三阶段启动）和「拓展」现代笔记本 UEFI→GRUB→OS 三级跳类比。
5. **lab1_2_2_file（项目组成和执行流）**：完整项目树 + 核心文件（entry.S / init.c / kernel.ld / function.mk / Makefile）+ 完整启动流程图（加电复位→0x1000 MROM→0x80000000 OpenSBI→加载内核到 0x80200000→entry.S→kern_init()→输出→死循环）。
6. **lab1_3_booting**：**空页/占位**，只有标题无正文（站点仍可能更新的证据）。
7. **lab1_3_1_layout（OpenSBI, bin, elf）**：bootloader 概念、固件/OpenSBI 的 M 态、RISC-V 四级特权级（U/S/保留/M）、复位地址 0x1000、为何内核必须放 0x80200000（地址相关代码）、elf vs bin 区别（bss 大数组例子）、任务=布局正确的 elf→objcopy 转 bin。
8. **lab1_3_2_linkerscript（内存布局，链接脚本，入口点）**：各段（.text/.rodata/.data/.bss/stack/heap）；逐行注释 tools/kernel.ld（`ENTRY(kern_entry)`、`BASE_ADDRESS=0x80200000`、SECTIONS）；给 entry.S 全文（`kern_entry: la sp, bootstacktop; tail kern_init`）。**练习1 的答案来源**。
9. **lab1_3_3_sbi_io（从SBI到stdio）**：从零造 cprintf——OpenSBI 提供"输出一个字符"原始接口；`ecall`（S 态陷入 M 态调 SBI）；内联汇编 `sbi_call()`（功能号入 a7、参数 a0/a1/a2、ecall、返回值 a0）；逐层封装 `sbi_console_putchar`→`cons_putc`→`cputch/cputs`→`cprintf`；顺带 defs.h 类型、riscv.h 的 read_csr/write_csr 宏。
10. **lab1_3_4_makeit（Just make it）**：make/Makefile 基本规则；`make qemu` 实际命令；期望输出（OpenSBI banner + `(THU.CST) os is loading ...`）。
11. **lab1_4_gdb（GDB 调试工具）**：riscv64 gdb 位置；远程调试原理（QEMU 当被调试目标 + GDB 连端口）；tmux 双窗格速查表；`make debug`（-s -S 开 1234）+ `make gdb`（file bin/kernel / set arch riscv:rv64 / target remote localhost:1234）。**练习2 操作方法来源**。
12. **lab1_5_requirement（实验报告要求）**：markdown 报告四项要求（见 [[lab1-task-checklist]]）；在 labcodes/lab1 下作答、组内建 gitee/github 仓库 push 提交。

相关：[[lab1-task-checklist]]、[[guidebook-structure]]
