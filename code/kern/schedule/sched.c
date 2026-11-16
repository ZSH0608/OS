#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 YOUR CODE
/* ==================== 开始改代码 ====================
 * 占位：schedule 待模块三提示词实现
 * ==================== */
void
schedule(void)
{
    /* no-op placeholder */
}
/* ==================== 改代码结束 ==================== */


