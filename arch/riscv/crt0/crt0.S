#include <asm.h>

.section ".entry_function","ax"
ENTRY(_start)

    /* 任务1（P2 与 P1 的关键差别）：P2 里用户程序不再"跑完跳回内核"，而是被
     *   内核调度器反复调度，所以这里：
     *     ① 不再保存/恢复内核现场（那套 saved_ra/saved_sp 是 P1 的用法）；
     *     ② 不能再自己设 sp——进入 _start 时 sp 已经是内核在 init_pcb_stack
     *        里为本任务准备好的用户栈顶。若在这里统一设成某个固定地址，多个
     *        任务就会共用一个栈、互相踩踏。 */

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

    /* 进入 app 的 main 函数；测试程序都是死循环，正常不会返回 */
    call  main

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
