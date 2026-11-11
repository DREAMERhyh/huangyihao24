#include <atomic.h>
#include <common.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/lock.h>
#include <os/kernel.h>

/* =====================================================================
 * P3 任务3：双核支持
 *
 * 大内核锁（早期 Linux 风格的 BKL）不变量：
 *   "任何在内核态继续执行的执行流，都持有大锁。"
 * 三个交接点（缺一不可，详见各自注释）：
 *   · trap 入口（exception_handler_entry，entry.S）：SAVE 之后 lock；
 *   · trap 出口（ret_from_exception，entry.S）：RESTORE 之前 unlock；
 *   · 任务切换缝隙（do_scheduler 的 switch_to 交棒）：
 *     switch_to 全程持锁；被切回的老任务（从 switch_to 返回点继续）和
 *     新任务（从 new_task_entry 进入）都继续持有这把锁，直到本核当前
 *     执行流真正返回用户态的 ret_from_exception 才统一放。
 *     ★ 不能在 switch_to 之前放锁：prev 已入就绪队列但现场还没写，
 *     另一核会挑到旧 kernel_sp（大锁版历史上就是这条偶发崩溃）。
 * 锁的临界区里 SIE=0（trap 入口硬件已清），自旋等待只等"另一个核"
 * 放锁，不会被本地中断打断。
 * ===================================================================== */

#ifdef FINE_GRAIN_LOCK
/* ---------------------------------------------------------------------
 * 细粒度版：四把域锁（纪律——锁序/不可重入/阻塞纪律——见 smp.h 注释）。
 * 大锁版：一把 big_kernel_lock，语义与原实现完全一致。 */
spin_lock_t sched_lock;
spin_lock_t sync_lock;
spin_lock_t console_lock;
spin_lock_t loader_lock;
/* 诊断锁：把"进 M 态固件（U-Boot）"的入口串行化。
 *   大锁版里 BKL 在 trap 入口就被持有，"ecall 进 M 态"天然全串行；
 *   细粒度版撤掉 BKL 后双核可同时 ecall——实测会诱发 U-Boot M 态
 *   并发问题（间歇性 csrr mhartid 被判非法 → 指令被跳过 → 下游内存
 *   破坏 → PC 飞进数据段）。本锁只在 call_bios 唯一入口包住整段
 *   M 态调用，复现大锁版的串行语义。 */
spin_lock_t bios_lock;

void fine_locks_init(void)
{
    spin_lock_init(&sched_lock);
    spin_lock_init(&sync_lock);
    spin_lock_init(&console_lock);
    spin_lock_init(&loader_lock);
    spin_lock_init(&bios_lock);
}

/* 细粒度版里"大内核锁"不存在：trap 入口/出口的 lock_kernel 调用点保持
 * 不动，这里退化为空函数（一条 jal+ret，开销可忽略）。 */
void lock_kernel(void)   { }
void unlock_kernel(void) { }
#else
static spin_lock_t big_kernel_lock;
#endif

void smp_init()
{
    /* 主核在唤醒从核之前调用：准备从核的 idle PCB 形式初值
     *   （真值在从核第一次被切换出场时由 switch_to 写入 PCB）。
     *   从核的两张固定栈见 smp.h：BootStack 用于其启动与 idle 循环，
     *   TrapStack 是其陷阱栈（sscratch），与主核的陷阱栈对应。 */
#ifdef FINE_GRAIN_LOCK
    fine_locks_init();
#else
    spin_lock_init(&big_kernel_lock);
#endif

    pid0_pcb[1].pid = 0;
    pid0_pcb[1].kernel_sp = SECONDARY_TRAP_STACK;
    pid0_pcb[1].user_sp = SECONDARY_TRAP_STACK;
    pid0_pcb[1].cpu_mask = CPU_MASK_ALL;
    /* 从核还没启动（等 wakeup_other_hart），它的 idle 先标为 BLOCKED；
     *   从核自己在 main 的从核路径里置 RUNNING。单核模式下它一直
     *   BLOCKED，ps 里如实反映"这核没在跑"。 */
    pid0_pcb[1].status = TASK_BLOCKED;
}

/* P3 任务5（路线A）：核号 = 本核当前任务的 PCB 字段。
 *   current_running 是 tp 寄存器（sched.h），本核每次派遣任务前都会把
 *   "派遣核"写进 next->running_cpu（sched.c do_scheduler），所以：
 *     · 本核 idle / 用户任务 / 内核任意路径上，读到的都是本核核号；
 *     · 任务被迁移到另一核后，由新核的调度器改写，读回新核号；
 *   ★ 这里【只读内存、不再读 CSR】——正是为绕开细粒度版双核高频 syscall
 *   下偶发的 `csrr mhartid 非法指令`（详见 smp.h / sched.h 注释）。 */
uint64_t get_current_cpu_id()
{
    return current_running->running_cpu;
}

void wakeup_other_hart()
{
    /* [2026-10-28 板上修复] 按任务书 §5.1 / 往届标准做法：用 IPI 唤醒从核。
     *   从核在 bootblock 里已把 stvec 设成内核入口并开了 S 态软件中断；
     *   bbl/u-boot 的 M 态把主核的 MSIP 转发成 S 态软中断后，从核由
     *   trap 直接进内核入口（head.S 的从核分支）。
     *   ★ 主核 main 的全部初始化完成后才调用（只发一次）。 */
    /* 只发给从核 hart 1（mask bit1）——往届标准做法；send_ipi(NULL)
     * 会连主核自己一起发，主核收到 SSIP 会多走一次陷阱。 */
    unsigned long mask = 0x2;
    send_ipi(&mask);
}

#ifndef FINE_GRAIN_LOCK
void lock_kernel()
{
    spin_lock_acquire(&big_kernel_lock);
}

void unlock_kernel()
{
    spin_lock_release(&big_kernel_lock);
}
#endif
