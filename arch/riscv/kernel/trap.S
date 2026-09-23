#include <asm.h>
#include <csr.h>

ENTRY(setup_exception)

  /* TODO: [p2-task3] save exception_handler_entry into STVEC */
  /* 任务3：stvec 低两位是 MODE。这里用 Direct 模式（MODE=0），即所有例外/
   *   中断都跳到 base 指向的那一个入口，再由软中断在 C 里按 scause 分发；
   *   handler 是 4 字节对齐的，低两位天然为 0，直接写进去即可。 */
  la   t0, exception_handler_entry
  csrw CSR_STVEC, t0

  /* TODO: [p2-task4] enable interrupts globally */
  /* 任务4：打开 sstatus.SIE（全局中断使能）。单开 sie 的 STIE 位还不够——
   *   sie 只是"允许哪类中断"，sstatus.SIE 才是"允不允许被打断"。
   *   打开后，时钟中断才能在用户态/内核空转时打断进来。 */
  li   t0, SR_SIE
  csrs CSR_SSTATUS, t0

  /* ENTRY/ENDPROC 只打符号表，不生成返回指令——漏了这句会直接"跌进"
   * 下一个函数继续执行（本次调试踩过：跌进 list_add_tail 报 Store/AMO fault） */
  jr   ra

ENDPROC(setup_exception)
