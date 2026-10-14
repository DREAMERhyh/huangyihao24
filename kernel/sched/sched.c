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
/* P3 任务3：每核一份 idle PCB。主核 [0] 用 INIT_KERNEL_STACK 页当"内核空转栈"；
 *   从核 [1] 的两张固定栈见 smp.h（其形式初值由 smp_init 填）。 */
pcb_t pid0_pcb[NR_CPUS] = {
    {
        .pid = 0,
        .kernel_sp = (ptr_t)pid0_stack,
        .user_sp = (ptr_t)pid0_stack,
        .cpu_mask = CPU_MASK_ALL,   /* 任务4：idle 不受亲和性过滤（它不走 pick_next） */
    },
};
/* 每核"正在运行"的 PCB（do_scheduler 在大锁内维护，见 sched.h 注释） */
pcb_t *core_running[NR_CPUS];

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
        task->wl_cols += (dpos > 0) ? dpos : 1;   /* 外层已保证 dpos != 0 */
        task->wl_run = 0;                         /* 前进过 → 插队配额刷新 */
    }

    task->wl_pos = workload;
    task->has_wl = 1;
}

/* 任务5：从就绪队列里挑"下一架飞"的飞机。
 *   规则：队首是飞控任务时，在**所有飞控任务**里挑累计前进格数最少的那一架
 *   （"谁飞得少谁先飞"）；队首是普通任务时照旧拿队首——普通任务按轮转拿
 *   自己那份，不会被飞控任务饿死。
 *   ★ 插队配额（wl_run < WL_RUN_MAX 才有资格凭落后插队）：
 *     被选中一次配额 +1，**前进一格就清零**；配额用尽就退回普通轮转。
 *     两个作用：
 *     ① 防"进度卡死"霸占 CPU——若某架的上报位置永远不变（助教把 START_POINT
 *        改成 59：每轮上报值恒为 1），它永远是最小值；没有配额就会永远被选中，
 *        把其余所有任务饿死（实测：未加护栏时其余四架 2 秒后全冻住）。
 *        加配额后它最多连占 4 片就被降级为普通轮转，其余任务照常飞。
 *     ② 让"每格要烧好几片"的慢机也能拿到足额补偿——只要每格需要的时间片
 *        ≤ 配额，它每次都在完成一格后立刻重获资格，速率照样被拉平。
 *   为什么不估"每格要烧多少 CPU"再分时间片：**进度本身就是最直接的反馈**。
 *   落后的一架会被优先选中直到追上（前进即刷新配额）；不需要知道
 *   CYCLE_PER_MOVE，也不怕宿主抢 CPU、时钟抖动这类噪声——它们只会把某架
 *   压慢一点，而那架随后会被自动补回来（实测累计进度差 ≤5 格）。
 *   配额的补偿上限 ≈ WL_RUN_MAX − 1 个时间片/格（还要再扣上报本身的开销），
 *   即"每格耗时不超过大约 8 个时间片（≈30ms）"的慢机都能被拉平。 */
#define WL_RUN_MAX 8

/* P3 任务4：任务是否允许在本核（cpu）上运行。
 *   亲和掩码的约定：bit(cpu) == 1 才允许。掩码为 0 的进程谁都跑不了，
 *   永远留在就绪队列里（壳里不允许构造出这种状态：taskset 传入的
 *   掩码由 shell 检查非 0；exec 继承来的至少有一个核）。 */
static int cpu_allows(pcb_t *t, int cpu)
{
    return (t->cpu_mask >> cpu) & 1UL;
}

/* 从全局就绪队列里挑下一个跑的任务（队首 RR + 任务5 的 pick-min）。
 *   返回 NULL：队列为空、或队列里的任务掩码都不允许本核——由调用者
 *   让本核 idle 怠速。 */
static pcb_t *pick_next(void)
{
    int cpu = get_current_cpu_id();
    pcb_t *best = 0;

    /* 取"第一个掩码允许本核"的任务当候选（等价于原来"选队首"的 RR 语义） */
    for (list_node_t *n = ready_queue.next; n != &ready_queue; n = n->next) {
        pcb_t *t = list_entry(n, pcb_t, list);
        if (cpu_allows(t, cpu)) {
            best = t;
            break;
        }
    }

    if (best == 0 || !best->has_wl)
        return best;                        /* 没候选或普通任务：直接选它 */

    /* 任务5 自适应：在"允许本核"的任务之间挑飞得最少的（配额防饿死） */
    for (list_node_t *n = best->list.next; n != &ready_queue; n = n->next) {
        pcb_t *t = list_entry(n, pcb_t, list);
        if (!cpu_allows(t, cpu))
            continue;
        if (!t->has_wl || t->wl_run >= WL_RUN_MAX)
            continue;                       /* 配额用尽：退回普通轮转 */
        if (t->wl_cols < best->wl_cols)
            best = t;
    }

    if (best->has_wl)
        best->wl_run++;                     /* 用掉一格配额（前进时在下方清零） */
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
    pcb_t *idle = &pid0_pcb[get_current_cpu_id()];   /* 本核的空转任务 */

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
    if (prev->status == TASK_RUNNING && prev != idle) {
        prev->status = TASK_READY;
        list_add_tail(&prev->list, &ready_queue);
    } else if (prev == idle) {
        /* 怠速任务平时就标成"不可运行"。严格说这是借用了 BLOCKED 的语义
         * （它不是被谁阻塞，只是没活干）——今天没有副作用，但将来若加
         * ps/kill/wait 这类"按 status 扫全部 PCB"的功能，要记得把 pid0 排除。 */
        prev->status = TASK_BLOCKED;
    }

    pcb_t *next = 0;
    if (!list_empty(&ready_queue))
        next = pick_next();            /* 任务5：飞得最少的先飞；任务4：只挑允许本核的 */

    if (next != 0) {
        list_del(&next->list);
    } else if (prev != idle) {
        /* 本核没活干（队列空，或里面全是"掩码不含本核"的任务）→ 本核怠速。
         * 那些任务留给别的核：全局就绪队列 + 按掩码过滤天然支持跨核迁移。 */
        next = idle;
    } else {
        next = prev;                   /* 已经在怠速且没别的事，继续 */
    }

    next->status = TASK_RUNNING;
    current_running = next;
    core_running[get_current_cpu_id()] = next;   /* 每核 current_running 表（PCB 复用要查） */

    /* 时间片统一为 1/4 个时钟节拍——绝不为 0，飞机不会"停下"。 */
    bios_set_timer(get_ticks() + SLICE_LEN);

    // TODO: [p2-task1] switch_to current_running
    /* P3 任务3：大内核锁在"切换缝隙"里交接（与 entry.S 的 new_task_entry、
     *   trap 入口/出口的 lock/unlock 共同维持不变式，详见 kernel/smp/smp.c）：
     *   · unlock 后本执行流处于"不持锁"状态，另一核才能进内核；
     *   · 被切回的老任务从 switch_to 之后继续（重新 lock）；
     *   · 新任务从 new_task_entry 进（那里先 lock 再走 ret_from_exception）。 */
    unlock_kernel();
    switch_to(prev, next);
    lock_kernel();
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
