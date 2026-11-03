#include <riscv.h>

    .macro SAVE_ALL

    csrw sscratch, sp

    addi sp, sp, -36 * REGBYTES
    # save x registers
    STORE x0, 0*REGBYTES(sp)
    STORE x1, 1*REGBYTES(sp)
    STORE x3, 3*REGBYTES(sp)
    STORE x4, 4*REGBYTES(sp)
    STORE x5, 5*REGBYTES(sp)
    STORE x6, 6*REGBYTES(sp)
    STORE x7, 7*REGBYTES(sp)
    STORE x8, 8*REGBYTES(sp)
    STORE x9, 9*REGBYTES(sp)
    STORE x10, 10*REGBYTES(sp)
    STORE x11, 11*REGBYTES(sp)
    STORE x12, 12*REGBYTES(sp)
    STORE x13, 13*REGBYTES(sp)
    STORE x14, 14*REGBYTES(sp)
    STORE x15, 15*REGBYTES(sp)
    STORE x16, 16*REGBYTES(sp)
    STORE x17, 17*REGBYTES(sp)
    STORE x18, 18*REGBYTES(sp)
    STORE x19, 19*REGBYTES(sp)
    STORE x20, 20*REGBYTES(sp)
    STORE x21, 21*REGBYTES(sp)
    STORE x22, 22*REGBYTES(sp)
    STORE x23, 23*REGBYTES(sp)
    STORE x24, 24*REGBYTES(sp)
    STORE x25, 25*REGBYTES(sp)
    STORE x26, 26*REGBYTES(sp)
    STORE x27, 27*REGBYTES(sp)
    STORE x28, 28*REGBYTES(sp)
    STORE x29, 29*REGBYTES(sp)
    STORE x30, 30*REGBYTES(sp)
    STORE x31, 31*REGBYTES(sp)

    # get sr, epc, badvaddr, cause
    # Set sscratch register to 0, so that if a recursive exception
    # occurs, the exception vector knows it came from the kernel
    csrrw s0, sscratch, x0
    csrr s1, sstatus
    csrr s2, sepc
    /* ==================== 开始改代码 ====================
     * 工具链兼容：新版 GNU as 不再识别旧名 sbadaddr；
     * stval 与 sbadaddr 是同一 CSR（地址 0x143），语义不变，仅换用汇编器可识别的名字。
     * ==================== 改代码结束 ==================== */
    csrr s3, stval
    csrr s4, scause

    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

    /* ==================== 开始改代码 ====================
     * 功能模块一：Trap 上下文恢复（RESTORE_ALL）
     * 依据提示词 [SPECIFICATION]::RESTORE_ALL：
     * 1) 先写回 sstatus/sepc，保证后续 sret 使用 trap() 可能更新后的最终值；
     * 2) 再恢复除 x2(sp) 外的通用寄存器；
     * 3) 最后才从 trapframe.gpr.sp（偏移 2*REGBYTES）恢复 sp——
     *    与 SAVE_ALL「把分配帧前的原始 sp 存入 sscratch 再写入该槽」严格对应，
     *    不能把 x2 当成与其它通用寄存器完全同质的槽位处理。
     * lab3 function1 2413157
     * ================================================== */
    .macro RESTORE_ALL
    #lab3 function1 2413157
    # 从 trapframe 取出可能被 trap() 修改过的 status（偏移 32）/epc（偏移 33）
    LOAD s1, 32*REGBYTES(sp)
    LOAD s2, 33*REGBYTES(sp)
    # 为什么先读到通用寄存器，再写 CSR？因为 RISC-V 不能直接 内存 → CSR，必须经过通用寄存器。

    # 把 s1 写入 CSR sstatus，把 s2 写入 CSR sepc。
    csrw sstatus, s1
    csrw sepc, s2

    # 按与 SAVE_ALL 相同的槽位布局恢复通用寄存器（先不恢复 x2）
    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    LOAD x4, 4*REGBYTES(sp)
    LOAD x5, 5*REGBYTES(sp)
    LOAD x6, 6*REGBYTES(sp)
    LOAD x7, 7*REGBYTES(sp)
    LOAD x8, 8*REGBYTES(sp)
    LOAD x9, 9*REGBYTES(sp)
    LOAD x10, 10*REGBYTES(sp)
    LOAD x11, 11*REGBYTES(sp)
    LOAD x12, 12*REGBYTES(sp)
    LOAD x13, 13*REGBYTES(sp)
    LOAD x14, 14*REGBYTES(sp)
    LOAD x15, 15*REGBYTES(sp)
    LOAD x16, 16*REGBYTES(sp)
    LOAD x17, 17*REGBYTES(sp)
    LOAD x18, 18*REGBYTES(sp)
    LOAD x19, 19*REGBYTES(sp)
    LOAD x20, 20*REGBYTES(sp)
    LOAD x21, 21*REGBYTES(sp)
    LOAD x22, 22*REGBYTES(sp)
    LOAD x23, 23*REGBYTES(sp)
    LOAD x24, 24*REGBYTES(sp)
    LOAD x25, 25*REGBYTES(sp)
    LOAD x26, 26*REGBYTES(sp)
    LOAD x27, 27*REGBYTES(sp)
    LOAD x28, 28*REGBYTES(sp)
    LOAD x29, 29*REGBYTES(sp)
    LOAD x30, 30*REGBYTES(sp)
    LOAD x31, 31*REGBYTES(sp)

    # 最后恢复 sp：释放 trapframe，回到 SAVE_ALL 之前的栈指针
    /*
    必须放最后：
        sp 变回 SAVE_ALL 里 addi sp, sp, -36*REGBYTES 之前的值；
        原先那块 Trap frame 所在栈空间不再被 sp 指向，相当于释放；
        之后执行 sret 时，被打断程序看到的栈指针与 Trap 发生前一致。
    */
    LOAD x2, 2*REGBYTES(sp)
    .endm
    /* ==================== 改代码结束 ==================== */

    .globl __alltraps
    .align(2)
__alltraps:
    SAVE_ALL

    /* ==================== 开始改代码 ====================
     * 功能模块一：Trap 入口与 C 处理函数衔接（__alltraps）
     * 依据提示词 [SPECIFICATION]::__alltraps：
     * SAVE_ALL 之后当前 sp 即指向完整 trapframe；
     * 按 RISC-V 调用约定把该指针放入 a0，再调用 trap(struct trapframe *tf)；
     * trap 返回后继续执行 __trapret，完成 RESTORE_ALL + sret。
     * lab3 function1 2413157
     * ================================================== */
    #lab3 function1 2413157
    # sp should be the same as before "jal trap"
    move  a0, sp          # a0 = &trapframe，作为 trap() 的第一个参数
    jal   trap            # 转入 C 语言 Trap 分发/处理；返回后继续执行 __trapret
    /* ==================== 改代码结束 ==================== */

    .globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    sret
