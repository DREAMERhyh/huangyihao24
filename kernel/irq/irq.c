#include <os/irq.h>
#include <csr.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>
#include <os/task.h>
#include <os/mm.h>

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`

    /* scause 的最高位区分"中断"与"例外"：
     *   1 → 中断（异步，如时钟中断），低 63 位是中断号
     *   0 → 例外（同步，如 ecall/env call、缺页），低 63 位是例外号
     * 两者用不同的入口表分发。 */
    if (scause & SCAUSE_IRQ_FLAG)
        irq_table[scause & ~SCAUSE_IRQ_FLAG](regs, stval, scause);
    else
        exc_table[scause](regs, stval, scause);
}

/* 暖重启用的引导参数副本：放 .data 段——head.S 清 BSS 时不会抹掉它
 * （boot_* 那三个全局在 .bss 里，重启会被清零，必须另存一份）。 */
uint64_t boot_saved[3] __attribute__((section(".data"))) = { 1, 1, 1 };

/* 重启内核：等价于把机器热启动一次，回到"选任务"的提示符。
 *   为什么需要它：测试程序都是 while(1) 死循环，调度一旦开始内核就不再读键盘，
 *   按什么键都没反应，只能退出 QEMU 重来。验收时要换一组任务的话，这样最快。
 *   做法就是把内核入口 head.S 的 _start 重跑一遍（清 BSS、重设栈、回 main），
 *   但要自己把 bootblock 传来的三个引导参数重新放进 a0/a1/a2，
 *   否则 init_task_info 会拿到垃圾值。 */
static void restart_kernel(void) __attribute__((noreturn));
static void restart_kernel(void)
{
    /* 就绪/睡眠队列是 .data 里的静态初始化链表，清 BSS 不会复位它们，
     * 得手动断开，免得新任务建 PCB 时挂到旧结点上 */
    list_init(&ready_queue);
    list_init(&sleep_queue);

    asm volatile("csrw sie, zero");                 /* 关中断，重启途中不再被打断 */
    asm volatile("mv a0, %0" ::"r"(boot_saved[0]));
    asm volatile("mv a1, %0" ::"r"(boot_saved[1]));
    asm volatile("mv a2, %0" ::"r"(boot_saved[2]));
    asm volatile("li sp, %0" ::"i"(INIT_KERNEL_STACK));
    asm volatile("la t0, _start\n\t jr t0");

    __builtin_unreachable();
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    /* 跑测试时按 Ctrl+C：停掉当前这组任务，回到选任务的提示符。
     * 内核在调度期间不读键盘，所以这里顺手看一眼（每个时钟节拍一次 M 模式
     * 调用，开销很小）。正常的验收操作不会按到 Ctrl+C。 */
    if (bios_getchar() == 0x03)
        restart_kernel();

    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule

    /* 任务4：时钟中断就是"时间片到期"的信号——每次中断都调度一次，抢走
     *   当前任务对 CPU 的占用。被抢下的任务现场已由 exception_handler_entry
     *   存好，下次调度到它时会从 ret_from_exception 原样恢复、sret 回用户态。
     *
     * 任务5：下一次中断的触发时刻由 do_scheduler 按"下一个任务的时间片"重设
     *   （见 sched.c），所以这里不需要再重设 timer——两条返回路径都设了。 */
    do_scheduler();
}

void init_exception()
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    /* 先把所有例外都指向 handle_other（它会把现场打出来并 assert 掉，
     *   便于定位"踩到没实现的例外"）；只给 ecall 换成正经处理函数。 */
    for (int i = 0; i < EXCC_COUNT; ++i)
        exc_table[i] = handle_other;
    exc_table[EXCC_SYSCALL] = handle_syscall;

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    /* 中断表同理：默认 handle_other（便于抓没实现的中断），
     *   只给 S 态时钟中断挂上真正的处理函数。 */
    for (int i = 0; i < IRQC_COUNT; ++i)
        irq_table[i] = handle_other;
    irq_table[IRQC_S_TIMER] = handle_irq_timer;

    /* TODO: [p2-task3] set up the entrypoint of exceptions */
    setup_exception();
}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    char* reg_name[] = {
        "zero "," ra  "," sp  "," gp  "," tp  ",
        " t0  "," t1  "," t2  ","s0/fp"," s1  ",
        " a0  "," a1  "," a2  "," a3  "," a4  ",
        " a5  "," a6  "," a7  "," s2  "," s3  ",
        " s4  "," s5  "," s6  "," s7  "," s8  ",
        " s9  "," s10 "," s11 "," t3  "," t4  ",
        " t5  "," t6  "
    };
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ",reg_name[i+j], regs->regs[i+j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}
