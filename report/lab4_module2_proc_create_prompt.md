# Lab4 功能模块二提示词

````markdown
[PROMPT]
**任务**：修改 `kern/process/proc.c`（如声明需同步则同时修改 `kern/process/proc.h`），完成「进程创建及初始化」功能模块。
本模块负责把系统中「已经能运行内核代码」推进到「已经能创建并登记多个内核线程」。`alloc_proc` 得到一个干净的进程控制块；`proc_init` 把当前执行现场包装成 idleproc 并创建第一个真正的内核线程 initproc；`kernel_thread` 把函数入口和参数整理成线程首次运行所需的临时中断帧；`do_fork` 把这些信息和内核栈、PID、链表、状态位等资源真正组合成一个可运行实体；`copy_thread` 把 trapframe 和 context 安放到新线程自己的内核栈上；`copy_mm` 在当前实验阶段承担地址空间处理接口的占位作用。

**操作要求**：
- 必须在项目中修改实际文件，而不是仅展示代码片段。
- 保留文件中现有代码；只在标记为「lab4」/「YOUR CODE」的占位位置补全。
- 不得修改 `get_pid` / `hash_proc` / `find_proc` / `setup_kstack` / `put_kstack` / `forkret` 等既有辅助函数的行为约定。
- 不得改动 `struct proc_struct`、`struct context`、`struct trapframe` 的字段定义。
- 不得引入与本模块无关的功能。

**输出要求**：
- 直接进行文件操作。
- 以工程真实代码及下列 `[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]` 为准。

[RELY]
enum proc_state { PROC_UNINIT, PROC_SLEEPING, PROC_RUNNABLE, PROC_ZOMBIE };
#define PROC_NAME_LEN 15
#define MAX_PROCESS 4096
#define MAX_PID (MAX_PROCESS * 2)
#define KSTACKSIZE (2 * PGSIZE)
#define CLONE_VM 0x00000100
struct proc_struct { state, pid, runs, kstack, need_resched, parent, mm, context, tf, pgdir, flags, name, list_link, hash_link };
struct context { ra, sp, s0~s11 };
struct trapframe { gpr(struct pushregs), status, epc, badvaddr, cause };
extern struct proc_struct *idleproc, *initproc, *current;
extern char bootstack[], bootstacktop[];
extern uintptr_t boot_pgdir_pa;
kmalloc / kfree
alloc_pages / free_pages / page2kva / kva2page
get_pid / hash_proc / find_proc / set_proc_name / get_proc_name
setup_kstack / put_kstack / wakeup_proc / forkret / kernel_thread_entry / forkrets
list_init / list_add / list_next / le2proc
local_intr_save / local_intr_restore
read_csr(sstatus) / SSTATUS_SPP / SSTATUS_SPIE / SSTATUS_SIE
E_NO_MEM / E_NO_FREE_PROC

[GUARANTEE]
必须实现：
```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

[SPECIFICATION]
## alloc_proc
**Pre-Condition**: 无。
**Post-Condition**: 返回一块状态干净、字段已初始化的 PCB：state=PROC_UNINIT、pid=-1、runs=0、kstack=0、need_resched=0、parent=NULL、mm=NULL、context 清零、tf=NULL、pgdir=boot_pgdir_pa、flags=0、name 清零；内存不足返回 NULL。

## proc_init
**Pre-Condition**: pmm/vmm 已初始化；proc_list 与 hash_list 未初始化。
**Post-Condition**: proc_list 与 hash_list 初始化完毕；idleproc（pid=0、RUNNABLE、kstack=bootstack、need_resched=1、parent=NULL、mm=NULL、tf=NULL）与 initproc（pid=1、RUNNABLE、parent=idleproc、mm=NULL、context.ra 与 context.sp 非 0）都已建立，且 current=idleproc。

## kernel_thread
**Pre-Condition**: fn/arg 合法；进程系统已初始化。
**Post-Condition**: 构造临时中断帧（s0=fn、s1=arg、epc=kernel_thread_entry、status 置 SPP/SPIE 并清 SIE），调用 do_fork 返回新线程 pid 或负错误码。

## do_fork
**Pre-Condition**: 进程数未达上限。
**Post-Condition**: 依次完成 alloc_proc → setup_kstack → copy_mm → copy_thread → 挂 proc_list/hash_list（关中断）→ wakeup_proc 七步，返回子进程 pid；任一步失败则回滚已分配资源并返回负错误码。

## copy_thread
**Pre-Condition**: proc->kstack 已分配，tf 有效。
**Post-Condition**: trapframe 放到内核栈顶，a0=0，sp=esp（esp==0 时用栈顶），context.ra=forkret、context.sp=tf。

## copy_mm
**Pre-Condition**: current->mm == NULL。
**Post-Condition**: 本实验不做地址空间复制，返回 0。
````
