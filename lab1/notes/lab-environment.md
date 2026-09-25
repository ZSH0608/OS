---
name: lab-environment
description: 用户 macOS M3 上的 lab1 实验环境：Homebrew riscv 工具链版本、QEMU/OpenSBI 的 -kernel 兼容性坑、构建/运行/调试命令
metadata: 
  node_type: memory
  type: project
  originSessionId: ef874303-4216-4138-9834-589e56c705fe
  modified: 2026-09-22T11:30:32.607Z
---

用户在做 riscv64-ucore 操作系统实验（AI 驱动式课程，见 [[guidebook-structure]]），机器是 **macOS 15.6 / Apple Silicon M3（arm64）**，Darwin 24.6.0。

项目路径：`/Users/zhangshuhan/Desktop/配置agent&&OS环境/os-lab/lab1`

## 工具链（Homebrew 7.0.4 全部已装好，无需再装）
- `riscv64-unknown-elf-gcc` 16.2.0
- binutils 2.47（`riscv64-unknown-elf-ld / objcopy / objdump`）
- `riscv64-unknown-elf-gdb` 17.2
- `qemu-system-riscv64` 11.1.1
- `make`：系统自带 GNU Make 3.81（够用，无需 brew 装 gmake）

Makefile 默认 `GCCPREFIX := riscv64-unknown-elf-` 正好匹配 Homebrew 装的前缀，无需覆盖；`QEMU := qemu-system-riscv64` 也匹配。

## 关键兼容性坑（重要）
新版 Homebrew QEMU 11.1.1 自带的 OpenSBI 是 **v1.8.1（fw_dynamic 型固件）**，而指导书对应 2020 年的 OpenSBI v0.6（fw_jump，固定跳 0x80200000）。

旧写法 `-device loader,file=ucore.img,addr=0x80200000` 只把镜像塞进内存、**不设置固件跳转地址**，结果是 OpenSBI 打印 `Domain0 Next Address : 0x0000000000000000`，内核根本不会执行（无 `(THU.CST) os is loading` 输出）。正确做法是用 **`-kernel`** 加载。

Makefile 的 `qemu`/`debug` 两个 target 现已改为 `-kernel $(kernel)`（附中文注释说明此问题），已实测可用。`$(kernel)` = `bin/kernel`（ELF）；`-kernel bin/ucore.img`（raw 二进制）也验证可用。

## 常用命令
- `make` — 编译（生成 `bin/kernel` + `bin/ucore.img`）
- `make qemu` — 运行（应打印 `(THU.CST) os is loading ...` 后死循环）
- `make debug` + `make gdb` — 远程调试（两个终端或 tmux；QEMU `-s -S` 开 1234 端口，gdb 用 `file bin/kernel` + `target remote localhost:1234`）
- `make clean` — 清理 obj/ bin/

## 验证状态（2026-09-22 已全过）
编译 ✅ / 运行 ✅ / GDB 断点 `0x80200000` 命中 `kern_entry`（entry.S 的 `la sp, bootstacktop`）✅。

**Why:** 这是该用户做 lab1~lab9 的固定环境，后续实验都在此编译运行。
**How to apply:** 后续 lab 遇到 `make qemu` 没输出时，先查 OpenSBI banner 里的 `Domain0 Next Address` 是否为 0x80200000——若为 0 就是固件没拿到跳转地址，检查是否误用了 `-device loader` 而非 `-kernel`。
