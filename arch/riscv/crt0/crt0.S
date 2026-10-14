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

    /* 进入 app 的 main 函数 */
    call  main

    /* [P3] main 正常返回后走 sys_exit 退出：
     *   内核由此回收本进程的全部资源（PCB/内核栈/用户栈/持有的锁），
     *   并唤醒所有 waitpid 本进程的等待者。
     *   ★ 原 P2 版本这里是 "loop: wfi" 死循环，P3 必须去掉：
     *   ① U 态执行 wfi 是非法指令（illegal instruction 直接进 M 态，
     *      由 U-Boot 报 "truly_illegal_insn" 并停摆整个系统）；
     *   ② main 正常返回的进程（如 ready_to_exit）若不退出，
     *      waitpid 的等待者永远等不到。 */
    call  sys_exit

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/
// unreachable unless sys_exit has a bug：纯自旋兜底（不再用 wfi，见上）
loop:
    j loop

END(_start)
