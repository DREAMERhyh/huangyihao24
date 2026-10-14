#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/time.h>
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

// bootblock 经 head.S 传来的引导参数（任务5 起为【字节】语义）：
//   a0 = AppInfo 起始字节偏移，a1 = 用户程序数目，a2 = apps 区起始字节偏移
//   定义见 task.h；赋值在 head.S 清完 BSS 后完成
uint64_t boot_appinfo_off;
uint64_t boot_appnum;
uint64_t boot_apps_base_off;

// Task info array
task_info_t tasks[TASK_MAXNUM];

// 镜像里记录的用户程序数目（createimage 写在镜像偏移 0x1f8）
int task_num = 0;

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;
    jmptab[SCREEN_REFLUSH]  = (long (*)())screen_reflush;
}

static void init_task_info(void)
{
    // 任务5：从 AppInfo 区读出 task_info_t 数组，填进 tasks[]。
    //   位置与数量来自 bootblock 传参（head.S 已存进 boot_appinfo_off /
    //   boot_appnum），不再直接读 0x50200xxx。
    //
    //   ★ AppInfo 在镜像里**不按扇区对齐**（字节级紧密排列），而块设备只能
    //   整扇区读、没有偏移参数。所以必须：
    //     ① 算出 AppInfo 落在哪个扇区、扇区内偏移多少；
    //     ② 把"覆盖它的若干个整扇区"读进中转缓冲；
    //     ③ 从缓冲的 offset 处起，拷 APPINFO_BYTES 字节进 tasks[]。
    //   中转缓冲需 2 个扇区：offset ≤ 511，APPINFO_BYTES = 448，
    //   故 offset + 448 ≤ 959 < 1024。
    static uint8_t appinfo_buf[2 * SECTOR_SIZE]
        __attribute__((aligned(SECTOR_SIZE)));

    task_num = (int)boot_appnum;

    if (task_num <= 0 || task_num > TASK_MAXNUM) {
        task_num = 0;
        return;
    }

    uint32_t start  = (uint32_t)boot_appinfo_off;
    uint32_t sector = start / SECTOR_SIZE;              // 从哪块开始读
    uint32_t offset = start % SECTOR_SIZE;              // 块内偏移多少
    uint32_t need   = NBYTES2SEC(offset + APPINFO_BYTES);   // 要读几块

    if (need > sizeof(appinfo_buf) / SECTOR_SIZE) {     // 防御（不会发生）
        task_num = 0;
        return;
    }

    if (bios_sd_read((unsigned)(unsigned long)appinfo_buf, need,
                     sector) != 0) {
        task_num = 0;
        return;
    }

    // 只拷"有内容的那几行"（task_num × 28 字节），从缓冲的 offset 处起
    memcpy((void *)tasks, (const void *)(appinfo_buf + offset),
           (size_t)task_num * sizeof(task_info_t));
}

/************************************************************/
static void init_pcb(void)
{
    // TODO: [p2-task1] initialize 'current_running'
    // 内核自己也得有个 PCB：调度、屏幕光标等都是经由 current_running 访问的。
    //   pid0 是内核的空转任务，初始置 RUNNING 只为对齐 current_running；它不参与
    //   轮转、也不进就绪队列，只在就绪队列为空时被拎回来怠速（睡眠唤醒靠它的时钟中断）。
    current_running = &pid0_pcb;
    pid0_pcb.status = TASK_RUNNING;

    // P3 任务1：内核初始化完成后，直接启动 shell，让它作为 pid 号为 1 的
    //   第一个用户态进程（任务书约定）。shell 负责从串口读命令、用 exec
    //   启动所有其余任务——内核不再提供"选任务"的交互界面。
    pid_t shell_pid = do_exec("shell", 0, 0);
    assert(shell_pid == 1);
}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    // 把系统调用号映射到内核函数上（用户侧 invoke_syscall 用 a7 传号）
    syscall[SYSCALL_SLEEP]        = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD]        = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE]        = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR]       = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH]      = (long (*)())screen_reflush;
    syscall[SYSCALL_CLEAR]        = (long (*)())screen_clear;
    syscall[SYSCALL_GET_TIMEBASE] = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK]     = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT]    = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ]     = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_SET_WORKLOAD] = (long (*)())do_set_sche_workload;

    // P3 任务1：进程管理 + 终端
    syscall[SYSCALL_EXEC]         = (long (*)())do_exec;
    syscall[SYSCALL_EXIT]         = (long (*)())do_exit;
    syscall[SYSCALL_KILL]         = (long (*)())do_kill;
    syscall[SYSCALL_WAITPID]      = (long (*)())do_waitpid;
    syscall[SYSCALL_PS]           = (long (*)())do_process_show;
    syscall[SYSCALL_GETPID]       = (long (*)())do_getpid;
    syscall[SYSCALL_READCH]       = (long (*)())do_getchar;
}
/************************************************************/

int main(void)
{
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information from bootblock-passed AppInfo (〃'▽'〃)
    init_task_info();

    // Init Process Control Blocks |•'-'•) ✧
    init_pcb();
    printk("> [INIT] PCB initialization succeeded.\n");

    // Read CPU frequency (｡•ᴗ-)_
    time_base = bios_read_fdt(TIMEBASE);

    // Init lock mechanism o(´^｀)o
    init_locks();
    printk("> [INIT] Lock mechanism initialization succeeded.\n");

    // Init interrupt (^_^)
    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");

    // Init system call table (0_0)
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");

    // Init screen (QAQ)
    init_screen();
    printk("> [INIT] SCREEN initialization succeeded.\n");

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's

    // ① 先给时钟装上第一次触发时间，只需要在这里武装一次；之后每次调度
    //    （do_scheduler）都会按下一个任务的时间片重设下一次触发时刻。
    bios_set_timer(get_ticks() + TIMER_INTERVAL);

    // ② 内核自己也要能被中断打断（抢占模式下的"第一次调度"正是由这次时钟
    //    中断发起的：内核空转时被时钟打断 → handle_irq_timer → do_scheduler
    //    → 第一个任务被调度上场）。但陷阱入口要靠 sscratch 找内核栈顶，所以
    //    先给它一页专用的陷阱栈——直接借用 main 现在用的内核栈会和 main 的
    //    栈帧重叠。
    asm volatile("csrw sscratch, %0" : : "r"(allocKernelPage(1) + PAGE_SIZE));

    // ② 再打开 sie 的 STIE 位（允许时钟中断）。此时 sstatus.SIE 已经是 1
    //    （内核态由 setup_exception 打开，用户态由 sret 按陷阱帧的 SPIE 恢复），
    //    所以中断立刻就会来——抢占模式下的"第一次调度"就由这次时钟中断发起。
    enable_preempt();

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // 抢占模式下不再需要主动让出 CPU：时钟中断自己会触发 do_scheduler。
        // 这里是内核（pid0）的空转：wfi 睡到下一次中断，醒来后继续参与调度。
        // 注意不能用 do_scheduler() 空转——那会让 pid0 白占一个时间片。
        /* pid0 只在"没别的任务可跑"时才到这里，所以放心睡等时钟；
         * 有任务在跑时它根本不参与轮转（见 do_scheduler）。 */
        asm volatile("wfi");
    }

    return 0;
}
