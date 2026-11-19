.text
.globl kernel_thread_entry
kernel_thread_entry:        # void kernel_thread(void)
# s0寄存器里放的是新进程要执行的函数，s1寄存器里放的是传给函数的参数。
	move a0, s1   # 把参数放在了a0寄存器
	jalr s0       # 跳转到s0执行我们指定的函数

	jal do_exit
