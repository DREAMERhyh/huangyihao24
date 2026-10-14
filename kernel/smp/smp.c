#include <atomic.h>
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
 *   · 任务切换缝隙（do_scheduler 的 unlock→switch_to→lock 对偶）：
 *     被切走的执行流停在"不持锁"状态，而无论是被切回的老任务
 *     （从 switch_to 之后继续）还是新任务（从 new_task_entry 进入），
 *     都会先重新 lock 再继续。
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
    /* 向"从核唤醒标志"写 1：从核在 bootblock 里自旋轮询这个地址
     *   （0x50300000，见 smp.h 的 SMP_WAKEUP_ADDR；bootblock.S 的
     *   secondary 段用同一字面量）。
     *
     * ★ 为什么不用 send_ipi（踩坑记录）：主核经 U-Boot 发出的 IPI 是
     *   CLINT MSIP（M 态软中断），要进 M 态 trap 才能被 U-Boot 转发成
     *   SSIP 去唤醒 S 态的从核，而进 M 态 trap 需要 mstatus.MIE=1——
     *   从核 mret 后 MIE=0，且【S 态无权改 MIE】（写 mie/mstatus 直接
     *   非法指令）。换成内存标志后整条链路不再依赖任何中断使能位，
     *   QEMU 多核内存一致直接保证可见性。
     *   ★ 主核 main 的全部初始化完成后才调用（只写一次）：
     *   写标志前的 fence 发布此前所有初始化写入，从核看到标志时
     *   就能安全使用内核数据（BSS/tasks/锁表/pid0_pcb……）。 */
    asm volatile("fence rw, rw" ::: "memory");
    *(volatile uint64_t *)SMP_WAKEUP_ADDR = 1;
    asm volatile("fence rw, rw" ::: "memory");
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
