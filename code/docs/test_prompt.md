# ucore Lab2 —— Buddy / SLUB 测试设计提示词

> 用途：把本文件整段交给 AI 或开发者，作为测试套件的设计与实现说明。
> 前提：Buddy 与 SLUB 算法已经实现；本任务只写测试，不改框架评分测试。
> 算法实现要求见同目录的 `algorithm_prompt.md`。

---

## 0. 角色与背景

你在 `lab2_new` 仓库中为已经实现的 Buddy 页分配器和 SLUB 对象分配器编写**独立测试**。

- 工具链/模拟器：`riscv64-unknown-elf-*`、`qemu-system-riscv64`。
- 不允许修改：
  - `basic_check`、`default_check`、`best_fit_check`；
  - `tools/` 下的评分脚本与测试；
  - `best_fit` / `default` 模块。
- 默认构建必须保持 `make grade` 20/20。
- C99、`-nostdinc`、`-Werror`；不使用 libc。
- 内核启动栈只有 8 KiB：大数组放 `.bss`（文件级 `static`），不要放栈上。
- 该代码库没有定义 `true/false`（`bool` 是 `int`），测试文件需要自行定义宏。

---

## 1. Buddy 测试套件

### 1.1 文件与接入

1. 新增 `kern/mm/buddy_test.c`、`kern/mm/buddy_test.h`，提供 `void buddy_test(void)`。
2. 在 `kern/mm/pmm.c` 的 `pmm_init()` 末尾用 `#if PMM_USE_BUDDY` 包裹调用：
   ```c
   #if PMM_USE_BUDDY
       buddy_test();
   #endif
   ```
   不要影响默认 best_fit 路径，也不要打乱 `make grade` 在函数末尾的注入位置。
3. 如测试需要查看各级空闲链表，可在 `buddy_pmm.h/c` 增加**只读**检查接口
   （例如 `buddy_test_order_count(order)`、`buddy_test_order_block(order,index)`、
   `buddy_test_page_state(page)`），不得改变分配路径语义。

### 1.2 输出格式

统一：

```
[Buddy] <name>: PASS
[Buddy] <name>: FAIL
```

失败时先打印缩进的 `[Buddy]   ...` 细节（页号、order、期望值、实际值、计数），再打印结论行。
最后输出 `[Buddy] result: ALL PASS` 或 `FAILURES`。

### 1.3 必须覆盖的测试

1. **初始结构自检**
   - 各阶链表按物理地址有序；
   - 每个空闲块页数为 2 的幂、首页 `property` 正确、起始页号按块大小对齐；
   - 不同阶/同阶的块不重叠；
   - 所有空闲块页数之和 == `nr_free_pages()`。
2. **基本分配与释放**
   - 从最小可满足阶分配；返回页的 `page2pa` 合法、`PageProperty` 已清除、页号在受管范围内；
   - 连续分配多个块，地址不重叠；
   - 释放后重新分配，检查块被复用（同一地址）或空闲页数正确恢复。
3. **分裂行为**
   - 只有大块可用时请求小块，验证大块被正确分裂，剩余部分挂到低阶；
   - 精确检查分裂前后**每阶块数**：`order ∈ [target, src)` 各 +1，`src` 阶 −1；
   - 释放后各阶计数与总页数恢复。
4. **合并行为**
   - 分配一个 32 页块，先释放前 16 页，断言其 buddy（后 16 页）仍占用时**不合并**；
   - 再释放后 16 页，断言合并为 ≥32 页的块；低阶 16 页块消失；
   - 检查各阶计数与总页数恢复；
   - 反例：buddy 在受管区外、为 reserved 页、或仍是已分配页时，不得合并。
5. **边界情况**
   - `n = 0` 返回 NULL；
   - `n > 2^BUDDY_MAX_ORDER` 返回 NULL；
   - 耗尽空闲链表后 `alloc_page()` 返回 NULL；
   - 请求 3、5、6 等非 2 的幂：按向上取整的阶分配，`nr_free` 按 `2^order` 记账，
     释放后恢复。
6. **统计一致性**
   - 每次操作后检查“总空闲页数 == 各阶块页数之和”；
   - 测试结束释放全部对象，页数与每阶块数回到基线快照。
7. **有限压力测试**
   - 固定伪随机种子，几十到几百轮 alloc/free；
   - 记录存活对象的页号与大小，检查互不重叠；
   - 释放后检查计数守恒；
   - 如果实现支持，可写入/读回 pattern 验证内存可访问。

### 1.4 关键经验（务必遵守）

- 合并规则测试中，**buddy 状态必须在分配之后采样**。`alloc_pages(1)` 可能从大块分裂，
  在 `p+1` 新产生一个 order-0 空闲块；用分配前的快照推导期望会误判为“错误合并”。
- 重复释放/非法释放在本实现中走 `panic`；测试不要主动触发，改为验证“释放后页状态非 ALLOC”
  或说明该路径未执行。
- 结构自检失败时应给出具体 order/ppn/property，不要只打印 FAIL。

### 1.5 验收

```bash
make clean
make DEFS=-DPMM_USE_BUDDY=1 test     # 或 qemu
```

预期：

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

---

## 2. SLUB 测试套件

### 2.1 文件与接入

1. 新增 `kern/mm/slub_test.c`、`kern/mm/slub_test.h`，提供 `void slub_test(void)`。
2. 在 `kern/init/init.c` 中定义编译开关，默认关闭：
   ```c
   #ifndef SLUB_TEST
   #define SLUB_TEST 0
   #endif
   ```
3. 在 `pmm_init()` 之后的 `slub_init()` 之后调用：
   ```c
   #if SLUB_TEST
       slub_test();
   #endif
   ```
4. 默认构建（`SLUB_TEST=0`）不得产生任何行为变化，`make grade` 仍为 20/20。

### 2.2 输出格式

```
[SLUB] <name>: PASS
[SLUB] <name>: FAIL
```

失败时打印对象地址、请求尺寸、可用尺寸、页数统计与期望值/实际值。
测试开始时保存 `nr_free_pages()` 基线与 `struct slub_stats` 基线，结束时逐项比较。

### 2.3 必须覆盖的测试

1. **各尺寸类基本功能**
   - 对 8、16、32、…、2048 每类分配若干对象；
   - 返回地址非空、按约定对齐（8B 类 8 对齐，其余 16 对齐）、互不重叠；
   - 写满整个可用对象并读回；
   - 释放后重新分配，检查复用或至少不泄漏页。
2. **尺寸边界**
   - `size = 0, 1, 7, 8, 9, …` 以及各尺寸类边界前后一个字节；
   - `2048, 2049, 4096, 4097, 8192`；
   - 验证 `kmalloc(0)==NULL`、超大尺寸返回 NULL；
   - 验证大对象路径（可用尺寸 ≥ 请求、页数正确）。
3. **跨 slab 行为**
   - 同一尺寸类分配超过单 slab 容量（如 64B 类每页 63 个，分配 200 个）；
   - 断言 slab 数为 4、内容互不干扰、地址不重叠；
   - 全部释放后页数回到基线（本实现空 slab 立即归还，无保留配额）。
4. **大对象**
   - 4095、4096、4097、8192、16384；
   - 在对象首、尾和跨页边界（4090~4101）写入并读回；
   - 释放后 `nr_free_pages()` 恢复。
5. **混合与压力**
   - 固定伪随机种子，几十到几百轮；
   - 维护存活对象数组（指针、请求大小、可用大小、pattern 种子）；
   - 随机分配/释放；分配时检查地址不与存活对象重叠，释放前校验内容；
   - 结束时释放全部对象，已分配对象数回到基线，页数回到基线。
6. **异常路径**
   - `kfree(NULL)` 不崩溃、统计不变；
   - 超大尺寸返回 NULL；
   - 仅测试实现校验范围内的非法指针（例如 RAM 窗口外的指针，实现会打印并返回）；
   - 重复释放若实现为 `panic`，**不要实际触发**，在输出中说明该路径未执行。
7. **统计与不变量**
   - 每次分配/释放后检查（测试可用静默检查，失败才打印）：
     ```
     free_objs + (allocated_objs - large_objs) == slab_capacity
     used_pages == slabs + large_pages
     ```
   - slab 数、页数与实际分配一致；
   - 最终 `allocated_objs/free_objs/slabs/used_pages/large_objs/large_pages`
     全部回到基线。

### 2.4 依赖的实现约定（必须在报告中说明）

- `kmalloc(0)` 返回 NULL；`kmalloc` 不清零。
- 空 slab **立即归还**，因此页数必须精确恢复；若实现改为保留空 slab，要给出保留上限
  并相应放宽断言。
- 大对象上限（本实现为 1 MiB − 头部）与超限返回 NULL。
- 8B 类 8 字节对齐、其余 16 字节对齐。
- 重复释放/非法对象指针走 `panic`；RAM 窗口外指针走“打印并返回”。
- 测试写入“可用大小”而不是仅请求大小，才能覆盖整对象。

### 2.5 验收

```bash
make clean
make DEFS="-DSLUB_TEST=1" test
```

预期：

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

同时确认默认构建：

```bash
make clean && make grade        # 仍为 20/20
```

---

## 3. 汇报要求

测试交付报告必须包含：

1. 新增/修改文件清单；
2. **每条测试针对的典型 bug**：
   - 元数据定位错误（页对齐/偏移）；
   - 对齐错误（buddy XOR / 对象步长）；
   - 空闲链表破坏（重复释放、错误合并）；
   - 跨 slab 计数错误；
   - 大对象释放错误（页数/order）；
   - 页泄漏（空 slab 未回收）；
   - `nr_free` / 统计漂移。
3. 测试为什么有效（断言点与不变量）；
4. 依赖的实现约定（size=0、空 slab 策略、上限、对齐、panic 路径）；
5. 运行命令与实际输出；
6. 失败时的定位方法（按断言逐条对照，不要改测试绕过）。
