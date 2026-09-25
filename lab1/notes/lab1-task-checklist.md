---
name: lab1-task-checklist
description: lab1 需要完成的任务清单：2 个练习、报告要求、关键文件、建议阅读顺序
metadata: 
  node_type: memory
  type: project
  originSessionId: ef874303-4216-4138-9834-589e56c705fe
  modified: 2026-09-22T11:31:02.869Z
---

lab1「比麻雀更小的麻雀（最小可执行内核）」要完成的任务（对应 [[lab1-guidebook-detail]]）。

## 1. 主线（要理解的启动流程）
`加电复位(0x1000 MROM) → 跳 0x80000000(OpenSBI, M态) → OpenSBI 加载内核到 0x80200000 → 跳 entry.S(kern_entry) → kern_init() → cprintf 输出 → 死循环`

## 2. 两个练习
- **练习1**：读 `kern/init/entry.S`，说明 `la sp, bootstacktop`（分配内核栈 / 设栈指针）和 `tail kern_init`（跳 C 入口）的作用与目的。
- **练习2**：用 GDB 从加电跟踪到内核第一条指令 0x80200000，回答"RISC-V 加电后最初几条指令在什么地址、做了什么"，报告里记录调试过程/观察/答案。tips 三阶段：0x1000 复位固件→SBI 主初始化加载内核（可用 `watch *0x80200000` 观察加载瞬间）→跳 0x80200000（`b *0x80200000` 验证）。

## 3. 实验报告（markdown，四项）
1. 本章整体逻辑线（按什么顺序、围绕哪些功能逐步实现）；
2. 每个功能的核心函数/模块理解；
3. 本章重要知识点 ↔ OS 原理知识点对应（含义、关系、差异）；
4. OS 原理中很重要但本章未对应上的知识点。
在 `labcodes/lab1` 下完成并写报告，组内建 gitee/github 仓库 git push 提交。

## 4. 关键文件
| 文件 | 作用 |
|---|---|
| `kern/init/entry.S` | 内核入口（练习1 对象） |
| `kern/init/init.c` | `kern_init()` C 入口 |
| `tools/kernel.ld` | 链接脚本：`ENTRY(kern_entry)`、`BASE_ADDRESS=0x80200000` |
| `libs/sbi.c` | `sbi_call` 内联汇编封装 ecall |
| `kern/driver/console.c` / `kern/libs/stdio.c` / `libs/printfmt.c` | 从"输出一个字符"逐层封出 `cprintf` |

## 5. 建议阅读顺序（刚拿到代码的学生）
lab0/2_overview（全局地图）→ lab0/3_startdash（搭环境）→ lab0.5/3_prompt_structure + lab0.5/4_practice_guide（四段式提示词 + 流程）→ lab1.html → lab1_1_goals → lab1_2_1_exercise（带问题）→ lab1_2_2_file（对照代码）→ lab1_3_1_layout → lab1_3_2_linkerscript → lab1_3_3_sbi_io → lab1_3_4_makeit → lab1_4_gdb（做练习2 前）→ lab1_5_requirement（写报告前）。lab1_3_booting 为空页可跳过。

相关：[[lab1-guidebook-detail]]、[[guidebook-structure]]、[[lab-environment]]
