/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *        Process scheduling related content, such as: scheduler, process blocking,
 *                 process wakeup, process creation, process kill, etc.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#ifndef INCLUDE_SCHEDULER_H_
#define INCLUDE_SCHEDULER_H_

#include <type.h>
#include <os/list.h>
#include <os/task.h>
#include <os/smp.h>

/* P4：consensus 会同时有 1 个父进程 + 8 个子进程，mailbox 每个进程还有
 * 2 个线程；32 个 PCB 槽位足够覆盖 C-core 全部测试。 */
#define NUM_MAX_TASK 48

/* used to save register infomation */
typedef struct regs_context
{
    /* Saved main processor registers.*/
    reg_t regs[32];

    /* Saved special registers. */
    reg_t sstatus;
    reg_t sepc;
    reg_t sbadaddr;
    reg_t scause;
} regs_context_t;

/* used to save register infomation in switch_to */
typedef struct switchto_context
{
    /* Callee saved registers.*/
    reg_t regs[14];
} switchto_context_t;

typedef enum {
    TASK_BLOCKED,
    TASK_RUNNING,
    TASK_READY,
    TASK_EXITED,
} task_status_t;

/* Process Control Block */
typedef struct pcb
{
    /* register context */
    // NOTE: this order must be preserved, which is defined in regs.h!!
    reg_t kernel_sp;
    reg_t user_sp;
    ptr_t kernel_stack_base;
    ptr_t user_stack_base;

    /* previous, next pointer */
    list_node_t list;
    list_head wait_list;

    /* process id */
    pid_t pid;

    /* P4：线程号（主线程 tid == pid；pthread 线程 tid 唯一） */
    pid_t tid;

    /* P4：线程与进程的区别。线程共享 pid 和 pgdir，只独占内核栈/用户栈。 */
    int is_thread;

    /* P4：本进程的线程数（只在主线程 PCB 上有意义，用于最后一个线程退出时
     * 回收页表）。 */
    int thread_count;

    /* P4：本任务使用的根页表（内核虚拟地址）。idle 用内核页表。 */
    ptr_t pgdir;

    /* P3：进程名（exec 按名启动 / ps 显示）。
     * ★ 槽位空闲的判据：name[0] == '\0'（pcb[] 静态零初始化天然如此）。 */
    char name[TASK_NAME_LEN];

    /* P3 任务4：核亲和掩码——bit i = 允许在第 i 号核上运行。
     *   exec 默认继承父进程；taskset 可启动时指定、或对已存在进程修改。
     *   调度器 pick_next 只从"掩码允许本核"的任务里挑（见 sched.c）。 */
    uint64_t cpu_mask;

    /* P3 任务5（路线A）：本任务此刻"在哪个核上跑"。
     *   核号的携带方式：启动时每个核只读一次 mhartid（head.S → main 的参数），
     *   写进自己的 idle PCB；此后每次调度由本核把核号写进 next->running_cpu
     *   （sched.c do_scheduler）。get_current_cpu_id() 直接返回
     *   current_running（tp 寄存器）的这个字段——任务迁到别的核后，
     *   新核派遣它时会改写，核号自动跟随。
     *   ★ 全内核不再有运行时的 csrr mhartid：细粒度版双核高频 syscall 下
     *   它偶发被判非法指令，U-Boot 跳过后寄存器残值被当成返回值 → 崩溃。 */
    int running_cpu;

    /* BLOCK | READY | RUNNING */
    task_status_t status;

    /* cursor position */
    int cursor_x;
    int cursor_y;

    /* time(seconds) to wake up sleeping PCB */
    uint64_t wakeup_time;

    /* ---- 任务5（C-core 自适应调度）----
     * 5 架飞机的 CYCLE_PER_MOVE 不同，"前进一格"要烧的 CPU 不一样，公平轮转下
     * 它们就会飞得不一样快。内核不需要知道 CYCLE_PER_MOVE：靠 set_sche_workload
     * 上报的位置数出"每架飞了多少格"，调度时让飞得最少的那架先飞（见 pick_next）。 */
    int      wl_pos;       /* 最近一次上报的进度（workload = 60 - 列号，前进则变小） */
    int      wl_cols;      /* 自起飞以来累计前进的格数（回卷那一格也补记） */
    int      wl_run;       /* 连续被选中、且其间没前进过几次（插队配额，前进即清零） */
    int      has_wl;       /* 是否已经有过一次上报 */

    /* ---- 任务5：唤醒纪律（wake_pending）----
     *   任务仍在某个核上执行时被唤醒（典型窗口：它在 do_block/block_on_queue
     *   里放下 sched_lock 之后、走进自己的 do_scheduler 之前的那几步，另一核
     *   恰好替它放锁并 do_unblock 它），此刻若把它挂进就绪队列就放任不管，
     *   另一核的 pick_next 会把它挑走——两个核同跑一个 PCB（同一个内核栈），
     *   现场互相踩踏（实测崩溃签名 epc=ra=&sched_lock，300s 验证里 hit1=4）。
     *   所以这种"提前唤醒"只打标记：pick_next 见到它先跳过，直到它自己的
     *   do_scheduler 开头清掉标记（那时它已经/正在经交棒切走，全程持
     *   sched_lock，别的核挑不走）。
     *   置位者：do_unblock（持 sched_lock）；清位者：该任务的 do_scheduler。 */
    int      wake_pending;

} pcb_t;

/* ready queue to run */
extern list_head ready_queue;

/* sleep queue to be blocked in */
extern list_head sleep_queue;

/* current running task PCB */
register pcb_t * current_running asm("tp");
extern pid_t process_id;

extern pcb_t pcb[NUM_MAX_TASK];
/* P3 任务3：每个核一份空转任务（idle）PCB：pid0_pcb[hartid]。
 *   主核 [0] 的初值见 sched.c；从核 [1] 的形式初值在 smp_init 里填。 */
extern pcb_t pid0_pcb[NR_CPUS];
extern const ptr_t pid0_stack;

/* P3 任务3：每核"正在运行"的 PCB（do_scheduler 在大锁内维护）。
 *   用途：找一个"没有任何核正在使用"的 PCB 槽位来复用（alloc_pcb）。
 *   本核自己的可以直接读 current_running（tp 寄存器），别的核只能查这里。
 *   任务5（路线A）：本核自己的也用来回答"我是哪个核"——
 *   get_current_cpu_id() == current_running->running_cpu。 */
extern pcb_t *core_running[NR_CPUS];

/* 是否有核"还在用"这个 PCB（正在跑 / 正被切换）。判据是 core_running[]——
 *   由 do_scheduler 在派遣时写入、该核下次调度才改写。细粒度版整个
 *   switch_to 都在 sched_lock 内（交棒），所以持锁读到"不在表上"严格蕴含
 *   "已完整下核"：alloc_pcb 敢回收它的栈、do_unblock 敢直接挂回就绪队列。
 *   （大锁版放锁与 switch_to 之间还剩几条汇编指令的窗口，属历史遗留，
 *   实测无影响——大锁从不崩。） */
static inline int task_on_cpu(pcb_t *t)
{
    for (int c = 0; c < NR_CPUS; ++c) {
        if (core_running[c] == t)
            return 1;
    }
    return 0;
}

extern void switch_to(pcb_t *prev, pcb_t *next);
void do_scheduler(void);
void do_sleep(uint32_t);

void do_block(list_node_t *, list_head *queue);
void do_unblock(list_node_t *);

/* 任务5：接收某个 fly 进程上报的当前位置，累计它飞过的格数（调度按此挑人） */
void do_set_sche_workload(int workload);

/************************************************************/
/* TODO [P3-TASK1] exec exit kill waitpid ps*/
#ifdef S_CORE
extern pid_t do_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2);
#else
/* 任务4：第 4 参 = 核亲和掩码（0 = 继承父进程；taskset 启动时传非 0） */
extern pid_t do_exec(char *name, int argc, char *argv[], uint64_t mask);
#endif
extern void do_exit(void);
extern int do_kill(pid_t pid);
extern int do_waitpid(pid_t pid);
/* P4：pthread_create/join 的内核实现 */
extern pid_t do_pthread_create(void (*entry)(void *, void *),
                               void (*start_routine)(void *), void *arg);
extern int do_pthread_join(pid_t tid);
/* 任务4：修改已存在进程的核亲和掩码，成功返回 1（找不到进程返回 0） */
extern int do_taskset_p(pid_t pid, uint64_t mask);
extern void do_process_show();
extern pid_t do_getpid();
/* shell 的串口轮询：读一个字符，无输入返回 -1（P3 任务1 加） */
extern int do_getchar(void);
/************************************************************/

#endif
