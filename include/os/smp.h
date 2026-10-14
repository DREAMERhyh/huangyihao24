#ifndef SMP_H
#define SMP_H

#define NR_CPUS 2

/* P3 任务3：从核的两张固定栈（与 arch/riscv/kernel/head.S 同一字面量；
 * bootblock.S 的 secondary 段用 0x50301000 作为其启动栈）。
 * 选址避开一切已用内存：0x50_2xxx_xxxx 内核镜像、0x504F-0x5050 主核
 * 内核栈/mm 动态区、0x52... app 窗口、0x5310... 用户栈区。 */
#define SECONDARY_BOOT_STACK 0x50301000UL
#define SECONDARY_TRAP_STACK 0x50302000UL

/* P3 任务3：从核的唤醒标志（主核写 1，从核在 bootblock 里自旋等它；
 * bootblock.S 的 secondary 段用同一字面量 0x50300000）。
 * 选址在从核启动栈下方 4KB，避开全部已用内存区。 */
#define SMP_WAKEUP_ADDR 0x50300000UL

extern void smp_init();
extern void wakeup_other_hart();
extern uint64_t get_current_cpu_id();
extern void lock_kernel();
extern void unlock_kernel();

#endif /* SMP_H */
