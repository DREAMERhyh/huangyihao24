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

/* 任务5：所有任务共用同一个时间片。"让落后飞机多飞"靠调度时的挑选顺序实现
 *   （见 pick_next），不靠改时间片大小——题目要求的"任何时间片不得为 0"
 *   于是天然成立。取 1/4 个时钟节拍（~3.75ms）：5 架飞机轮流时，每架每次
 *   上场只前进两三格，编队看着更连续（15ms 节拍下每批要跳 9 格）。 */
#define SLICE_LEN   (TIMER_INTERVAL / 4)

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

/* 任务5：fly 程序每前进一格就报一次"当前位置"（workload = 60 - 列号，前进则变小）。
 *   内核不猜 CYCLE_PER_MOVE 是多少，只数一件事：这架飞机到目前一共飞了多少格。 */
void do_set_sche_workload(int workload)
{
    pcb_t *task = current_running;

    if (task->has_wl && task->wl_pos != workload) {
        int dpos = task->wl_pos - workload;     /* >0：前进；<0：一轮飞完从头开始 */

        /* 回卷这一支要**补记 1 格**：上一轮最后一格（i=59）是在它自己的上报
         *   之后才飞的，下一次上报已经是新一轮起点，那一格不会被 dpos 统计到。
         *   不补的话内核每轮少算 1 格，短轮任务被少算的比例更大（fly1 每轮 10 格
         *   → 少算 10%），"谁快谁慢"的排序就会被系统性带偏。 */
        if (dpos != 0)
            task->wl_cols += (dpos > 0) ? dpos : 1;
    }

    task->wl_pos = workload;
    task->has_wl = 1;
}

/* 任务5：从就绪队列里挑"下一架飞"的飞机。
 *   规则：队首是飞控任务时，在**所有飞控任务**里挑累计前进格数最少的那一架
 *   （"谁飞得少谁先飞"）；队首是普通任务时照旧拿队首——普通任务按轮转拿
 *   自己那份，不会被飞控任务饿死。
 *   为什么不估"每格要烧多少 CPU"再分时间片：**进度本身就是最直接的反馈**。
 *   落后的一架下一轮就被优先选中，直到追上；不需要知道 CYCLE_PER_MOVE，
 *   也不怕宿主抢 CPU、时钟抖动这类噪声——它们只会把某架飞机压慢一点，
 *   而那架飞机随后会被自动补回来（实测把累计进度差压在几个格子以内）。 */
static pcb_t *pick_next(void)
{
    pcb_t *head = list_entry(ready_queue.next, pcb_t, list);

    if (!head->has_wl)
        return head;

    pcb_t *best = head;
    for (list_node_t *n = head->list.next; n != &ready_queue; n = n->next) {
        pcb_t *t = list_entry(n, pcb_t, list);
        if (t->has_wl && t->wl_cols < best->wl_cols)
            best = t;
    }
    return best;
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

    /* ★ pid0（内核空转）不参与轮转：它只是"没别的任务可跑"时的兜底。
     *   让它跟着轮转有两个害处：
     *   ① 白占一个时间片，所有用户进程都少拿 CPU（三实体时每进程只能拿 1/3）；
     *   ② 它跑的是 main 里的 wfi，而 wfi 是 guest **唯一主动把宿主 CPU 让出去**
     *      的地方——一睡一醒会把宿主调度器的相位与 guest 的轮转对齐，于是
     *      "guest 的 rdtime 是宿主墙钟、不是执行量"这个事实，就从随机误差变成
     *      按轮转槽位稳定偏向的系统性误差，实测能让两个**完全相同**的任务
     *      产出差 20%（本机只有 1 核、load 常 3~9，被放大）。
     *   所以下面只在就绪队列空的时候把它拎出来怠速。
     *   （用 `-icount shift=3,sleep=off` 把时间基准换成指令数后，偏差为 0.006%，
     *   证明调度本身是公平的，问题出在"墙钟时间基准 + 宿主超载"。） */
    if (prev->status == TASK_RUNNING && prev != &pid0_pcb) {
        prev->status = TASK_READY;
        list_add_tail(&prev->list, &ready_queue);
    } else if (prev == &pid0_pcb) {
        /* 怠速任务平时就标成"不可运行"。严格说这是借用了 BLOCKED 的语义
         * （它不是被谁阻塞，只是没活干）——今天没有副作用，但将来若加
         * ps/kill/wait 这类"按 status 扫全部 PCB"的功能，要记得把 pid0 排除。 */
        prev->status = TASK_BLOCKED;
    }

    pcb_t *next;
    if (!list_empty(&ready_queue)) {
        // TODO: [p2-task1] Modify the current_running pointer.
        next = pick_next();            /* 任务5：飞得最少的先飞；普通任务照旧拿队首 */
        list_del(&next->list);
    } else if (prev != &pid0_pcb) {
        next = &pid0_pcb;              /* 兜底：所有任务都阻塞了，让内核上场怠速 */
    } else {
        next = prev;                   /* 已经在怠速且没别的事，继续 */
    }

    next->status = TASK_RUNNING;
    current_running = next;

    /* 时间片统一为 1/4 个时钟节拍——绝不为 0，飞机不会"停下"。 */
    bios_set_timer(get_ticks() + SLICE_LEN);

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
