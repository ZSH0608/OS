#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}


// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int do_exit(int error_code)
{
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

//LAB4 function2 2411514
/* ==================== 开始改代码 ====================
 * 功能模块二：进程创建及初始化
 * 依据提示词：alloc_proc 得到干净 PCB；proc_init 包装 idleproc 并创建
 * initproc；kernel_thread 构造临时中断帧；do_fork 完成 7 步创建；
 * copy_thread 布置 trapframe 与首次运行 context；copy_mm 当前为占位。
 * ==================== */

/* 前向声明：do_fork 需在定义之前调用这两个 static 函数 */
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);

// alloc_proc：申请一块 PCB（proc_struct），并把字段设成安全初始值
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));//通过 kmalloc 获得一块内存，放proc_struct
    if (proc != NULL)
    {
        proc->state = PROC_UNINIT;      // state：进程生命周期状态。PROC_UNINIT：已创建但尚未进入调度
        proc->pid = -1;                 // 进程号。-1 表示还没正式分配
        proc->runs = 0;                 // 被调度运行过的次数，初始为 0。
        proc->kstack = 0;               // 该线程内核栈起始地址。0 表示还没分配
        proc->need_resched = 0;         // 是否需要重新调度。0 表示暂时不需要。
        proc->parent = NULL;            // 父进程指针。
        proc->mm = NULL;                // mm：用户地址空间管理结构。内核线程没有独立用户地址空间，为 NULL。
        memset(&(proc->context), 0, sizeof(struct context)); // 上下文清零（进程切换时要保存/恢复的寄存器集合）
        proc->tf = NULL;                // 指向该进程中断帧的指针
        proc->pgdir = boot_pgdir_pa;    // 页表根的物理地址，页目录基址则指向内核已经建立好的页表
        proc->flags = 0;                // 标志位清零
        memset(proc->name, 0, PROC_NAME_LEN + 1); // 进程名清零
    }
    return proc;
}

// 初始化进程管理系统：建链表、创建 idleproc 和 initproc
/*
初始化管理链表，创建并登记 idleproc（0 号，空闲进程）和 initproc（1 号），
并把 current 设为 idleproc。
*/
void
proc_init(void)
{
    int i;
    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++)
    {
        list_init(hash_list + i);
    }

    // 创建第 0 个内核线程 idleproc：直接复用当前执行现场
    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;   // 复用启动阶段准备好的 bootstack
    idleproc->need_resched = 1;                // 尽快让出 CPU 给 initproc
    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc;

    // 创建第 1 个内核线程 initproc
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }
    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

/*
第一个内核线程的创建是通过 kernel_thread 函数来实现的。
这个函数负责为新的内核线程创建一个初始化好的中断帧，并通过调用 do_fork 函数将其转化为一个新的进程，
kernel_thread函数通过调用do_fork函数最终完成了内核线程的创建工作。
（怎么调用 do_fork 函数？将中断帧的指针传递给do_fork函数，do_fork函数会根据中断帧的指针创建一个新的进程。）
*/
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    // 把函数入口地址放进中断帧的 s0，之后 kernel_thread_entry 会用 s0 作为跳转目标（jalr s0）。
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    // 置 SPP/SPIE、清 SIE：sret 后回到 S 态并恢复中断使能状态
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;//sret 后仍留在 S 态（内核态），因为这是内核线程
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

//创建子进程/内核线程的完整流程，do_fork是创建线程的主要函数
/*
1. 分配并初始化进程控制块（alloc_proc函数）
2. 分配并初始化内核栈（setup_kstack函数）
3. 根据clone_flags决定是复制还是共享内存管理系统（copy_mm函数）
4. 设置进程的中断帧和上下文（copy_thread函数）
5. 把设置好的进程加入链表
6. 将新建的进程设为就绪态
7. 将返回值设为线程id
*/
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;//即将创建的子进程 PCB 指针。
    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    // 1. 分配 PCB。（alloc_proc函数）
    if ((proc = alloc_proc()) == NULL)
    {
        goto fork_out;
    }
    proc->parent = current;

    // 2. 给子进程分配内核栈（通常 2 页），起始地址写入 proc->kstack。失败：释放已分配的 PCB（kfree(proc)），再返回。（setup_kstack函数）
    if (setup_kstack(proc) != 0)
    {
        goto bad_fork_cleanup_proc;
    }

    // 3. 处理地址空间。本实验 copy_mm 几乎总是返回 0；若失败，要先回收内核栈，再回收 PCB。根据clone_flags决定是复制还是共享内存管理系统（copy_mm函数）
    if (copy_mm(clone_flags, proc) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }

    // 4. 在子进程内核栈上复制/布置中断帧，并设置 context，让它第一次被调度时能跑起来。
    copy_thread(proc, stack, tf);

    // 5. 把设置好的进程加入链表，挂入 proc_list 与 hash_list（这段必须关中断保证原子性）
    {
        bool intr_flag;
        local_intr_save(intr_flag);//关中断，并把“之前是否开着中断”存进 intr_flag
        {
            proc->pid = get_pid();//分配一个未占用的 pid，写入 proc->pid
            hash_proc(proc);//按 pid 挂到哈希表 hash_list
            list_add(&proc_list, &(proc->list_link));//挂到全局进程链表
            nr_process ++;//进程计数器加 1
        }
        local_intr_restore(intr_flag);//按 intr_flag 恢复中断
    }
    // 6. 将新建的进程设为就绪态，把子进程状态设为 PROC_RUNNABLE（就绪太），可被调度选中。
    wakeup_proc(proc);

    // 7. 返回子进程 pid，将返回值设为线程id
    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

//用于将当前进程的中断帧复制到新进程的内核栈中
/*
我们首先在上面分配的内核栈上分配出一片空间来保存trapframe。然后，我们将trapframe中的a0寄存器（返回值）设置为0，说明这个进程是一个子进程。
之后我们将上下文中的ra设置为了forkret函数的入口，并且把trapframe放在上下文的栈顶。
*/
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    // 把中断帧 trapframe放到新进程内核栈顶端
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // a0=0 ，表示这个进程是一个子进程
    proc->tf->gpr.a0 = 0;
    // 若调用方未指定栈，则使用内核栈顶（即中断帧自身位置）
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    // switch_to 恢复后执行的“返回地址”。设为 forkret，所以新线程第一次被调度时，会先进入 forkret
    proc->context.ra = (uintptr_t)forkret;
    //切换后使用的栈指针，设为中断帧所在地址。之后 forkret → forkrets(current->tf) → __trapret 按这份 trapframe 恢复寄存器并 sret，跳到 kernel_thread_entry，再调用真正的 fn。
    proc->context.sp = (uintptr_t)(proc->tf);
}

// 本应处理地址空间复制/共享；lab4 内核线程阶段只做占位，直接成功返回
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    assert(current->mm == NULL);
    /* 本实验创建的是内核线程，current->mm 为 NULL，无需复制地址空间 */
    return 0;
}
/* ==================== 改代码结束 ==================== */

//LAB4 function3 2412888
/* ==================== 开始改代码 ====================
 * 功能模块三：进程切换与空闲循环
 * proc_run 在关中断临界区内更新 current、按需切换 Sv39 页表并调用
 * switch_to；cpu_idle 持续检查 need_resched，有需求时发起调度。
 * ==================== */
 /*
 在就绪态进程链表找到合适的进程后，调用该 proc_run() 函数：保存当前进程current的进程上下文，恢复新进程的执行现场，完成进程切换。
 让参数指定的进程成为当前运行进程：更新 current，必要时换页表，再调用 switch_to 切换寄存器上下文
 */
void
proc_run(struct proc_struct *proc)
{
    if (proc == current)
    {
        return;
    }

    bool intr_flag;
    struct proc_struct *prev = current;

    local_intr_save(intr_flag);
    {
        /* 1. 把全局 current 改成 proc */
        current = proc;

        /* 2. 若新旧进程页表不同，写入新的 satp 并刷新 TLB

        页表不同才切换；satp.MODE 位于 bit[63:60]，必须使用 Sv39 格式。
         * libs/riscv.h 的 lsatp() 是 Sv32 语义，不能用于本实验。 */
        if (proc->pgdir != prev->pgdir)
        {
            write_csr(satp, ((uint64_t)SATP_MODE_SV39 << 60) |
                                (proc->pgdir >> RISCV_PGSHIFT));
            flush_tlb();
        }
        //3. 调用 switch_to，保存旧进程上下文、恢复新进程上下文
        switch_to(&(prev->context), &(proc->context));
    }
    local_intr_restore(intr_flag);
}

/*
idleproc 的主循环。
内核初始化结束后会进入这里：没有其它事时就空转；一旦需要调度，就调用 schedule()
*/
void
cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)//need_resched = 1，表示需要调度
        {
            schedule();
        }
    }
}
/* ==================== 改代码结束 ==================== */
