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
 * 功能模块三：进程调度选择
 * 在关中断临界区内完成「清 need_resched -> 轮转选择 -> 触发切换」。
 * idleproc 不在 proc_list 中，因此以 idleproc 为起点时从链表头扫描；
 * 否则从 current 的后继开始扫描，扫完一圈仍无候选则回退 idleproc。
 * ==================== */
void
schedule(void)
{
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        current->need_resched = 0;

        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do
        {
            le = list_next(le);
            if (le == &proc_list)
            {
                /* 哨兵不是进程，跳过并结束本轮扫描 */
                continue;
            }
            next = le2proc(le, list_link);
            if (next->state == PROC_RUNNABLE)
            {
                break;
            }
            next = NULL;
        } while (le != last);

        if (next == NULL)
        {
            /* 没有可运行线程时退回空闲线程 */
            next = idleproc;
        }

        next->runs++;
        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}
/* ==================== 改代码结束 ==================== */


