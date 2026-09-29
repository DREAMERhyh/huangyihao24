#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/irq.h>
#include <os/time.h>
#include <os/mm.h>
#include <os/kernel.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>

/* ---- 任务5：时间片取值范围 ----
 * 平均值取一个时钟节拍；下限保证任何进程（哪怕快到不需要时间片）也拿得到
 * 实打实的 CPU——题目明确要求"视觉上不能有飞机停下"。 */
#define SLICE_BASE  TIMER_INTERVAL

/* 内核怠速（pid0）的时间片：短一点，免得"有任务被唤醒却要等它睡完" */
#define IDLE_SLICE  (TIMER_INTERVAL / 8)
#define SLICE_MIN   (TIMER_INTERVAL / 8)
#define SLICE_MAX   (TIMER_INTERVAL * 4)

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack,
    .time_slice = IDLE_SLICE
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

/* 把"到当下为止"的 CPU 占用折算出来：正在跑的进程 cpu_used 还没结算到此刻 */
static uint64_t cpu_now(pcb_t *task)
{
    if (task == current_running)
        return task->cpu_used + (get_ticks() - task->last_tick);
    return task->cpu_used;
}

/* 任务5：按"每前进一格要烧多少 CPU"重新分配时间片。
 *   设成本为 c_i，时间片 s_i ∝ c_i，则各进程单位时间前进的格数相等——
 *   这正是"飞行速度相同"的含义。平均时间片固定为 SLICE_BASE，
 *   所以 s_i = SLICE_BASE * c_i / (所有成本的均值)。 */
static void update_time_slices(void)
{
    uint64_t sum = 0;
    int n = 0;

    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].cost > 0) {
            sum += (uint64_t)pcb[i].cost;
            n++;
        }
    }
    if (n == 0)
        return;

    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        uint64_t s;

        if (pcb[i].cost <= 0)
            continue;

        s = SLICE_BASE * (uint64_t)pcb[i].cost / (sum / n);

        if (s < SLICE_MIN)
            s = SLICE_MIN;      // 时间片绝不为 0：宁可多给，也不让飞机停
        if (s > SLICE_MAX)
            s = SLICE_MAX;

        pcb[i].time_slice = s;
    }

}

/* 任务5：fly 程序每前进一格就报一次当前位置。
 *   内核不知道 CYCLE_PER_MOVE，但两次上报之间的"格数差"与"CPU 差"一比，
 *   就能反推出这架飞机前进一格平均要花多少 CPU——起点位置不同也不影响，
 *   因为用的是差分。 */
void do_set_sche_workload(int workload)
{
    pcb_t *task = current_running;
    uint64_t cpu = cpu_now(task);

    if (task->has_wl && task->wl_pos != workload) {
        int dpos = task->wl_pos - workload;     /* workload = 60 - 列号：前进则变小 */

        if (dpos > 0) {
            task->wl_cols += dpos;

            /* 用"自起飞以来的累计平均"：每格的开销差异极大（一格只改几个
             * 屏幕格子，而每轮收尾要清三行、动上百个格子），单格成本能从
             * 6k 跳到 290k。累计平均正好等于"每格真正摊销的成本"，而且同一
             * 轮里的收尾开销会平摊到该轮所有格子上——这正是我们要的。 */
            task->cost = (int)((cpu - task->wl_start_cpu) /
                               (uint64_t)task->wl_cols);
            update_time_slices();
        }
    }

    task->wl_pos = workload;
    if (!task->has_wl) {
        task->wl_start_cpu = cpu;
        task->wl_cols = 0;
    }
    task->has_wl = 1;
}

void do_scheduler(void)
{
    // TODO: [p2-task3] Check sleep queue to wake up PCBs
    // 每次调度前先看看有没有睡够的进程，把它们放回就绪队列
    check_sleeping();

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    /* 任务1：时间片轮转（RR）。
     *   被换下场的任务如果还活着（RUNNING），就排到就绪队列尾部再等下一轮；
     *   已经被阻塞/退出的任务由 do_block 等自己摘掉，这里不再放回。 */
    pcb_t *prev = current_running;

    /* 任务5：把 prev 这一轮占掉的 CPU 结算进它的账上 */
    prev->cpu_used += get_ticks() - prev->last_tick;

    /* ★ pid0（内核空转）不参与轮转：它只是"没别的任务可跑"时的兜底。
     *   让它跟着轮转有两个害处：
     *   ① 白占一个时间片，所有用户进程都少拿 CPU；
     *   ② 它跑的是 main 里的 wfi，会让 QEMU 在宿主机上真的睡下去——宿主的
     *      调度延迟会让 guest 的 rdtime 白白流走，而 rdtime 正是我们抢占用的
     *      时钟；于是"排在 wfi 之后"的任务时间片里混进无谓的流逝，实测能
     *      让两个完全相同的任务产出差 20%（排在后面的稳定吃亏）。
     *   所以下面只在就绪队列空的时候把它拎出来怠速。 */
    if (prev->status == TASK_RUNNING && prev != &pid0_pcb) {
        prev->status = TASK_READY;
        list_add_tail(&prev->list, &ready_queue);
    } else if (prev == &pid0_pcb) {
        prev->status = TASK_BLOCKED;   /* 怠速任务平时就是"不可运行" */
    }

    pcb_t *next;
    if (!list_empty(&ready_queue)) {
        // TODO: [p2-task1] Modify the current_running pointer.
        next = list_entry(ready_queue.next, pcb_t, list);
        list_del(&next->list);
    } else if (prev != &pid0_pcb) {
        next = &pid0_pcb;              /* 兜底：所有任务都阻塞了，让内核上场怠速 */
    } else {
        next = prev;                   /* 已经在怠速且没别的事，继续 */
    }

    next->status = TASK_RUNNING;
    current_running = next;
    next->last_tick = get_ticks();

    /* 任务5：下一次时钟中断就定在"下一个时间片用完"的时刻，
     *   于是时间片大小直接就是被抢占的间隔（而不是被固定节拍量化）。 */
    bios_set_timer(get_ticks() + next->time_slice);

    // TODO: [p2-task1] switch_to current_running
    switch_to(prev, next);
}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    /* get_timer() 已经是"秒"（= ticks / timebase），所以直接加上睡眠秒数即可。
     * 到点与否由 check_sleeping() 在每次 do_scheduler 开头比较。 */
    current_running->wakeup_time = get_timer() + sleep_time;

    // do_block 内部会置 BLOCKED、挂到睡眠队列并让出 CPU（第 1、3 步）
    do_block(&current_running->list, &sleep_queue);
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    pcb_t *task = list_entry(pcb_node, pcb_t, list);

    // 先置状态、再入队、最后让出，三步之间即使被时钟中断打断也自洽：
    // 状态已是 BLOCKED（do_scheduler 不会把本任务再排回就绪队列），
    // 结点已经在阻塞队列上（唤醒者找得到本任务）。
    task->status = TASK_BLOCKED;
    list_add_tail(pcb_node, queue);

    do_scheduler();
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    pcb_t *task = list_entry(pcb_node, pcb_t, list);

    list_del(pcb_node);
    task->status = TASK_READY;
    list_add_tail(pcb_node, &ready_queue);
}
