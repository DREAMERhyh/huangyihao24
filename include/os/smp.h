#ifndef SMP_H
#define SMP_H

#include <os/lock.h>

#define NR_CPUS 2

/* P3 任务4：核亲和掩码的"全允许"值（低 NR_CPUS 位全 1）。
 *   idle 任务与初始进程用它；新进程默认继承父进程的掩码。 */
#define CPU_MASK_ALL ((1UL << NR_CPUS) - 1)

/* P3 任务3：从核的两张固定栈（与 arch/riscv/kernel/head.S 同一字面量；
 * bootblock.S 的 secondary 段用 0x50301000 作为其启动栈）。
 * 选址避开一切已用内存：0x50_2xxx_xxxx 内核镜像、0x504F-0x5050 主核
 * 内核栈/mm 动态区、0x52... app 窗口、0x5310... 用户栈区。 */
#define SECONDARY_BOOT_STACK 0xffffffc050301000UL
#define SECONDARY_TRAP_STACK 0xffffffc050302000UL

/* P3 任务3：从核的唤醒标志（主核写 1，从核在 bootblock 里自旋等它；
 * bootblock.S 的 secondary 段用同一字面量 0x50300000）。
 * 选址在从核启动栈下方 4KB，避开全部已用内存区。 */
#define SMP_WAKEUP_ADDR 0x50300000UL

extern void smp_init();
extern void wakeup_other_hart();

/* P3 任务5（路线A）：本核核号（0/1）。
 *   ★ 曾经是运行时 `csrr mhartid`——细粒度版 + 双核 + 高频 syscall 下
 *   这条指令偶发被判非法指令，U-Boot 跳过后寄存器残值当返回值用，
 *   下游把垃圾当索引 → PC 飞进数据区。现在改读"当前任务 PCB 上的
 *   running_cpu 字段"（current_running 在 tp 寄存器里，见 sched.h）：
 *   核号在启动时各核读一次 mhartid 得到（head.S → main 的参数），
 *   之后由调度器在每次派遣任务时跟随（sched.c do_scheduler），
 *   全内核运行时不再碰 mhartid。 */
extern uint64_t get_current_cpu_id();
extern void lock_kernel();
extern void unlock_kernel();

/* =====================================================================
 * P3 任务5：细粒度锁（make FINE_LOCK=1 编出的第二份镜像）
 *
 * 大锁版：整个 trap 区间持一把 big_kernel_lock（entry.S 里 lock/unlock），
 *   任何核进内核都串行——fly 刷屏（console 系统调用）会连坐所有核上的
 *   一切内核操作（包括与 console 毫无关系的 mutex/condition）。
 * 细粒度版：撤掉大锁（lock_kernel/unlock_kernel 变空函数），按内核资源
 *   分四把域锁；调用点统一写 FINE_LOCK()/FINE_UNLOCK() 宏——大锁版展开
 *   为空，一份源码编出两版行为：
 *     · sched_lock   —— 全部队列（就绪/睡眠/各原语的等待队列）的挂摘、
 *                       所有 pcb.status、core_running、cpu_mask 写；
 *     · sync_lock    —— 同步原语的"非队列数据"：barrier 计数、
 *                       mailbox 环形缓冲、mlocks 的 key/owner；
 *     · console_lock —— 屏幕缓冲、光标、UART 读写；
 *     · loader_lock  —— 任务加载的 staging 缓冲与地址表。
 * 纪律（防死锁，全代码统一）：
 *   ① 锁序恒为 sync_lock → sched_lock → console_lock，禁止反向嵌套；
 *   ② 自旋锁不可重入：do_scheduler 全程持 sched_lock，其内部路径
 *      （check_sleeping/do_unblock/pick_next）不得再拿；
 *   ③ 申请锁与恢复中断配对：临界段都在 irq_save 窗口内，锁只防跨核；
 *   ④ 阻塞纪律不变：判条件+挂队在同一临界段完成，放锁并恢复中断后
 *      才允许 do_scheduler。
 * ===================================================================== */
#ifdef FINE_GRAIN_LOCK
extern spin_lock_t sched_lock;
extern spin_lock_t sync_lock;
extern spin_lock_t console_lock;
extern spin_lock_t loader_lock;
extern spin_lock_t bios_lock;   /* M 态固件入口串行化（诊断修复） */
void fine_locks_init(void);

#define FINE_LOCK(l)   spin_lock_acquire(l)
#define FINE_UNLOCK(l) spin_lock_release(l)
#else
#define FINE_LOCK(l)   do { } while (0)
#define FINE_UNLOCK(l) do { } while (0)
#endif

#endif /* SMP_H */
