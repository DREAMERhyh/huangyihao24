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
    if (scause & SCAUSE_IRQ_FLAG) {
        uint64_t code = scause & ~SCAUSE_IRQ_FLAG;
        if (code >= IRQC_COUNT) {
            handle_other(regs, stval, scause);
            return;
        }
        irq_table[code](regs, stval, scause);
    } else {
        if (scause >= EXCC_COUNT) {
            handle_other(regs, stval, scause);
            return;
        }
        exc_table[scause](regs, stval, scause);
    }
}

/* ★ P3 起删掉了 P2 的"Ctrl+C 暖重启"：
 *   ① 它靠时钟中断里轮询 bios_getchar 实现，而 P3 的 shell 是串口的正经
 *      消费者——两个读者并存时，用户敲的字符可能被中断抢先读走再丢掉，
 *      shell 就丢输入。串口只能有一个读者，归还给 shell。
 *   ② P3 的测试进程都能正常 exit（不再是 P2 那样的死循环），不需要重启
 *      换任务组；真卡死了，在 QEMU 层重跑脚本即可。 */

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
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
    /* P4：三类缺页都走同一个按需调页/换页处理程序 */
    exc_table[EXCC_INST_PAGE_FAULT]  = handle_page_fault;
    exc_table[EXCC_LOAD_PAGE_FAULT]  = handle_page_fault;
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_page_fault;

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
