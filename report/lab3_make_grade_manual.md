# Lab3：`make grade` 说明与手动操作教程

适用目录：`os-lab/code`（官方 lab3 骨架 + 已实现 Trap 代码）

---

## 一、`make grade` 得到 `Total Score: 100/100` 时系统做了什么

`make grade` **不会改你的源码仓库内容**。它会：

1. `make clean`
2. 把当前工程打包复制到临时目录（如 `/tmp/lab3_grade`）
3. 在临时副本上运行 `tools/inject.py`：把评分测试代码注入进去
4. 在临时副本里执行 `tools/grade.sh`
5. 跑完后删除临时目录

### `inject.py` 注入了什么

把 `tools/test_lab3.c`、`test_lab3.h`、`test_lab3_context.S` 拷到临时工程的 `kern/test/`，并改临时 `Makefile` / `init.c`：

| 插入位置（`kern_init` 内） | 调用的测试 |
|---|---|
| 第二次 `idt_init()` 之后 | `test_lab3_exception_context()` |
| `clock_init()` 之后 | `test_lab3_timer_is_armed()` |
| `intr_enable()` 之后 | `test_lab3_wait_for_timer()` |

### `grade.sh` 的三组测试（满分 100）

每次测试都会：**clean → 带宏重新编译 → 限时跑 QEMU → 在串口输出里搜关键字**。

| 测试 | 编译宏 | 分值 | 限时 | 主要检查内容 | 必须出现的输出子串 |
|---|---|---|---|---|---|
| Test 1 | `GRADE_LAB3_CONTEXT` | 35 | 8s | `ebreak` 后寄存器恢复、`epc` 前进、Trap 入口/返回 | `[Test 1] Trap entry and breakpoint return: PASS`、`Breakpoint at` |
| Test 2 | `GRADE_LAB3_ILLEGAL` | 25 | 8s | 非法指令 `epc` 前进；未知中断/异常兜底打印 | `[Test 2] Illegal instruction return: PASS`、`Illegal instruction at`、`Supervisor software interrupt`、`Unknown interrupt: cause 15`、`cause    0x00000014` |
| Test 3 | `GRADE_LAB3_TIMER` | 40 | 30s | 时钟中断可重复触发，`ticks` 能涨到 200 | `[Test 3] Sustained supervisor timer: PASS`、`100 ticks`、`ticks=200` |

三项都过才会打印：

```text
Total Score: 100/100
```

（脚本退出码为 0。）

### 本机 QEMU 启动方式（重要）

评分脚本里使用：

```text
qemu-system-riscv64 -machine virt -nographic -m 128M -bios default -kernel bin/ucore.img
```

原因简述：

- 仅用 `-device loader,...` 时，OpenSBI 的 `Next Address` 可能为 0，内核不启动
- 直接 `-kernel bin/kernel`（ELF）在本机可能报 DTB 放置失败
- `-kernel bin/ucore.img`（扁平二进制）+ `-m 128M` 可正常启动，且内存上界与 `PHYSICAL_MEMORY_END` 一致

---

## 二、你自己手动怎么操作（详细步骤）

### 0. 环境准备

打开终端，确认工具存在：

```bash
which riscv64-unknown-elf-gcc
which qemu-system-riscv64
which python3
```

含义：交叉编译器、QEMU、Python3（评分限时用）都应能找到。

进入工程代码目录：

```bash
cd "/Users/zhangshuhan/Desktop/配置agent&&OS环境/os-lab/code"
```

---

### 1. 日常编译

```bash
make clean
make
```

| 命令 | 含义 |
|---|---|
| `make clean` | 删除 `obj/`、`bin/` 等中间产物 |
| `make` | 编译生成 `bin/kernel`，再 `objcopy` 得到 `bin/ucore.img` |

理想结果：最后看到类似

```text
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

无报错即编译成功。

---

### 2. 手动跑内核（看时钟中断）

```bash
make qemu
```

或等价命令：

```bash
qemu-system-riscv64 -machine virt -nographic -m 128M -bios default -kernel bin/ucore.img
```

| 参数 | 含义 |
|---|---|
| `-machine virt` | RISC-V virt 虚拟板 |
| `-nographic` | 无图形界面，串口接到当前终端 |
| `-m 128M` | 内存 128MB（与页表/物理内存上界匹配） |
| `-bios default` | 使用 QEMU 内置 OpenSBI |
| `-kernel bin/ucore.img` | 加载内核镜像 |

理想结果（节选）：

```text
DTB Init
...
(THU.CST) os is loading ...
memory management: default_pmm_manager
check_alloc_page() succeeded!
++ setup timer interrupts
100 ticks
100 ticks
...
```

结果含义：

- DTB/物理内存初始化成功
- Trap、时钟中断通路工作
- 每 100 次时钟中断打印一次 `100 ticks`
- 打印满 10 轮后会 `sbi_shutdown` 关机（正常演示路径）

退出 QEMU：若未自动关机，按 `Ctrl-A` 再按 `X`。

---

### 3. 一键评分（推荐）

仍在 `code` 目录：

```bash
make grade
```

含义：按上一节流程，在临时目录注入测试并跑满三组用例。

理想结果：

```text
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
  - illegal instruction EPC advance and return: PASS (25/25)
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

结果含义：官方评分脚本认定模块一（Trap 入口/恢复）与模块二（异常/中断处理、持续时钟）均通过。

耗时大约几十秒（Test3 最多等约 30 秒）。

---

### 4. 若只想“人肉”对照某一项测试（可选）

下面命令用于理解评分在测什么；**日常交作业用 `make grade` 即可**。

#### 4.1 类似 Test1（断点 + 上下文）

需要自己改临时工程较麻烦，因此建议直接看 `make grade` 的 Test1 输出。  
核心人工验证点：`ebreak` 后能打印 `Breakpoint at`，且程序能继续跑（`epc` 已前进）。

#### 4.2 类似 Test3（持续时钟）

手动 `make qemu`，确认出现多次：

```text
100 ticks
```

说明 `clock_set_next_event()` 与 `ticks++` 正确，中断可持续触发。

---

### 5. 常见失败对照

| 现象 | 可能原因 |
|---|---|
| `Total Score: 0/100` 且输出文件几乎只有 OpenSBI | QEMU 未真正进入内核（加载参数不对） |
| `page_init: invalid memory range` | QEMU 内存与 `PHYSICAL_MEMORY_END` 不一致（应用 `-m 128M`） |
| 缺少 `Breakpoint at` / `Illegal instruction at` | 异常处理打印字符串不符合评分检索 |
| 有一次 `100 ticks` 后不再出现 | 未调用 `clock_set_next_event()` |
| 有 `timeout: command not found` | 本机无 GNU `timeout`；当前 `grade.sh` 已用 Python 限时 |

---

## 三、和实验内容的对应关系（方便写报告）

| 评分点 | 对应你实现的代码 |
|---|---|
| Trap 入口、寄存器恢复、断点返回 | `trapentry.S`：`RESTORE_ALL`、`move a0,sp` + `jal trap`；`exception_handler` 的 breakpoint 分支 |
| 非法指令返回 + 兜底打印 | `exception_handler` / `interrupt_handler` default |
| 持续 supervisor timer | `interrupt_handler` 的 `IRQ_S_TIMER`：`clock_set_next_event`、`ticks++`、`100 ticks` |
