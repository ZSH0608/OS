# 完整对话记录：riscv64-ucore lab1 阅读与环境配置

- 日期：2026-09-22
- 项目：riscv64-ucore 操作系统实验（lab1）
- 在线指导书：http://8.135.34.58/lab2026/_book/
- 参与方：用户（macOS Apple Silicon M3）+ Claude Code

本文件记录本会话从"阅读指导书"到"配置环境"再到"整理备忘"的完整过程与结论。

---

## 第一轮：阅读在线实验指导书并做结构化总结

**用户要求：**
> 请仔细阅读在线实验指导书 http://8.135.34.58/lab2026/_book/
> 1. 用 curl 抓取首页目录及主要章节页面（欢迎页、lab0 各节、lab0.5、lab1 全部页面、附录概览），不要只凭猜测。
> 2. 用中文结构化告诉我：这本书是什么、给谁用、整体结构（lab0～lab9 + lab0.5 + 附录各自解决什么问题）。
> 3. lab1 已是最终版本，把 lab1 写细一点，其它 lab 只写定位。
> 4. 给出"我接下来该先读哪几页"的阅读顺序（面向刚拿到 lab1 代码的学生）。
> 不要开始改本地代码，只做阅读与总结。

**Claude 完成的工作与结论：**

实际抓取了首页、lab0 全部 5 节、lab0.5 全部 6 节、lab1 全部 12 页、附录 4 页的正文（`markdown-section` 里的真实内容，非目录标题）。

### 这本书是什么
《riscv64-ucore 操作系统实验指导书》（HonKit 6.2.2 构建）。清华 ucore 教学操作系统从 x86 移植到 64 位 RISC-V 后的配套 step-by-step 教程，代码基于 bbl-ucore 与原版 ucore_os_lab。核心理念"step by step"：每个 lab 假设已完成前一章，从零搭出能跑简单命令行的 OS。

**给谁用**：操作系统课程学生。默认环境 Ubuntu + QEMU + RISC-V 交叉工具链 + OpenSBI。2026 版新增 lab0.5，明确这是"AI 协作编程"课程。

### 整体结构（各 lab 定位）

| 章节 | 标题 | 定位 |
|---|---|---|
| 欢迎页 | 欢迎来到 ucore step-by-step 的世界 | stub，只有仓库说明和工具链链接 |
| lab0 | 预备起 | 环境准备：历史溯源、结构概览、Linux 命令、常用软件、搭建环境 |
| lab0.5 | AI 驱动的操作系统实验 | 范式转变、提示词工程、四段式提示词、AI 协作实践 |
| lab1 | 比麻雀更小的麻雀（最小可执行内核） | 启动流程 + 最小内核（见下） |
| lab2 | 物理内存和页表 | 页管理、分页、页表、物理内存分配器 |
| lab3 | 断，都可以断 | RISC-V 中断、trap、时钟中断、中断开关 |
| lab4 | 进程管理 | 虚拟内存（多级页表）+ 内核线程（PCB、调度） |
| lab5 | 用户程序 | 用户进程、系统调用、首次进入用户态、进程退出 |
| lab6 | 进程调度 | 进程状态、切换、RR、Stride |
| lab7 | 同步互斥 | 信号量、管程 |
| lab8 | 文件系统 | VFS、SFS、设备、open/read、shell |
| lab9 | 页面置换与内存映射 | 缺页、FIFO、mmap |
| 附录 | 附录 | makefile 简介、进程属性、用户进程特征 |

⚠️ **章节错位提醒**：lab0「概览」页里 lab1~lab9 的一句话清单写的是经典 x86 ucore 的问题映射，与实际章节整体错位一格（如它说 lab7=调度、lab9=文件，实际 lab7=同步互斥、lab9=页面置换）。以目录真实标题为准。

### lab1 详细（12 页逐页）
见下方 lab1 小节，或直接看 `lab1-guidebook-detail.md`。

### 建议阅读顺序
1. `lab0/2_overview`（全局地图）
2. `lab0/3_startdash`（搭环境）
3. `lab0.5/3_prompt_structure` + `lab0.5/4_practice_guide`（四段式提示词 + 流程）
4. `lab1.html` → `lab1_1_goals` → `lab1_2_1_exercise`（带问题）
5. `lab1_2_2_file`（对照代码）
6. `lab1_3_1_layout` → `lab1_3_2_linkerscript` → `lab1_3_3_sbi_io` → `lab1_3_4_makeit`
7. `lab1_4_gdb`（做练习2 前）
8. `lab1_5_requirement`（写报告前）

（`lab1_3_booting` 为空页可跳过。）

---

## 第二轮：配置 lab1 实验环境（macOS Apple Silicon M3）

**用户要求：**
> 我是 macOS Apple Silicon (M3)，请直接帮我配置好 lab1 的实验环境。先不做实验，只完成环境配置，然后帮我总结 lab1 实验需要完成的任务。

**Claude 完成的工作与结论：**

### 环境现状（工具链本来就已装好，无需新装）

| 组件 | 版本 |
|---|---|
| Homebrew | 7.0.4 |
| riscv64-unknown-elf-gcc | 16.2.0 |
| binutils（ld/objcopy/objdump） | 2.47 |
| riscv64-unknown-elf-gdb | 17.2 |
| qemu-system-riscv64 | 11.1.1 |
| make | 系统 GNU Make 3.81（够用） |

Makefile 默认 `GCCPREFIX := riscv64-unknown-elf-` 正好匹配，无需覆盖。

### 三项端到端验证全部通过
1. **编译** `make` → 生成 `bin/kernel`（ELF）+ `bin/ucore.img`（raw 二进制）。
2. **运行** `make qemu` → OpenSBI 启动，`Domain0 Next Address: 0x80200000`，内核打印 `(THU.CST) os is loading ...`。
3. **GDB 调试** `make debug` + gdb → 连上 `localhost:1234`，从复位地址 `0x1000` 起步，断点 `0x80200000` 命中 `kern_entry`（`entry.S` 的 `la sp, bootstacktop`）。

### 一个必须说明的兼容性坑 ⚠️
新版 Homebrew QEMU 11.1.1 自带 OpenSBI **v1.8.1（fw_dynamic 型固件）**，指导书对应 2020 年的 OpenSBI v0.6（fw_jump，固定跳 0x80200000）。

旧写法 `-device loader,file=ucore.img,addr=0x80200000` 只把镜像塞进内存、不设置固件跳转地址，结果 `Domain0 Next Address = 0`，内核不跑。正确做法是改用 **`-kernel`**。

**如实说明**：Makefile 的 `qemu`/`debug` target 已经被改成 `-kernel $(kernel)` 并附中文注释——这个改动**不是 Claude 做的**（Claude 最初读到的是旧写法，文件 mtime 在本会话期间变化，而 Claude 的命令只读或只写 obj/bin）。Claude 实测确认当前版本正确可用，故保留未重复修改。

### 常用命令
`make`（编译）、`make qemu`（运行）、`make debug` + `make gdb`（调试）、`make clean`（清理）。

### lab1 需要完成的任务清单

**主线（启动流程）**：
`加电复位(0x1000 MROM) → 跳 0x80000000(OpenSBI) → 加载内核到 0x80200000 → 跳 entry.S(kern_entry) → kern_init() → cprintf 输出 → 死循环`

**两个练习**：
- 练习1：读 `kern/init/entry.S`，说明 `la sp, bootstacktop`（分配内核栈）和 `tail kern_init`（跳 C 入口）的作用与目的。
- 练习2：用 GDB 从加电跟踪到内核第一条指令 0x80200000，回答"加电后最初几条指令在什么地址、做了什么"，报告记录调试过程/观察/答案。

**实验报告（markdown，四项）**：整体逻辑线 / 核心函数理解 / 知识点与 OS 原理对应 / OS 原理中未对应上的知识点。

**关键文件**：`kern/init/entry.S`、`kern/init/init.c`、`tools/kernel.ld`、`libs/sbi.c`、`kern/driver/console.c`、`kern/libs/stdio.c`、`libs/printfmt.c`。

---

## 第三轮：把结论写入项目备忘（Claude 记忆区）

**用户要求：**
> 除了 macOS M3 + Homebrew 工具链 + QEMU/OpenSBI -kernel 兼容性坑，把你刚才的完整结构化总结、lab1 任务清单都写进项目备忘。

**Claude 完成的工作：** 在记忆区写入 4 个记忆文件 + 索引：
- `lab-environment.md`（project）
- `guidebook-structure.md`（reference）
- `lab1-guidebook-detail.md`（reference）
- `lab1-task-checklist.md`（project）
- `MEMORY.md`（索引）

---

## 第四轮：用户找不到文件 → 说明路径

**用户：** 文件在哪里，我没找到。

**Claude 说明：** 文件在隐藏目录
`/Users/zhangshuhan/.claude/projects/-Users-zhangshuhan-Desktop---agent--OS---os-lab-lab1/memory/`
（`~/.claude` 是隐藏目录，Finder 默认不显示）。给了三种打开方式：访达 `Cmd+Shift+G` 跳转、`open ~/.claude/...`、`Cmd+Shift+.` 显示隐藏文件。

---

## 第五轮：复制一份到项目里

**用户：** 需要，请你复制一份过去。

**Claude 完成的工作：** 把 4 个内容文件复制到 `lab1/notes/`，并新增 `notes/README.md` 索引。

---

## 第六轮（本轮）：把完整对话写成 .md

**用户要求：** 请把我们刚才所有的完整对话写成 .md 文档，放在 lab1/notes/ 下。

**Claude 完成的工作：** 生成本文档 `lab1/notes/对话记录.md`。

---

## 附：lab1 指导书 12 页逐页内容速览

1. **lab1.html**：麻雀骨架比喻（上百万行 Linux vs 几千行 ucore vs lab1 只留骨架）。
2. **lab1_1_goals**：学 4 件事——链接脚本描述内存布局；交叉编译生成 elf→内核镜像；OpenSBI 当 bootloader + QEMU；OpenSBI 服务格式化打印。
3. **lab1_2_labs**：同实验目的，强调对接 QEMU 启动流程 + OpenSBI。
4. **lab1_2_1_exercise**：2 个练习 + tips（三阶段启动）+ 现代笔记本 UEFI→GRUB→OS 类比。
5. **lab1_2_2_file**：项目树 + 核心文件 + 完整启动流程图。
6. **lab1_3_booting**：空页/占位。
7. **lab1_3_1_layout**：bootloader、固件 M 态、四级特权级、复位地址 0x1000、为何 0x80200000、elf vs bin。
8. **lab1_3_2_linkerscript**：各段；逐行注释 kernel.ld（ENTRY(kern_entry)、BASE_ADDRESS=0x80200000）；entry.S 全文。**练习1 答案来源**。
9. **lab1_3_3_sbi_io**：从零造 cprintf——OpenSBI 输出字符接口、ecall、sbi_call 内联汇编、逐层封装。
10. **lab1_3_4_makeit**：make/Makefile 规则、`make qemu` 命令、期望输出。
11. **lab1_4_gdb**：riscv64 gdb、远程调试原理、tmux 速查、make debug + make gdb。**练习2 方法来源**。
12. **lab1_5_requirement**：markdown 报告四项要求 + git 提交。

---

## 附：notes/ 目录现有文件清单

| 文件 | 内容 |
|---|---|
| `README.md` | 目录索引 |
| `对话记录.md` | 本文件（完整对话记录） |
| `lab-environment.md` | 环境备忘 |
| `guidebook-structure.md` | 指导书整体结构 |
| `lab1-guidebook-detail.md` | lab1 逐页详解 |
| `lab1-task-checklist.md` | lab1 任务清单 |
