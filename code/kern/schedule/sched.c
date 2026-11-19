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

//LAB4 function3 2412888
/* ==================== 开始改代码 ====================
 * 功能模块三：进程调度选择
 * 在关中断临界区内完成「清 need_resched -> 轮转选择 -> 触发切换」。
 * idleproc 不在 proc_list 中，因此以 idleproc 为起点时从链表头扫描；
 * 否则从 current 的后继开始扫描，扫完一圈仍无候选则回退 idleproc。
 * ==================== */

/*
schedule 是调度器：决定下一个该运行哪个进程，并在需要时触发切换。

它在关中断下依次做三件事：
1. 清掉当前进程的 need_resched（表示这次调度请求已处理）
2. 在进程链表里按轮转方式找下一个状态为 PROC_RUNNABLE 的进程；找不到就用 idleproc
3. 若选中的不是当前进程，调用 proc_run 去做真正的切换
*/
void
schedule(void)
{
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        //第 1 步：清除调度请求标志清掉当前进程的 need_resched（表示这次调度请求已处理）
        current->need_resched = 0;

        //第 2 步：确定从哪里开始扫:若当前进程不是 idleproc：从当前进程的下一个位置开始查找。若当前进程是 idleproc：从链表头开始查找
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

        //第 4 步：找不到就绪进程：遍历整个链表都没找到，则最后使用 idleproc 保底。
        if (next == NULL)
        {
            /* 没有可运行线程时退回空闲线程 */
            next = idleproc;
        }

        //第 5 步：更新运行次数并切换
        next->runs++;
        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}
/* ==================== 改代码结束 ==================== */


