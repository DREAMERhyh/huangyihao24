#include <asm.h>

// 用户程序的栈：所有 app 共享一个栈，栈顶 0x50400000，向下生长
//   内核栈独享 0x50400000 ~ 0x50500000（KERNEL_STACK = 0x50500000 向下生长），
//   两者互不交叠；app 栈向下到 0x50200000 仍有约 2MB 空间
.equ APP_STACK_TOP, 0x50400000

.section ".entry_function","ax"
ENTRY(_start)

    /* 任务3：为用户程序准备 C 运行环境
     *
     * 先保存内核现场。必须放进 .data 里的固定槽位，原因是：
     *   - 不能借 a0/a1 等临时寄存器：app 的 main 会随意使用它们，一冲就没
     *   - 不能借 s0-s11：内核调进来时 main() 里可能还有活着的 s 寄存器，
     *     而被调用者（也就是本函数）按 ABI 绝不能破坏它们
     * 因此本函数全程只使用 t / a 寄存器 */
    la    t2, saved_ra
    sd    ra, 0(t2)
    la    t3, saved_sp
    sd    sp, 0(t3)

    /* app 的栈：所有 app 共享，栈顶 0x50400000，向下生长 */
    li    sp, APP_STACK_TOP

    /* 清零 app 的 BSS
     *   边界取自本 app 自己 ELF 的 __bss_start / __BSS_END__
     *   （由共用链接脚本 riscv.lds 在该 app 自己的布局中赋值）
     *   注意必须按字节清（sb）：__bss_start 可能不是 8 字节对齐，
     *   例如 auipc 的 __bss_start = 0x52010284 且 bss 只有 4 字节，
     *   用 sd 会越界写坏后面的内存 */
    la    t0, __bss_start
    la    t1, __BSS_END__
clear_loop:
    bge   t0, t1, clear_done
    sb    zero, 0(t0)
    addi  t0, t0, 1
    j     clear_loop
clear_done:

    /* 任务3：进入 app 的 main 函数 */
    call  main

    /* 任务3：app 返回后跳回内核（恢复刚才保存的内核现场） */
    la    t2, saved_sp
    ld    sp, 0(t2)
    la    t3, saved_ra
    ld    ra, 0(t3)
    jr    ra

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)

.data
/* 内核现场保存槽位。必须放 .data 而不是 .bss：
 * 否则上面那段 BSS 清零循环会把自己刚存进去的值抹掉 */
saved_ra: .zero 8
saved_sp: .zero 8
