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

static spin_lock_t big_kernel_lock;

void smp_init()
{
    /* 主核在唤醒从核之前调用：准备从核的 idle PCB 形式初值
     *   （真值在从核第一次被切换出场时由 switch_to 写入 PCB）。
     *   从核的两张固定栈见 smp.h：BootStack 用于其启动与 idle 循环，
     *   TrapStack 是其陷阱栈（sscratch），与主核的陷阱栈对应。 */
    spin_lock_init(&big_kernel_lock);

    pid0_pcb[1].pid = 0;
    pid0_pcb[1].kernel_sp = SECONDARY_TRAP_STACK;
    pid0_pcb[1].user_sp = SECONDARY_TRAP_STACK;
    /* 从核还没启动（等 wakeup_other_hart），它的 idle 先标为 BLOCKED；
     *   从核自己在 main 的从核路径里置 RUNNING。单核模式下它一直
     *   BLOCKED，ps 里如实反映"这核没在跑"。 */
    pid0_pcb[1].status = TASK_BLOCKED;
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

void lock_kernel()
{
    spin_lock_acquire(&big_kernel_lock);
}

void unlock_kernel()
{
    spin_lock_release(&big_kernel_lock);
}
