# ucore Lab2 操作记录

> 本文件按实际执行顺序记录每一步的目标、修改文件、实现要点、验证结果与过程修正。
> 仓库根目录：`d:\OS\lab2_new`。
> 配套文档：`algorithm_prompt.md`（实现提示词）、`test_prompt.md`（测试提示词）。

---

## 0. 总览

| 阶段 | 任务 | 主要产物 |
|---|---|---|
| 1 | 环境勘察 | WSL 工具链/QEMU 可用性、QEMU 版本差异结论 |
| 2 | `page_init()` | `kern/mm/pmm.c` |
| 3 | Best-Fit | `kern/mm/best_fit_pmm.c`、`kern/mm/pmm.c` |
| 4 | Buddy System | `kern/mm/buddy_pmm.c/.h`、`kern/mm/pmm.c` |
| 5 | Buddy 测试 | `kern/mm/buddy_test.c/.h`、`buddy_pmm.c/.h` 只读接口、`pmm.c` 调用 |
| 6 | SLUB | `kern/mm/slub.c/.h`、`kern/init/init.c` |
| 7 | SLUB 测试 | `kern/mm/slub_test.c/.h`、`slub.c/.h` 统计扩展、`init.c` 开关 |
| 8 | 文档交付 | `docs/algorithm_prompt.md`、`docs/test_prompt.md`、`docs/operation_log.md` |

最终默认配置：`best_fit_pmm_manager`；`make grade` 20/20。
可选配置：`PMM_USE_BUDDY=1`、`SLUB_TEST=1`。

---

## 1. 环境勘察

### 1.1 工具链

- Windows 主机 PATH 中没有 `riscv64-unknown-elf-gcc` 与 `qemu-system-riscv64`。
- WSL 中可用：
  - 交叉工具链：`/mnt/d/OS/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin`
  - `qemu-system-riscv64`（10.2）、`python3`、`bc`、`gmake`。

构建命令模板：

```bash
export PATH=/mnt/d/OS/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin:$PATH
cd /mnt/d/OS/lab2_new
make
```

### 1.2 仓库事实

- 源文件为 CRLF 行尾；GCC 可正常编译。
- 骨架状态：
  - `pmm.c` 调用 `page_init()` 但未定义；
  - `best_fit_pmm.c` 只有 `basic_check/best_fit_check`；
  - `init_pmm_manager()` 指向 `default_pmm_manager`；
  - `KADDR` 在 `pmm.h` 中被注释。

### 1.3 QEMU 10.2 与课程旧版的差异

本机验证时发现两个环境问题，**只影响本地运行，不影响仓库源码**：

1. `-device loader,file=...,addr=0x80200000` 在 QEMU 10.2 下不设置启动地址，
   OpenSBI 打印 `Domain0 Next Address: 0x0000000000000000`，内核不执行。
   本地改用 `-kernel bin/ucore.img`，或用一个包装脚本把 `-device loader` 翻译成 `-kernel`。
2. `entry.S` 在 `csrw satp` 后仍以物理地址取指；QEMU 10.2 立即按新页表翻译，
   触发 `exec_page_fault`（epc=0x80200000）。旧版 QEMU 会继续执行当前 TB。
   本地在 `/tmp` 副本中给启动页表增加一个 1 GiB 恒等映射（VPN2=2）绕过。

因此所有 QEMU 运行验证都在 `/tmp` 临时副本中进行：
临时修改 `entry.S` + 使用 `-kernel`；**评测逻辑、测试代码与仓库源码未做这些修改**。
`/tmp` 在 WSL 每次调用间会被清空，相关脚本都在单次调用内完成。

---

## 2. 阶段一：实现 `page_init()`

### 2.1 目标

补上 `kern/mm/pmm.c` 中缺失的 `static void page_init(void)`，满足 `test_page_init()` 的 8 项检查。

### 2.2 修改文件

- `kern/mm/pmm.c`：新增 `page_init()`，位置在 `//lab2 function1 YOUR CODE` 标记处。
- 其他文件未改。

### 2.3 实现要点

1. `va_pa_offset = PHYSICAL_MEMORY_OFFSET` 必须先执行。
2. 从 DTB 取 `mem_base/mem_size`，校验非 0、`mem_end > mem_base`、`mem_end <= PHYSICAL_MEMORY_END`。
3. `npage = PPN(mem_end)`；`npage <= nbase` 时 panic。
4. `pages = ROUNDUP(end, PGSIZE)`；校验描述符数组不越过 `mem_end`。
5. 先把 `npage-nbase` 个描述符全部置 reserved（`flags=0` 后 `SetPageReserved`）、`ref=0`、`property=0`。
6. `freemem = ROUNDUP(PADDR(pages) + sizeof(struct Page)*(npage-nbase), PGSIZE)`。
7. `freemem >= mem_end` 或 `PPN(freemem) >= npage` 时 panic，不调用 `init_memmap`。
8. 否则 `init_memmap(pa2page(freemem), (mem_end-freemem)/PGSIZE)`。
9. `cprintf` 输出 memory base/size/end、npage、pages VA/PA、freemem 与页数。

### 2.4 验证与过程记录

- 单独编译 `pmm.c` 可通过。
- 整仓 `make` 当时被 `best_fit_pmm.c` 的骨架阻塞：
  - `basic_check/best_fit_check` 引用 `free_list/nr_free`，但 function2-2 尚未实现；
  - 评分注入的 `test_best_fit()` 引用 `best_fit_pmm_manager`。
  这是骨架的既有缺口，不是 `page_init` 的问题。
- 为验证 `page_init`，曾在 `/tmp` 副本中加最小脚手架（`free_area` 三行 + 空管理器占位），
  未写入仓库。
- 过程修正：任务早期曾提前把管理器切到 `best_fit`，按用户要求已还原；
  管理器切换最终在下一阶段按任务完成。
- `[Test 1] Page Init: PASS` 在阶段二完成后随完整构建一并确认。

---

## 3. 阶段二：实现 Best-Fit 并切换管理器

### 3.1 目标

在 `kern/mm/best_fit_pmm.c` 从零实现完整 Best-Fit，并在 `init_pmm_manager()` 中切换。

### 3.2 修改文件

- `kern/mm/best_fit_pmm.c`：新增 `free_area`、`free_list/nr_free` 宏、6 个回调、管理器结构。
- `kern/mm/pmm.c`：`pmm_manager = &best_fit_pmm_manager;`（`#include <best_fit_pmm.h>` 原本已存在）。

### 3.3 实现要点

- 空闲链表按 `struct Page*` 地址升序；块首页 `property=n` + `PG_property`。
- 分配时遍历整个链表，在 `property >= n` 中取最小者；同大小取先遇到者（严格 `<`）。
- 分裂：删除整块，尾部 `page+n` 作为新块插回原位置；`nr_free -= n`。
- 释放：逐页检查 `!PageReserved && !PageProperty`，有序插入，
  先与前块、再与后块合并；被吞并块清除 `PG_property`。

### 3.4 验证结果

```bash
make clean && make DEFS=...
```

- 默认构建通过、零警告。
- 用 QEMU 适配副本运行 `make test`：
  ```
  grading: 1 / 6 points
  ...
  grading: 6 / 6 points
  check_alloc_page() succeeded!
  [Test 1] Page Init: PASS
  [Test 2] Best-Fit: PASS
  ```
- 干净副本运行官方 `make grade`：`Total Score: 20/20`。

---

## 4. 阶段三：实现 Buddy System

### 4.1 目标

新增基于伙伴系统的页分配器，用 `PMM_USE_BUDDY` 编译开关接入，默认仍为 Best-Fit。

### 4.2 修改/新增文件

- 新增 `kern/mm/buddy_pmm.c`、`kern/mm/buddy_pmm.h`。
- `kern/mm/pmm.c`：
  - `#include <buddy_pmm.h>`；
  - `#ifndef PMM_USE_BUDDY / #define PMM_USE_BUDDY 0`；
  - `init_pmm_manager()` 中 `#if PMM_USE_BUDDY` 选择 `buddy_pmm_manager`。

### 4.3 实现要点

- `BUDDY_MAX_ORDER=15`；每阶一个 `free_area_t`，`nr_free` 记块数；全局 `buddy_nr_free` 记页数。
- 元数据：`static uint8_t buddy_state[32768]`（32 KiB BSS）：
  `0`=已分配/未管理，`1..16`=某阶空闲块首页，`0xFF`=空闲块内部页。
- 初始区间按**绝对 PPN** 贪心拆成最大对齐 2^k 块。
- 分配：`ceil(log2(n))`，向上找首个非空阶，逐级分裂，后半插入低阶；
  `nr_free` 按 `2^order` 扣减。
- 释放：校验对齐与元数据，标记首页，`idx ^ (1<<order)` 找 buddy，
  满足“范围内、非 reserved、同级空闲首页、property 匹配”才合并。
- `buddy_check()` 做结构自检。

### 4.4 验证结果

```bash
make clean && make                          # EXIT=0，无警告
make clean && make DEFS=-DPMM_USE_BUDDY=1   # EXIT=0，无警告
```

QEMU 适配副本启动 buddy 版本：

```
memory management: buddy_pmm_manager
check_alloc_page() succeeded!          <- buddy_check 通过
[Test 1] Page Init: PASS
[Test 2] Best-Fit: FAIL (pmm_manager=buddy_pmm_manager)   <- 预期
```

另在临时副本中做过一轮 1/2/5 页分配、释放、二次自检，`buddy functional test: PASS`；
该功能性代码未写入仓库（阶段四由正式测试套件替代）。

---

## 5. 阶段四：Buddy 测试套件

### 5.1 目标

新增独立、可 grep 的 Buddy 测试，覆盖分裂、合并、边界、耗尽与压力。

### 5.2 修改/新增文件

- 新增 `kern/mm/buddy_test.c`、`kern/mm/buddy_test.h`。
- `kern/mm/buddy_pmm.h/c`：增加只读检查接口
  `buddy_test_order_count`、`buddy_test_order_block`、`buddy_test_page_state`；
  `BUDDY_MAX_ORDER` 移到头文件。
- `kern/mm/pmm.c`：`pmm_init()` 末尾 `#if PMM_USE_BUDDY` 调用 `buddy_test()`。

### 5.3 测试内容

结构自检、基本分配/释放、分裂计数、合并/忙 buddy 不合并、边界合并规则、
非法尺寸与非 2 的幂、重复释放防护、耗尽空闲链表、固定种子 100 轮压力、最终状态恢复。

输出格式：`[Buddy] <name>: PASS/FAIL`。

### 5.4 过程修正（重要）

首轮运行 `merge rule / boundaries: FAIL`：

- 单页分配可能从大块分裂，并在 `p+1` 新生成 order-0 空闲 buddy；
- 测试原先使用**分配前**的快照推导期望，误判为“意外合并”。

修正：buddy 状态改为**分配后**采样。这不是分配器 bug。

### 5.5 验证结果

```
[Buddy] initial structure: PASS
[Buddy] basic alloc/free: PASS
[Buddy] split: PASS
[Buddy] merge / busy buddy: PASS
[Buddy] merge rule / boundaries: PASS
[Buddy] invalid and non-power-of-two sizes: PASS
[Buddy] double-free guard: PASS
[Buddy] exhaust free list: PASS
[Buddy] stress (fixed seed, 100 rounds): PASS
[Buddy] final state restored: PASS
[Buddy] result: ALL PASS
```

默认配置回归：`Total Score: 20/20`。

---

## 6. 阶段五：实现简化 SLUB

### 6.1 目标

在页分配器之上实现 `kmalloc/kfree`，支持固定尺寸类与大对象。

### 6.2 修改/新增文件

- 新增 `kern/mm/slub.c`、`kern/mm/slub.h`。
- `kern/init/init.c`：包含 `slub.h`；`pmm_init()` 后调用 `slub_init()`。

### 6.3 实现要点

- 尺寸类：8、16、32、64、128、256、512、1024、2048；不含 4096，>2048 走大对象路径。
- 每个 slab 一页，页首 `struct slab`（48 字节），首个对象偏移 48。
- 空闲对象前 8 字节存 next；每个 cache 维护 `partial` / `full` 链表。
- `kfree` 用 `ptr & ~(PGSIZE-1)` 定位页首头；大对象 payload 也从首页偏移 48 开始。
- 大对象：`2^ceil(log2((size+48)/PGSIZE))` 页，最大 order 8（payload ≤ 1 MiB−48）。
- 空 slab 立即 `free_pages`；统计 allocated/free/slabs/used_pages/large。
- 地址转换：`page_to_kva(page) = page2pa(page) + va_pa_offset`（等价 KADDR）。
- 非法指针窗口外打印并返回；magic 错误/重复释放 `panic`。

### 6.4 验证结果

- 默认与 buddy 配置编译均 EXIT=0、无警告。
- 临时副本冒烟测试（未写入仓库）：
  600 个 8B 对象跨 2 个 slab、写满/读回、交错释放后页数恢复；
  1/200/3000/4096 字节分配与对齐；`kmalloc(0)` 与超限返回 NULL；大对象释放归还页。
  输出 `[Slub] smoke test: PASS`。
- 默认 `make grade`：20/20。

---

## 7. 阶段六：SLUB 测试套件

### 7.1 目标

新增独立测试，覆盖尺寸边界、跨 slab、大对象、混合压力、异常与统计不变量。

### 7.2 修改/新增文件

- 新增 `kern/mm/slub_test.c`、`kern/mm/slub_test.h`。
- `kern/mm/slub.h/c`：
  - `struct slub_stats` 增加 `slab_capacity`、`large_pages`；
  - 新增 `slub_alloc_size(size_t)`（返回可用字节，失败返回 0）；
  - `slub_show_stats()` 输出新字段。
- `kern/init/init.c`：
  - `#ifndef SLUB_TEST / #define SLUB_TEST 0`；
  - `slub_init()` 后 `#if SLUB_TEST` 调用 `slub_test()`。

### 7.3 测试内容

- 各尺寸类：写满可用对象、对齐、不重叠、释放后页数回基线。
- 尺寸边界：1…8192、`kmalloc(0)`、超大尺寸。
- 跨 slab：64B 类 200 个对象 → 断言 4 个 slab。
- 大对象：4095/4096/4097/8192/16384，页边界逐字节校验。
- 压力：固定种子 200 轮，存活对象数组、分配重叠检查、释放前 pattern 校验。
- 异常：`kfree(NULL)`、RAM 窗口外指针；重复释放为 panic 路径，不实际触发。
- 统计不变量（每次操作静默检查）：
  ```
  free_objs + (allocated_objs - large_objs) == slab_capacity
  used_pages == slabs + large_pages
  ```

### 7.4 验证结果

```
[SLUB] size classes: PASS
[SLUB] size boundaries: PASS
[SLUB] cross slab: PASS
[SLUB] large objects: PASS
[SLUB] NULL/invalid pointer handling: PASS
[SLUB] mixed stress (fixed seed, 200 rounds): PASS
[SLUB] final state restored: PASS
[SLUB] result: ALL PASS
```

默认配置回归：`Total Score: 20/20`。

---

## 8. 最终文件清单

### 8.1 新增

| 文件 | 说明 |
|---|---|
| `kern/mm/buddy_pmm.c` / `.h` | Buddy 页分配器 + 只读检查接口 |
| `kern/mm/buddy_test.c` / `.h` | Buddy 测试套件 |
| `kern/mm/slub.c` / `.h` | SLUB 对象分配器 |
| `kern/mm/slub_test.c` / `.h` | SLUB 测试套件 |
| `docs/algorithm_prompt.md` | 算法实现提示词 |
| `docs/test_prompt.md` | 测试设计提示词 |
| `docs/operation_log.md` | 本操作记录 |

### 8.2 修改

| 文件 | 改动 |
|---|---|
| `kern/mm/pmm.c` | `page_init()`；`#include <buddy_pmm.h>/<buddy_test.h>`；`PMM_USE_BUDDY` 开关与管理器选择；`pmm_init()` 末尾条件调用 `buddy_test()` |
| `kern/mm/best_fit_pmm.c` | 在 function2-2 标记处新增完整 Best-Fit 与管理器；`basic_check/best_fit_check` 未改 |
| `kern/init/init.c` | `#include <slub.h>/<slub_test.h>`；`pmm_init()` 后 `slub_init()`；`SLUB_TEST` 开关与条件调用 `slub_test()` |

### 8.3 未修改

- `tools/` 全部文件；
- `kern/mm/default_pmm.c`；
- `basic_check` / `default_check` / `best_fit_check`；
- `kern/mm/pmm.h` 公共接口、`kern/mm/memlayout.h` 的 `struct Page`；
- `Makefile`。

---

## 9. 验证矩阵

| 验证项 | 命令 | 结果 |
|---|---|---|
| 默认编译 | `make clean && make` | 通过，零警告 |
| Buddy 编译 | `make DEFS=-DPMM_USE_BUDDY=1` | 通过，零警告 |
| SLUB 测试编译 | `make DEFS="-DSLUB_TEST=1"` | 通过，零警告 |
| Best-Fit 自检 | `make test`（QEMU 适配副本） | `grading: 6/6`、`check_alloc_page() succeeded!` |
| Buddy 自检 | buddy 模式启动 | `check_alloc_page() succeeded!` |
| Buddy 测试 | buddy 模式启动 | `[Buddy] result: ALL PASS` |
| SLUB 测试 | `SLUB_TEST=1` 启动 | `[SLUB] result: ALL PASS` |
| 官方评分 | `make grade`（干净副本） | `Total Score: 20/20` |

---

## 10. 已知状态与注意事项

### 10.1 工作区中的评分注入产物

当前工作区存在一套**就地注入**的测试产物：

- `kern/test/test_lab2.c`、`kern/test/test_lab2.h`；
- `Makefile` 中两行 `KINCLUDE/KSRCDIR += kern/test`；
- `kern/mm/pmm.c` 中的 `#include <test_lab2.h>` 与 `pmm_init()` 末尾的
  `test_page_init(); test_best_fit();`。

这些不是本记录的代码改动产生的（我们的注入运行都在 `/tmp` 副本中）。
官方 `tools/inject.py` 检测到 `Makefile` 已含 `kern/test` 会拒绝再次注入，
因此在该工作区直接 `make grade` 会失败。上面的 20/20 是在清理后的等价副本上验证的。

如需恢复干净骨架（建议先备份/对比）：

```bash
rm -rf kern/test
# 删除 Makefile 中包含 "kern/test" 的两行
# 删除 pmm.c 中的 test_lab2.h 包含与两个测试调用
```

### 10.2 本地 QEMU 适配

- 只在 `/tmp` 副本中：
  - `entry.S` 启动页表增加 VPN2=2 的 1 GiB 恒等映射；
  - 用 `-kernel bin/ucore.img` 或包装脚本替代 `-device loader`。
- 仓库中的 `entry.S`、Makefile、评分脚本均未改。

### 10.3 配置切换

`make` 不跟踪 `DEFS` 变化。切换 `PMM_USE_BUDDY` / `SLUB_TEST` 之前必须：

```bash
make clean
```

否则可能复用旧编译选项下的目标文件。

---

## 11. 命令速查

```bash
# 进入 WSL 并设置工具链
export PATH=/mnt/d/OS/riscv/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin:$PATH
cd /mnt/d/OS/lab2_new

# 默认（Best-Fit）
make clean && make
make grade

# Buddy
make clean && make DEFS=-DPMM_USE_BUDDY=1
make clean && make DEFS=-DPMM_USE_BUDDY=1 test

# SLUB 测试
make clean && make DEFS="-DSLUB_TEST=1"
make clean && make DEFS="-DSLUB_TEST=1" test
```

预期关键输出：

```
grading: 6 / 6 points
check_alloc_page() succeeded!
[Test 1] Page Init: PASS
[Test 2] Best-Fit: PASS
[Buddy] result: ALL PASS
[SLUB] result: ALL PASS
Total Score: 20/20
```
