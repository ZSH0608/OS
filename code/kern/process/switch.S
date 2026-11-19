//LAB4 function3 2412888
/* ==================== 开始改代码 ====================
 * 功能模块三：上下文切换
 * 依据提示词 A：switch_to 只负责保存/恢复 ra、sp、s0~s11，
 * 不做任何调度策略判断。struct context 在 RV64 下各字段 8 字节，
 * 偏移依次为 ra=0, sp=8, s0=16, ..., s11=104。
 * ==================== */

 /*
 switch_to 做一件事：

把 CPU 当前正在用的一组寄存器，存进旧进程的 context；
再把新进程 context 里保存的值装回寄存器。
*/

    .text
    .globl switch_to
switch_to:
    # void switch_to(struct context *from, struct context *to)
    # a0 = from, a1 = to
    # 1. 保存：把当前 CPU 执行的线程的 ra/sp/s0–s11 写入 from 指向的结构体
    sd ra,  0(a0)
    sd sp,  8(a0)
    sd s0,  16(a0)
    sd s1,  24(a0)
    sd s2,  32(a0)
    sd s3,  40(a0)
    sd s4,  48(a0)
    sd s5,  56(a0)
    sd s6,  64(a0)
    sd s7,  72(a0)
    sd s8,  80(a0)
    sd s9,  88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    # 2. 从 to 指向的结构体恢复目标线程现场；此后 sp 已属于目标线程
    ld ra,  0(a1)
    ld sp,  8(a1)
    ld s0,  16(a1)
    ld s1,  24(a1)
    ld s2,  32(a1)
    ld s3,  40(a1)
    ld s4,  48(a1)
    ld s5,  56(a1)
    ld s6,  64(a1)
    ld s7,  72(a1)
    ld s8,  80(a1)
    ld s9,  88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

    # 3. 按此时的 ra 跳转，实际跳到新进程上次保存的继续位置（新线程第一次则是 forkret）
    ret
/* ==================== 改代码结束 ==================== */
