#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    # a0: hartid
    # a1: dtb physical address
    # save hartid and dtb address
    la t0, boot_hartid
    sd a0, 0(t0)
    la t0, boot_dtb
    sd a1, 0(t0)

    # t0 := 三级页表的虚拟地址
    lui     t0, %hi(boot_page_table_sv39)
    # t1 := 0xffffffff40000000 即虚实映射偏移量
    li      t1, 0xffffffffc0000000 - 0x80000000
    # t0 减去虚实映射偏移量 0xffffffff40000000，变为三级页表的物理地址
    sub     t0, t0, t1
    # t0 >>= 12，变为三级页表的物理页号
    srli    t0, t0, 12

    # t1 := 8 << 60，设置 satp 的 MODE 字段为 Sv39
    li      t1, 8 << 60
    # 将刚才计算出的预设三级页表物理页号附加到 satp 中
    or      t0, t0, t1
    # 将算出的 t0(即新的MODE|页表基址物理页号) 覆盖到 satp 中
    csrw    satp, t0
    # 使用 sfence.vma 指令刷新 TLB
    sfence.vma
    # ==================== 开始改代码 ====================
    # 开启 Sv39 后，PC 仍是物理地址 0x80200xxx，而页表只映射
    # VA 0xffffffffc0000000+；必须立刻跳到对应高位虚拟地址再继续执行，
    # 否则下一条指令取指会缺页并反复落入入口（本机 QEMU 已复现）。
    lui     t0, %hi(.Lvirt_entry)
    addi    t0, t0, %lo(.Lvirt_entry)
    jr      t0
.Lvirt_entry:
    # ==================== 改代码结束 ====================
    # 从此，我们给内核搭建出了一个完美的虚拟内存空间！
    
    # 我们在虚拟内存空间中：随意将 sp 设置为虚拟地址！
    # 1. 使用临时寄存器 t1 计算栈顶的精确地址
    lui t1, %hi(bootstacktop)
    addi t1, t1, %lo(bootstacktop)
    # 2. 将精确地址一次性地、安全地传给 sp
    mv sp, t1
    # 现在栈指针已经完美设置，可以安全地调用任何C函数了
    # 然后跳转到 kern_init (不再返回)
    lui t0, %hi(kern_init)
    addi t0, t0, %lo(kern_init)
    jr t0

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:

    # a page table for Sv39
    .align PGSHIFT
    .global boot_page_table_sv39
# 分配 4KiB 内存给预设的三级页表
boot_page_table_sv39:
    # ==================== 开始改代码 ====================
    # Sv39 一级页表（每项映射 1GiB）：
    # - VPN2=2：VA 0x80000000 恒等映射到 PA 0x80000000
    #   这样 csrw satp 之后、跳到高半址之前，仍可用物理 PC 取指；
    # - VPN2=511：VA 0xffffffffc0000000 映射到 PA 0x80000000（内核高半址）
    .zero 8 * 2
    .quad (0x80000 << 10) | 0xcf # VRWXAD, identity 0x80000000
    .zero 8 * (511 - 3)
    .quad (0x80000 << 10) | 0xcf # VRWXAD, high half
    # ==================== 改代码结束 ====================

    .global boot_hartid
boot_hartid:
    .quad 0
    .global boot_dtb
boot_dtb:
    .quad 0
