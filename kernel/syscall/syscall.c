#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    /* 任务3：RISC-V 的传参约定（与 Linux 一致）：
     *   调用号放 a7(x17)，参数依次放 a0~a4(x10~x14)，返回值写回 a0。
     *   现场就摆在内核栈顶的 regs_context_t 里，直接读写对应槽位即可。 */
    regs->sepc += 4;    // 跳过 ecall 这条指令；不改的话 sret 回去会立刻再次陷入

    regs->regs[10] = syscall[regs->regs[17]](regs->regs[10], regs->regs[11],
                                             regs->regs[12], regs->regs[13],
                                             regs->regs[14]);
}
