#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>
#include <sbi.h>

#define TICK_NUM 100
// 输出 TICK_NUM 轮统计后关机（讲义规定的关机次数）
#define SHUTDOWN_AFTER 10
volatile size_t num = 0;

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

/* ==================== 开始改代码 ====================
 * 功能模块二：中断与异常处理
 * lab3 function2 2412888
 * ================================================== */

void interrupt_handler(struct trapframe *tf) {
    //最高位是中断/异常标志，右移 1 位，只留下原因编号，才能和 IRQ_S_TIMER 等宏比较。
    intptr_t cause = (tf->cause << 1) >> 1;

    //一些常见中断宏：
    switch (cause) {
        case IRQ_U_SOFT://用户态（U 模式）软件中断
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_SOFT://管理员（S 模式）软件中断
            cprintf("Supervisor software interrupt\n");
            break;
        case IRQ_H_SOFT:    //虚拟机管理员（H 模式）软件中断
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_SOFT:    //机器（M 模式）软件中断
            cprintf("Machine software interrupt\n");
            break;
        case IRQ_U_TIMER:    //用户态时钟中断
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_TIMER:    //管理员（S 模式）时钟中断
            // "All bits besides SSIP and USIP in the sip register are
            // read-only." -- privileged spec1.9.1, 4.1.4, p59
            // In fact, Call sbi_set_timer will clear STIP, or you can clear it
            // directly.
            // cprintf("Supervisor timer interrupt\n");

             /* LAB3 / lab3 function2 2412888 : */

            /*(1)设置下次时钟中断- clock_set_next_event()
             *(2)计数器（ticks）加一
             *(3)当计数器加到100的时候，我们会输出一个`100ticks`表示我们触发了100次时钟中断，同时打印次数（num）加一
            * (4)判断打印次数，当打印次数为10时，调用<sbi.h>中的关机函数关机
            */

            // (1) 设置下次时钟中断，否则之后再也收不到时钟中断
            clock_set_next_event();

            // (2) 全局时钟中断计数器加一（clock.h 中的 ticks，clock_init() 已清零）
            ticks++;

            // (3) 每 100 次时钟中断输出一次统计，并累加已输出轮数
            if (ticks % TICK_NUM == 0) {
                num++;
                print_ticks();//输出 100 ticks
            }

            // (4) 输出够 SHUTDOWN_AFTER 轮后关机（sbi_shutdown 不返回）
            if (num >= SHUTDOWN_AFTER) {
                sbi_shutdown();// 通过 OpenSBI 关机；
            }

            break;
        case IRQ_H_TIMER:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_TIMER:
            cprintf("Machine software interrupt\n");
            break;
        case IRQ_U_EXT:
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_EXT:
            cprintf("Supervisor external interrupt\n");
            break;
        case IRQ_H_EXT:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_EXT:
            cprintf("Machine software interrupt\n");
            break;
        default:
            // 评分脚本会检索该固定前缀；同时保留完整 Trap frame 便于调试
            cprintf("Unknown interrupt: cause %d\n", (int)cause);
            print_trapframe(tf);
            break;
    }
}

/*
 * insn_len - 读取 pc 处指令的字节长度（2/4/6/8）。
 * 返回 0 表示无法可靠判断，调用者不得修改 tf->epc。
 */
static int insn_len(uintptr_t pc) {
    uint16_t insn = *(volatile uint16_t *)pc;
    if ((insn & 0x3) != 0x3) {
        return 2; /* 16 位压缩指令 */
    } else if ((insn & 0x1f) != 0x1f) {
        return 4; /* 32 位标准指令 */
    } else if ((insn & 0x7f) == 0x3f) {
        return 6; /* 48 位指令（本实验不会出现，仅作说明） */
    } else if ((insn & 0x7f) == 0x7f) {
        return 8; /* 64 位指令（本实验不会出现，仅作说明） */
    }
    return 0;
}

void exception_handler(struct trapframe *tf) {
    int len;

    switch (tf->cause) {
        case CAUSE_ILLEGAL_INSTRUCTION:// 非法指令异常0x2
            cprintf("Exception Type: Illegal instruction\n");//打印类型
            // 评分期望子串：Illegal instruction at
            cprintf("Illegal instruction at %p\n", tf->epc);//打印出错指令地址tf->epc
            len = insn_len(tf->epc);//该地址处指令占几个字节
            if (len == 0) {
                // 无法可靠判断指令长度，不盲目修改 epc
                print_trapframe(tf);
            } else {
                tf->epc += len;//让返回后从下一条指令继续，而不是再次执行同一条非法指令
            }
            break;

        case CAUSE_BREAKPOINT:// 断点异常0x3,逻辑一模一样
            cprintf("Exception Type: breakpoint\n");//打印类型
            // 评分期望子串：Breakpoint at
            cprintf("Breakpoint at %p\n", tf->epc);//打印出错指令地址tf->epc
            len = insn_len(tf->epc);//该地址处指令占几个字节
            if (len == 0) {
                // 无法可靠判断指令长度，不盲目修改 epc
                print_trapframe(tf);
            } else {
                tf->epc += len;
            }
            break;
        case CAUSE_MISALIGNED_FETCH:
            print_trapframe(tf);
            break;
        case CAUSE_FAULT_FETCH:
            print_trapframe(tf);
            break;
        case CAUSE_MISALIGNED_LOAD:
            print_trapframe(tf);
            break;
        case CAUSE_FAULT_LOAD:
            print_trapframe(tf);
            break;
        case CAUSE_MISALIGNED_STORE:
            print_trapframe(tf);
            break;
        case CAUSE_FAULT_STORE:
            print_trapframe(tf);
            break;
        case CAUSE_USER_ECALL:
            print_trapframe(tf);
            break;
        case CAUSE_SUPERVISOR_ECALL:
            print_trapframe(tf);
            break;
        case CAUSE_HYPERVISOR_ECALL:
            print_trapframe(tf);
            break;
        case CAUSE_MACHINE_ECALL:
            print_trapframe(tf);
            break;
        default:
            print_trapframe(tf);
            break;
    }
}
/* ==================== 改代码结束 ==================== */

static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
