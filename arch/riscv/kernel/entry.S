#include <asm.h>
#include <csr.h>
#include <asm/regs.h>

.macro SAVE_CONTEXT
  /* TODO: [p2-task3] save all general purpose registers here! */
  /* HINT: Pay attention to the function of tp and sp, and save them carefully! */

  /* 任务3：把用户态现场存到当前任务的**内核栈**上，布局即 regs_context_t。
   *   难点是 sp：陷入时 sp 还是用户栈指针，而现场要落在内核栈顶。这里拿
   *   sscratch 当中转——平时它存着本任务的内核栈顶（每次 sret 回用户态前由
   *   RESTORE_CONTEXT 写好），一条 csrrw 就原子地完成
   *     sp ← 内核栈顶，sscratch ← 用户 sp
   *   交换后再往下让出 288 字节（= sizeof(regs_context_t)）作为陷阱帧。 */
  csrrw sp, CSR_SSCRATCH, sp
  addi  sp, sp, -(OFFSET_SIZE)

  sd x1,  OFFSET_REG_RA(sp)
  sd x3,  OFFSET_REG_GP(sp)
  sd x4,  OFFSET_REG_TP(sp)
  sd x5,  OFFSET_REG_T0(sp)
  sd x6,  OFFSET_REG_T1(sp)
  sd x7,  OFFSET_REG_T2(sp)
  sd x8,  OFFSET_REG_S0(sp)
  sd x9,  OFFSET_REG_S1(sp)
  sd x10, OFFSET_REG_A0(sp)
  sd x11, OFFSET_REG_A1(sp)
  sd x12, OFFSET_REG_A2(sp)
  sd x13, OFFSET_REG_A3(sp)
  sd x14, OFFSET_REG_A4(sp)
  sd x15, OFFSET_REG_A5(sp)
  sd x16, OFFSET_REG_A6(sp)
  sd x17, OFFSET_REG_A7(sp)
  sd x18, OFFSET_REG_S2(sp)
  sd x19, OFFSET_REG_S3(sp)
  sd x20, OFFSET_REG_S4(sp)
  sd x21, OFFSET_REG_S5(sp)
  sd x22, OFFSET_REG_S6(sp)
  sd x23, OFFSET_REG_S7(sp)
  sd x24, OFFSET_REG_S8(sp)
  sd x25, OFFSET_REG_S9(sp)
  sd x26, OFFSET_REG_S10(sp)
  sd x27, OFFSET_REG_S11(sp)
  sd x28, OFFSET_REG_T3(sp)
  sd x29, OFFSET_REG_T4(sp)
  sd x30, OFFSET_REG_T5(sp)
  sd x31, OFFSET_REG_T6(sp)

  /* 用户 sp 此刻还在 sscratch 里，取出来补进现场 */
  csrr t0, CSR_SSCRATCH
  sd   t0, OFFSET_REG_SP(sp)

  /*
   * P4 任务1：保留 SUM=1。内核需要直接读写用户地址（exec 拷 argv、写
   * 子进程用户栈），进入内核后不能把 SUM 清掉；只关 FPU。
   */
  li t0, SR_FS
  csrc CSR_SSTATUS, t0

  /* 任务3：保存 sstatus, sepc, stval 和 scause */
  csrr t0, CSR_SSTATUS
  sd   t0, OFFSET_REG_SSTATUS(sp)
  csrr t0, CSR_SEPC
  sd   t0, OFFSET_REG_SEPC(sp)
  csrr t0, CSR_STVAL
  sd   t0, OFFSET_REG_SBADADDR(sp)
  csrr t0, CSR_SCAUSE
  sd   t0, OFFSET_REG_SCAUSE(sp)

.endm

.macro RESTORE_CONTEXT
  /* TODO: Restore all general purpose registers and sepc, sstatus */
  /* HINT: Pay attention to sp again! */

  /* 任务3：与 SAVE_CONTEXT 对称。sstatus 必须在恢复通用寄存器之前写回，
   *   因为它影响后续指令的执行环境；sepc 决定 sret 的去向。
   *   sbadaddr/scause 是只读 CSR，不需要（也不能）恢复。 */
  ld t0, OFFSET_REG_SSTATUS(sp)
  csrw CSR_SSTATUS, t0
  ld t0, OFFSET_REG_SEPC(sp)
  csrw CSR_SEPC, t0

  ld x1,  OFFSET_REG_RA(sp)
  ld x3,  OFFSET_REG_GP(sp)
  ld x4,  OFFSET_REG_TP(sp)
  ld x5,  OFFSET_REG_T0(sp)
  ld x6,  OFFSET_REG_T1(sp)
  ld x7,  OFFSET_REG_T2(sp)
  ld x8,  OFFSET_REG_S0(sp)
  ld x9,  OFFSET_REG_S1(sp)
  ld x10, OFFSET_REG_A0(sp)
  ld x11, OFFSET_REG_A1(sp)
  ld x12, OFFSET_REG_A2(sp)
  ld x13, OFFSET_REG_A3(sp)
  ld x14, OFFSET_REG_A4(sp)
  ld x15, OFFSET_REG_A5(sp)
  ld x16, OFFSET_REG_A6(sp)
  ld x17, OFFSET_REG_A7(sp)
  ld x18, OFFSET_REG_S2(sp)
  ld x19, OFFSET_REG_S3(sp)
  ld x20, OFFSET_REG_S4(sp)
  ld x21, OFFSET_REG_S5(sp)
  ld x22, OFFSET_REG_S6(sp)
  ld x23, OFFSET_REG_S7(sp)
  ld x24, OFFSET_REG_S8(sp)
  ld x25, OFFSET_REG_S9(sp)
  ld x26, OFFSET_REG_S10(sp)
  ld x27, OFFSET_REG_S11(sp)
  ld x28, OFFSET_REG_T3(sp)
  ld x29, OFFSET_REG_T4(sp)
  ld x30, OFFSET_REG_T5(sp)
  ld x31, OFFSET_REG_T6(sp)

  /* 任务3：两个"不能提前动"的寄存器放在最后。
   *   ① sscratch 要还给"本任务内核栈顶"（= 帧基址 + 288），下次陷入时才换得回来；
   *   ② sp 最后一步才换成用户 sp，之前一直当帧基址用来寻址。 */
  addi t0, sp, OFFSET_SIZE
  csrw CSR_SSCRATCH, t0
  ld   sp, OFFSET_REG_SP(sp)
.endm

ENTRY(enable_preempt)
  not t0, x0
  csrs CSR_SIE, t0
  jr ra
ENDPROC(enable_preempt)

ENTRY(disable_preempt)
  csrw CSR_SIE, zero
  jr ra
ENDPROC(disable_preempt)

ENTRY(enable_interrupt)
  li t0, SR_SIE
  csrs CSR_SSTATUS, t0
  jr ra
ENDPROC(enable_interrupt)

ENTRY(disable_interrupt)
  li t0, SR_SIE
  csrc CSR_SSTATUS, t0
  jr ra
ENDPROC(disable_interrupt)

// NOTE: the address of previous pcb in a0
// NOTE: the address of next pcb in a1
ENTRY(switch_to)
  addi sp, sp, -(SWITCH_TO_SIZE)

  /* 任务1：保存 callee-saved 现场（switchto_context_t 的 14 个槽）。
   *   只存 callee-saved 的依据是 RISC-V ABI：switch_to 是内核"主动"调用的
   *   普通函数，caller-saved 寄存器在调用点已经由 C 编译器自己保存过了。 */
  sd ra,  SWITCH_TO_RA(sp)
  addi t0, sp, SWITCH_TO_SIZE    /* t0 = 进入 switch_to 之前的 sp */
  sd t0,  SWITCH_TO_SP(sp)
  sd s0,  SWITCH_TO_S0(sp)
  sd s1,  SWITCH_TO_S1(sp)
  sd s2,  SWITCH_TO_S2(sp)
  sd s3,  SWITCH_TO_S3(sp)
  sd s4,  SWITCH_TO_S4(sp)
  sd s5,  SWITCH_TO_S5(sp)
  sd s6,  SWITCH_TO_S6(sp)
  sd s7,  SWITCH_TO_S7(sp)
  sd s8,  SWITCH_TO_S8(sp)
  sd s9,  SWITCH_TO_S9(sp)
  sd s10, SWITCH_TO_S10(sp)
  sd s11, SWITCH_TO_S11(sp)

  /* kernel_sp 是 pcb_t 的第一个字段，直接把"本帧基址"存进 prev 的 PCB */
  sd sp, PCB_KERNEL_SP(a0)

  /* 任务1：从 next 的 PCB 里恢复现场。
   *   新任务第一次被调度时，这块现场是 init_pcb_stack 伪造的：
   *   任务3 起 ra = ret_from_exception、sp = 陷阱帧基址（入口在帧的 sepc 里）。 */
  ld sp, PCB_KERNEL_SP(a1)
  ld ra,  SWITCH_TO_RA(sp)
  ld s0,  SWITCH_TO_S0(sp)
  ld s1,  SWITCH_TO_S1(sp)
  ld s2,  SWITCH_TO_S2(sp)
  ld s3,  SWITCH_TO_S3(sp)
  ld s4,  SWITCH_TO_S4(sp)
  ld s5,  SWITCH_TO_S5(sp)
  ld s6,  SWITCH_TO_S6(sp)
  ld s7,  SWITCH_TO_S7(sp)
  ld s8,  SWITCH_TO_S8(sp)
  ld s9,  SWITCH_TO_S9(sp)
  ld s10, SWITCH_TO_S10(sp)
  ld s11, SWITCH_TO_S11(sp)
  ld sp,  SWITCH_TO_SP(sp)       /* sp 必须最后恢复 */

  jr ra
ENDPROC(switch_to)

ENTRY(ret_from_exception)
  /* TODO: [p2-task3] restore context via provided macro and return to sepc */
  /* HINT: remember to check your sp, does it point to the right address? */
  /* 任务3：进 here 时 sp 指向本任务内核栈上的 traps 帧（可能是刚从 sret
   *   返回用户态前的最后一站，也可以是新任务第一次被调度的终点：新任务的
   *   switch_to 现场里 ra 就被伪造成了 new_task_entry → 补锁后跳到这里）。 */
  /* P3 任务3：出内核前释放大内核锁（与 trap 入口的 lock 配对，维持
   *   "内核中执行 ⇒ 持锁"的不变式）。新任务从 new_task_entry 进来时
   *   继承的是上一任调度器交棒的锁，也在这里统一释放（那里不再 lock）。
   *   放在 RESTORE 之前：jal 不压栈；unlock_kernel 自己的栈帧压在陷阱帧
   *   基址【之下】（本任务内核栈的保留区），不碰陷阱帧。 */
  jal  unlock_kernel
  RESTORE_CONTEXT
  sret
ENDPROC(ret_from_exception)

/* P3 任务3：新任务的第一次上场入口（init_pcb_stack 伪造的 ra）。
 *   老任务被切回时从 switch_to 的返回点继续；新任务不经过那里，从这里
 *   直接走公共返回路径。
 * 任务5：两版都在 switch_to 期间由上一任调度器交棒——
 *   · 细粒度版：接棒方在这里放 sched_lock（与 do_scheduler 返回点对偶）；
 *   · 大锁版：BKL 由上一任一直持到这里，**不能再 lock**（自旋锁不可重入）；
 *     直接进 ret_from_exception，由那里的 unlock_kernel 放锁。 */
ENTRY(new_task_entry)
#ifdef FINE_GRAIN_LOCK
  la   a0, sched_lock
  jal  spin_lock_release
#endif
  j    ret_from_exception
ENDPROC(new_task_entry)

ENTRY(exception_handler_entry)

  /* TODO: [p2-task3] save context via the provided macro */
  SAVE_CONTEXT

  /* P3 任务3：进内核立刻拿大内核锁（trap 入口硬件已清 SIE，临界区
   *   不会被本核中断打断；自旋只等另一个核放锁）。
   *   jal 不压栈；被覆盖的 ra 下面立刻会重设。 */
  jal  lock_kernel

  /* TODO: [p2-task3] load ret_from_exception into $ra so that we can return to
   * ret_from_exception when interrupt_help complete.
   */
  la ra, ret_from_exception

  /* TODO: [p2-task3] call interrupt_helper
   * NOTE: don't forget to pass parameters for it.
   */
  mv   a0, sp              /* 参数1：regs_context_t *（陷阱帧） */
  csrr a1, CSR_STVAL       /* 参数2：stval */
  csrr a2, CSR_SCAUSE      /* 参数3：scause */
  /* 用 tail（不写 ra 的跳转）而不是 call：这样 interrupt_helper 返回时
   * 直接落到上面装好的 ret_from_exception 上 */
  tail interrupt_helper

ENDPROC(exception_handler_entry)
