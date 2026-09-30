# P2 任务3 工作日志：真系统调用（ecall/sret）

> 分支：`Project2`。提交：`a1c1059`（p2-task3）。
> 验证环境：QEMU + `loadbootd`。

## 任务书要求（§5.6，印刷页 17）

**实验要求**：
1. 掌握 RISC-V 下系统调用处理流程，实现系统调用处理逻辑；
2. 实现例外入口初始化、程序上下文的保存与恢复、例外结束返回逻辑；
3. 实现 `sys_yield / sys_move_cursor / sys_write / sys_sleep` 等函数；
4. 用全部测试任务（syscall 版本）打印出正确结果（图 P2-8：两个计数、锁两行、
   sleep、timer 共六行）。

**要点（任务书前页）**：进程切换要经过**两次现场保存**——用户态→内核态一次
（save context：全部通用寄存器 + 部分 CSR），`do_scheduler` 内 `switch_to` 再一次
（callee-saved）；返回时 `ret_from_exception` 恢复用户态现场。

## 改动清单

| 文件 | 改动 |
|---|---|
| `arch/riscv/kernel/entry.S` | `SAVE_CONTEXT`（sscratch 换栈 + 288B 陷阱帧 + 清 sstatus.SUM/FS）、`RESTORE_CONTEXT`（先恢复 CSR 再恢复 GPR、最后写回 sscratch）、`exception_handler_entry`、`ret_from_exception` |
| `arch/riscv/kernel/trap.S` | `setup_exception`：`stvec = exception_handler_entry`（Direct 模式）、打开 `sstatus.SIE` |
| `kernel/irq/irq.c` | `interrupt_helper`（按 scause 最高位分发）、`exc_table`（默认 handle_other、ecall→handle_syscall）、`handle_other`（打现场 + assert） |
| `kernel/syscall/syscall.c` | `handle_syscall`：`sepc += 4`、按 a7 取号、a0~a4 传参、返回值写回 a0 |
| `tiny_libc/syscall.c` | `invoke_syscall` 用内联汇编 `ecall`（a7=号、a0~a4=参、a0=返回值，带 clobber），全部 `sys_*` 重新接线 |
| `init/main.c` | `init_syscall`（挂 syscall 表）、`init_pcb_stack` 改为**伪造陷阱帧 + switchto 现场** |
| `kernel/sched/sched.c` | `do_sleep`（记 wakeup_time + do_block 到 sleep_queue）、`check_sleeping`（到点 do_unblock） |
| `kernel/sched/time.c` | `check_sleeping()` 本体；`get_ticks/get_timer/time_base` |
| `test/test_project2/*.c` | 六个测试程序启用 |

## 关键设计决策

1. **用真系统调用（ecall）取代 jmptab 简易通道**：用户侧 `invoke_syscall` 用
   `register long a0 asm("a0") ... a7 asm("a7")` + `ecall`，带完整的输入/输出/clobber
   约束（对比：有的实现靠"函数参数恰好落在 a0~a5"的未声明约定，换优化级别就坏）。
2. **陷阱入口用 `sscratch` 交换 sp**：用户态运行时 sscratch 存"本任务内核栈顶"，
   陷入第一条指令 `csrrw sp, sscratch, sp` 即完成换栈；返回前 `RESTORE_CONTEXT`
   把 sscratch 写回"下一次陷入要用的内核栈顶"。
3. **陷阱帧 = 288B（32 GPR + sstatus/sepc/sbadaddr/scause）**，压在**本任务自己的
   内核栈顶**；内核态全程不重开 SIE（design review C2：否则陷阱帧会压到用户栈上）。
4. **`init_pcb_stack` 的现场升级**：任务1 的"ra=入口、sp=用户栈顶"改成
   "陷阱帧（regs[2]=用户栈顶、regs[4]=PCB、sepc=入口、sstatus=SR_SPIE）+
   switchto 现场（ra=`ret_from_exception`、sp=陷阱帧基址）"——**新任务的第一次运行
   与"被抢占后回来"走同一条路**（`ret_from_exception → sret`）。
5. **`handle_syscall` 的 `sepc += 4`**：ecall 是 4 字节指令，不推进 sepc 会死循环。
6. **sleep 的时间语义**：`wakeup_time = get_timer() + sleep_time`（get_timer 已是秒），
   到点与否在每次 `do_scheduler` 开头的 `check_sleeping` 里比较——非抢占模式下
   由 sys_yield / pid0 空转循环驱动检查（任务4 改成时钟中断驱动）。

## 踩坑与修复

- **`setup_exception` 漏写 `jr ra`**：汇编 ENTRY/ENDPROC 不自动生成返回指令，
  函数直接"跌进"下一个函数（`list_add_tail`）执行，表现为 Store/AMO access fault
  （0x502011c2）。加 `jr ra` 修复。这是本任务最值得讲的一课。
- **sstatus.SUM/FS 的清理**：陷入时要把 SUM（用户内存访问）与 FS（浮点）位清零，
  否则内核代码可能受用户态配置影响；`csrc CSR_SSTATUS` 写在 SAVE_CONTEXT 开头，
  有实现忘了写就成了死代码（对照检查项）。
- **陷阱帧里 sbadaddr 存 stval**：handle_other 会打印 `regs->sbadaddr`，存错寄存器
  会让所有例外现场不可信。
- 用户侧 `sys_*` 包装原走 jmptab，需要同步改成 ecall 通道（两套并存了一小段时间）。

## 验证证据

- 六任务（print1/print2/lock1/lock2/sleep/timer）并发：输出与图 P2-8 同构；
  print1/print2 差 1~4%；锁互斥且换手正常；sleep 打 0~9 后显示
  "This task is sleeping, sleep time is 5." 并按时回来；timer 秒数与 ticks 单调、
  与真实时长吻合（时基 ~10MHz）。
- 复现：`loadbootd` 后输入六个任务名 + 空行；`emu.py` 还原屏幕核对。

## 结论

任务3 的 10 步全部落实；"两次现场保存"的概念在代码里对应 SAVE_CONTEXT（用户态现场，
288B）与 switch_to（内核态现场，14 寄存器）两层，返回路径 `ret_from_exception →
sret` 与任务4 的中断返回共用。
