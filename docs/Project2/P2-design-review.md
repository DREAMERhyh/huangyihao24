# Project 2 Design Review 答辩口径

对照《操作系统研讨课-Lecture 2》第 23、46 页的问题清单，逐题给出**结合本项目实际代码**
的回答。所有行号对应当前仓库状态（`git log` 里 5 个任务的提交）。

---

## A. 第 23 页：PCB / 上下文切换 / 调度

### A1. `switch_to` 的工作机制？如何支持两个进程的上下文切换？

代码：[arch/riscv/kernel/entry.S:151-196](../arch/riscv/kernel/entry.S#L151-L196)

- **调用约定**：`switch_to(prev, next)`，`a0 = prev 的 PCB 地址`、`a1 = next 的 PCB 地址`。
  它是**内核主动调用**的一个普通函数（不是被中断/异常打断后进入），因此遵循 RISC-V
  的函数调用 ABI。
- **只保存 callee-saved**：`sd ra / sd sp / sd s0..s11`，正好是
  `switchto_context_t` 的 14 个槽（偏移见 `asm/regs.h` 的 `SWITCH_TO_*`）。
  依据是 ABI：caller-saved 寄存器在"调用 switch_to 之前"就已被 C 编译器保存在
  调用者的栈帧里了，内核函数体里再存一遍是多余的。
- **存在哪**：先把 `sp` 往下挪 112 字节，在**自己（prev）的内核栈**上开一个 switchto 帧；
  `pcb_t` 的第一个字段就是 `kernel_sp`（偏移 0），于是 `sd sp, PCB_KERNEL_SP(a0)`
  一条指令就把"下次从这个栈的哪个位置接着跑"记进了 prev 的 PCB。
- **怎么换过去**：`ld sp, PCB_KERNEL_SP(a1)` 取出 next 的帧基址 → 按偏移恢复
  ra/s0~s11 → 最后 `ld sp, SWITCH_TO_SP(sp)` 换回它的 sp → `jr ra`。后半段执行的已经
  是"next 上次被切走时的现场"，对 next 而言像是从 `switch_to` 里正常返回。
- **新任务没有已保存的现场**，所以 `init_pcb_stack` 会在它的内核栈上**伪造**一份
  （见 A5）：`ra = ret_from_exception`、`sp = 陷阱帧基址`，于是第一次调度到它时走的
  也是这条"恢复 + 返回"的路，只是直接进了用户态。
- **`tp` 的约定**：`current_running` 声明为 `register pcb_t * ... asm("tp")`，
  框架其余部分假定 `tp == current_running`。`do_scheduler` 先改
  `current_running`（即 tp）再调 `switch_to`，所以切换过程中 tp 一直是对的。

### A2. 初始化 PCB 和内核栈时要做哪些事？

代码：[init/main.c:331-362](../init/main.c#L331-L362)（`init_pcb`）

对每个被选中的 app：

1. **分配内核栈**：`allocKernelPage(1) + PAGE_SIZE`（`mm.c` 的分配器，地址段见
   任务书表 P2-1：内核动态区 0x50500000-0x52000000）；
2. **分配用户栈**：`allocUserPage(1) + PAGE_SIZE`（用户动态区 0x52500000 起）。
   **两个栈必须分开、每个任务各一套**：用户程序在自己的用户栈上跑用户态代码；
   一陷入内核，现场落在它自己的内核栈上（陷阱帧压在栈顶，C 处理函数的栈帧在下面），
   两者不会互相踩，也不会和别的任务互相踩；
3. **按名从镜像装载**：`load_task_img(名字)`，把 app 搬到它的加载窗口并返回入口地址；
4. 填 PCB：`pid`、`status = TASK_READY`、`cursor_x/y = 0`、`wakeup_time = 0`；
5. **`init_pcb_stack`** 伪造陷阱帧 + switch_to 现场（见 A5）；
6. `list_add_tail` 把 PCB 挂进就绪队列；
7. 最后 `current_running = &pid0_pcb`，并把 pid0 置成 `TASK_RUNNING`。

> 为什么 pid0 也要有 PCB：屏幕光标（`screen_write_ch` 里的 `current_running->cursor_x/y`）、
> 调度都要经由 `current_running`；pid0 是内核自己的空转任务，**不参与轮转**，只在
> 就绪队列为空时被拎回来怠速兜底（睡眠进程由它的时钟中断驱动 `check_sleeping` 唤醒）。

### A3. `switchto_context_t` 里会保存哪些内容？为什么？

代码：`include/os/sched.h` 的 `switchto_context_t { reg_t regs[14]; }`

保存 **ra、sp、s0~s11** 共 14 个（顺序即 `switchto_context_t`，偏移即
`SWITCH_TO_RA=0 / SWITCH_TO_SP=8 / SWITCH_TO_S0=16 ...`）。

- 为什么只有这些：RISC-V ABI 把寄存器分为 caller-saved 与 callee-saved。
  `switch_to` 是内核主动调用的**普通函数**，调用点之前 caller-saved 已由编译器
  保存在调用者栈帧里，所以这里只需保存"被调用者必须负责维护"的 callee-saved。
- ra 决定"这个任务被恢复后从哪一条指令继续"，sp 决定"用哪个栈"——这两个不存就回不来。

### A4. 你设计的内核调度器（`do_scheduler`）的工作流程？

代码：[kernel/sched/sched.c:95-150](../kernel/sched/sched.c#L95-L150)

```
1. check_sleeping()              —— 先把睡够的进程放回就绪队列
2. prev = current_running
3. prev 若是 RUNNING 且不是 pid0 → 置 READY，挂到就绪队列**尾部**（时间片轮转）
   （已被 do_block 置成 BLOCKED 的不再排回，等于"从 CPU 上消失"）
4. 选下一个（就绪队列非空时进 pick_next）：
   · 队首是飞控任务 → 在所有飞控任务里挑"飞得最少"的那架（任务5，见 C1）
   · 否则            → 拿队首（普通任务照旧 RR）
   · 就绪队列空且不是 pid0 → 让 pid0 上场怠速（内核兜底）
   · 已经在怠速          → 继续跑 pid0
5. 置 RUNNING、current_running = next
6. bios_set_timer(now + SLICE_LEN)   —— 统一时间片（1/4 节拍），下一次时钟中断换人
7. switch_to(prev, next)
```

> **为什么 pid0 不参与轮转**（这是调试出来的，答辩时值得讲）：一是它跟着轮转会白占
> 一个时间片；二是它跑的是 `main` 里的 `wfi`，而 **`wfi` 是 guest 里唯一主动把宿主
> CPU 让出去的地方**——一睡一醒会把宿主调度器的相位与 guest 的轮转**对齐**，于是
> "guest 的 rdtime 是宿主墙钟、不是执行量"这个客观事实，就从随机误差变成**按轮转
> 槽位稳定偏向**的系统性误差，实测能让两个**完全相同**的任务产出差 20%。改成"只在
> 没别的任务可跑时上场"后偏差降到 1~5%（详见 C3；`-icount` 虚拟时钟下偏差为 0.006%，
> 证明公平性本身没问题，问题在时间基准）。

调度的触发点有三个：抢占模式下**时钟中断** `handle_irq_timer`、进程主动
`sys_yield`、以及阻塞让出（`do_block`，或互斥锁抢不到时的 `do_scheduler`）。

### A5. 当一个进程被阻塞时，内核会进行哪些处理？

以互斥锁为例（[kernel/locking/lock.c:97-127](../kernel/locking/lock.c#L97-L127)、
[kernel/sched/sched.c:152-189](../kernel/sched/sched.c#L152-L189)）：

1. `do_mutex_lock_acquire` 用**先入队再抢锁**的无窗口设计：关中断 →
   `list_add_tail(&current_running->list, &lock->block_queue)` →
   `spin_lock_try_acquire(&lock->lock)`。抢到就 `list_del` 把结点从等待队列摘回、
   置 `TASK_RUNNING` 后直接返回；抢不到就置 `TASK_BLOCKED`，恢复中断后**直接调
   `do_scheduler()`**——互斥锁的阻塞路径不经过 `do_block`（后者现在只被 `do_sleep` 用）；
2. `do_scheduler` 看到 prev 不是 RUNNING，就不再把它排回就绪队列 → 它从 CPU 上消失。
   注意：**它的用户态现场早就存在自己内核栈的陷阱帧里了**（它是通过 ecall 进来的），
   所以在队列里睡着的只是一个 PCB 结点；
3. 唤醒：持有者 `do_mutex_lock_release`（关中断里）置锁为空 + `do_unblock(队首)`
   → `list_del` + `status = READY` + 挂回就绪队列；
4. 下一次调度到它时，`switch_to` 从它的内核栈恢复，落回 `do_scheduler()` 的返回处，
   回到 `while(1)` 顶部重抢锁——**抢到才真正从 `sys_mutex_acquire` 返回**
   （这个循环顺带容忍了虚假唤醒）。

> 睡眠（`do_sleep`）是同一套机制的另一个使用者：走 `do_block` 挂到 `sleep_queue`
> 并记下 `wakeup_time`，到点由 `check_sleeping` → `do_unblock` 唤醒；它被唤醒后
> 从陷阱帧恢复、`sret` 回用户态，对用户程序而言就是"系统调用返回了"。
>
> 覆盖面：数据结构天然支持"一个进程同时持多把锁""≥3 个进程抢同一把锁"（每把锁
> 一条独立的等待队列、`mlocks[16]`、`do_unblock` 每次只唤醒队首）——课程验收实测
> 的是双进程单锁这一组。

---

## B. 第 46 页：系统调用 / 中断 / 异常

### B1. 描述 syscall 的执行流程；开发 syscall 时如何实现参数传递？

**参数传递**：RISC-V（与 Linux 一致）约定 —— 调用号放 `a7`，参数依次放 `a0~a4`，
返回值写回 `a0`。

用户侧 [tiny_libc/syscall.c:14-34](../tiny_libc/syscall.c#L14-L34)：

```c
register long a0 asm("a0") = arg0;   /* 用寄存器变量把变量钉在指定寄存器上 */
...
register long a7 asm("a7") = sysno;
asm volatile("ecall" : "+r"(a0) : "r"(a1)... : "memory");
return a0;
```

**为什么只能用寄存器**：用户态和内核态是两个特权级、两套栈，普通函数调用跨不过去；
`ecall` 是唯一的门，寄存器是唯一的通道。

**内核侧**：

```
ecall
 → stvec 指向的 exception_handler_entry（arch/riscv/kernel/entry.S:208）
 → SAVE_CONTEXT：把 32 个通用寄存器 + sstatus/sepc/stval/scause 存进内核栈的陷阱帧
 → tail interrupt_helper（先把 ra 装成 ret_from_exception）
 → interrupt_helper（kernel/irq/irq.c:14）按 scause 最高位分中断/例外；
   scause = 8（U 态 ecall）→ exc_table[8] = handle_syscall
 → handle_syscall（kernel/syscall/syscall.c:5）：
      ① regs->sepc += 4        // 跳过 ecall 指令，否则 sret 回去立刻再陷入
      ② syscall[regs->regs[17]](regs->regs[10..14])   // a7 查表、a0~a4 传参
      ③ 返回值写回 regs->regs[10]   // a0
 → ret_from_exception → RESTORE_CONTEXT（sstatus/sepc 写回 CSR、通用寄存器铺回、
   最后换回用户 sp）→ sret
```

### B2. 初始化中断处理需要设置哪些寄存器，初值是什么？

| 寄存器 | 值 | 位置/理由 |
|---|---|---|
| `stvec` | `exception_handler_entry` 的地址 | [trap.S:4](../arch/riscv/kernel/trap.S#L4)。低两位 MODE=0 表示 **Direct 模式**：所有例外/中断都进同一个入口，再由 C 按 `scause` 分发。handler 4 字节对齐，低两位天然为 0 |
| `sstatus.SIE` | 1 | 同上。`sie` 只说"允许哪类中断"，`sstatus.SIE` 才是"允不允许被打断"，缺一不可 |
| `sie`（SIE 全 1） | `enable_preempt()` | [entry.S:126](../arch/riscv/kernel/entry.S#L126)，`main` 里调用 |
| 时钟触发时刻 | `get_ticks() + TIMER_INTERVAL` | [init/main.c](../init/main.c) 里装第一次；**之后每次中断由 `do_scheduler` 按下一个任务的时间片重设**（Sstc 下就是写 stimecmp，RISC-V 的时钟中断不自动重装） |
| `irq_table[IRQC_S_TIMER]` | `handle_irq_timer` | [irq.c:95](../kernel/irq/irq.c#L95)，其余进 `handle_other` |
| `exc_table[EXCC_SYSCALL]` | `handle_syscall` | 同上，其余进 `handle_other`（打印现场 + assert，便于抓没实现的例外） |
| 新任务的 `sstatus` | `SR_SPIE`(0x20) | `init_pcb_stack`：SPP=0 让 sret 回 **User** 态，SPIE=1 让 sret 后中断使能 |

### B3. 进程因异常进入内核时，需要保存哪些上下文？

代码：[arch/riscv/kernel/entry.S:5-73](../arch/riscv/kernel/entry.S#L5-L73)（`SAVE_CONTEXT`）

- **32 个通用寄存器**（x0 恒定不存）+ **sstatus、sepc、stval、scause** 四个 CSR，
  共 288 字节 = `sizeof(regs_context_t)`，整块压在**本任务内核栈的栈顶**。
- **sp 的特殊处理**（这道题的考点）：陷入时 `sp` 还是用户栈指针，而现场要落在内核栈上。
  用 `sscratch` 中转：平时它存着本任务的内核栈顶（每次 `sret` 回用户态前由
  `RESTORE_CONTEXT` 写好），一条 `csrrw sp, sscratch, sp` 就**原子地**完成
  "sp ← 内核栈顶、sscratch ← 用户 sp"；随后再把 sscratch 里的用户 sp 补进 `regs[2]`。
- **tp** 也照存：它就是 `current_running` 指向的 PCB，恢复时写回 tp 才能保证
  回到用户态后"tp == current_running"仍然成立。
- **为什么要存全**：内核随时可能因为 `sys_yield`/被抢占而切去跑别的任务，回来时
  要能原样恢复。`RESTORE_CONTEXT` 做的是对称操作：sstatus/sepc 写回 CSR →
  通用寄存器铺回 → 最后 `ld sp, OFFSET_REG_SP(sp)` 换回用户 sp → `sret`。

### B4. 开发任务 3 时，`init_pcb_stack` 需要完成哪些初始化？

代码：[init/main.c:283-329](../init/main.c#L283-L329)

它要在任务的内核栈上**伪造两块现场**（因为新任务从没跑过，没有已保存的现场）：

1. **陷阱帧**（`regs_context_t`，288 字节，落在内核栈顶）：
   - `regs[2] (sp)` = 该任务自己的用户栈顶（每个任务一块，互不重叠）
   - `regs[4] (tp)` = 该任务的 PCB（保证 tp == current_running）
   - `sepc` = 程序入口 → `sret` 后从这里开始执行
   - `sstatus` = `SR_SPIE`：**SPP=0 保证 sret 后跑在 User 态**，SPIE=1 保证
     sret 后中断使能
   - 其余清零
2. **switch_to 现场**（`switchto_context_t`，压在陷阱帧下面）：
   - `regs[0] (ra)` = `ret_from_exception`
   - `regs[1] (sp)` = 陷阱帧基址
   - `regs[2..13] (s0~s11)` = 0（新任务没有需要恢复的 callee-saved 变量）
3. `pcb->kernel_sp = switchto 帧基址`、`pcb->user_sp = 用户栈顶`

**效果**：第一次调度到它时，`switch_to` 的收尾把 sp 恢复成陷阱帧基址、`jr ra` 进
`ret_from_exception` → `RESTORE_CONTEXT` 照陷阱帧把寄存器铺好 → `sret` 进用户态从入口跑。
也就是说，**新任务的"第一次运行"和"被抢占后回来"走的是同一条路**。

### B5. 任务 3 和任务 4 中，分别何时唤醒睡眠的进程？

两者**唤醒逻辑相同**：`check_sleeping()`（[kernel/sched/time.c:34](../kernel/sched/time.c#L34)）
扫描睡眠队列，把 `get_timer() >= wakeup_time` 的进程 `do_unblock` 回就绪队列。
区别在于**它是被谁、在什么时候调用的**：

- **任务 3（非抢占）**：`check_sleeping` 在 `do_scheduler` 开头。此时 `do_scheduler`
  只有两个触发源 —— 进程主动 `sys_yield`，以及 pid0 空闲循环里的 `do_scheduler`。
  所以是"有进程主动让出 CPU 时顺手唤醒"；如果所有进程都睡着了，就靠 pid0 在空转
  循环里一次次调度把时间推过去。
- **任务 4（抢占）**：`do_scheduler` 由**时钟中断**触发
  （`handle_irq_timer` → `do_scheduler`），于是每到一个时间片结束就会检查一次睡眠
  队列 —— 即使所有用户进程都在睡，时钟照样响、照样唤醒。

> 实现细节：就绪队列为空时 `do_scheduler` 会换成 pid0 上场怠速（内核在 `wfi` 里
> 睡等下一次时钟中断），timer 照常重设——时钟不会断，`check_sleeping` 仍会被下一次
> 中断触发。

---

## C. 主动补充（答辩时容易被追问的点）

### C1. 任务 5 的自适应调度算法

代码：[kernel/sched/sched.c:33-93](../kernel/sched/sched.c#L33-L93)

- **内核先验知识为零**：不知道 `CYCLE_PER_MOVE`，也不知道起始位置，只能靠
  `set_sche_workload` 上报的位置。
- **只数一件事：每架飞了多少格**。`do_set_sche_workload` 把位置差分攒成
  `wl_cols`（累计前进格数；**回卷那一格也补记**——不补的话短轮任务被系统性少算，
  fly1 每轮 10 格会少算 10%，"谁快谁慢"的排序就被带偏，见 C3）。
  不估成本、不算时间片大小。
- **调度规则（`pick_next`）：谁飞得少谁先飞**。`do_scheduler` 选下一个任务时，
  若队首是飞控任务，就在**所有飞控任务**里挑 `wl_cols` 最小的那架；队首是普通
  任务则照旧拿队首——普通任务按 RR 拿自己那份，**不会被飞控任务饿死**。
  这就是任务书原话的直译："根据实时飞行位置调整优先级，让进度较慢的进程能拥有
  更大或更多的时间片"：落后的一架下一轮就被优先选中，直到追上。
- **★ 插队配额**（代码审查两轮打磨出来的）：凭"落后"插队不是无限次的——**被选中一次
  配额 +1，前进一格清零**；配额用尽（≥`WL_RUN_MAX`=8）就退回普通轮转。两个作用：
  ① 防"进度卡死"霸占 CPU：一个**上报位置永远不变**的程序（助教把 `START_POINT` 改成
  59：每轮只飞一格、上报值恒为 1）永远停在最小值上——没有护栏时其余四架 2 秒后
  **全部冻死**（位置变化只剩 6~8 次）；有配额后同一场景五架全在飞（2352/2354 次），
  且卡死者只剩公平轮转的 1/5 份额；
  ② 让"每格要烧好几片"的慢机拿到**足额补偿**：上限 ≈ 配额−1 个时间片/格（还要扣掉
  上报本身的开销），即"每格耗时 ≤ 约 8 片（≈30ms）"都能被拉平（只按"每格最多多补
  1 片"的旧版上限 ≈ 1 + 快机每格片数 ≈ 1.4；**>4 片/格的区间未实测、属模型推断**）。
  实测（均在配额 8 的构建上）把 fly5 的 `CYCLE_PER_MOVE` 逐级抬高：4000
  （每格 ≈1.9 片）→ 差 0.71%；**9000（每格 ≈3.8 片，已超旧版所有上限）→ 仍锁步，
  差 0.74%**。混合负载同时验过（两轮，各 88 秒）：fly1+print1+fly5 同跑，print1 计数
  持续增长（18809 / 38383 次，绝对值随宿主快慢变、只看"持续增长"；两次相邻自增之间
  最多夹 2 次飞控报数）——**没有被飞控任务饿死**；两轮 fly1/fly5 的**回归差**
  0.59% / 0.33%（第一轮与五机水平相当、第二轮明显更小——print1 把两个飞控在队列里
  隔开，与"位次假设"方向一致；两轮的端点差分别是 0.31% / 0.00%，只作粗参考）。
- **时间片统一为 1/4 个时钟节拍**（`SLICE_LEN = TIMER_INTERVAL/4` ≈ 3.75ms），
  **绝不为 0**（题目要求"不能有飞机停下"——同一个常数对谁都非零，天然成立；
  取 1/4 节拍是为了让每架每次上场只前进两三格，编队看着更连续）。
- **为什么这样就同速**：它相当于 stride / CFS 那类"虚拟时间"调度——被选中的永远
  是累计进度最少的进程，于是五架的累计前进格数被压在一个小窗口里**锁步增长**；
  任何扰动（宿主抢 CPU、时钟抖动、"某格特别贵"）只会把某架压慢一点，随后它会被
  自动补回来。**不需要知道 CYCLE_PER_MOVE，也不怕助教现场改速度和起点。**
- **为什么不用"时间片 ∝ 每格 CPU 成本"**（开发中试过、已删）：它只能对齐长期
  平均，**没有反馈闭环**——实测 54 秒里五机累计进度漂移出 **302 格**（fly1 一整轮
  才 10 格！）、任意 1 秒窗口内速率差 10~42%，肉眼就是"速度明显不一样"。
- **实测**（1 核宿主；用逐格位置轨迹对比，同一台机器）：
  旧（时间片 ∝ 每格成本）：总差 **5.1%**、累计进度偏离最大 **151 格**、
  1 秒窗口速率差中位 21.7%/最大 41.9%；
  新（pick-min）：总差 **0.04~0.1%**（低负载、逐格计数口径）、累计偏离最大 **5 格**、
  1 秒窗口速率差中位 **3.7%**/最大 8.3%（⚠️ 口径：低负载窗口、宿主 load≈0.3~0.6、
  逐格轨迹；同机多会话加高负载时同一指标会升到 10~20%——那是**宿主执行速率的抖动**
  （逐机自身 1 秒窗口速率的 CV 五架几乎相同、≈27~28%），不是调度不公；主口径请用
  累计量，窗口差只当辅助并标注负载）；`-icount` 固定基准下差 **0.33%**
  （五机分别前进 3010/3000/3000/3000/3000 格，每格耗时互差 ≤2.3%）。
  另一路独立实测（以 fly1 的每次上报为时钟、对两机"累计格数"做过原点回归，约 500 个
  采样点；**四组运行，含两组带镜像 md5 的定版复现**）：相对 fly1 存在一个
  **方向可复现的位次相关梯度**（"位次"指机上排序 fly1→fly5，**不是队列槽位**，见下）：
  fly2~fly5 分别慢 **0.27~0.30% / 0.43~0.45% / 0.56~0.61% / 0.67~0.74%**（四组逐位
  重合；倒序复跑量级 0.40~1.02%——**可复现的是方向、不是精确值**，量级在噪声带内）。
  机制**部分定位**：倒序选任务（fly5 先入队、fly1 最后）复跑后 fly1 **仍然最快**
  ——排除了"平局时靠队首者胜"的槽位假设；剩余可疑因素（CPM 与每轮长度同向、未解耦）
  未定论。量级肉眼不可辨，留档备查。
  （端点差口径受各机"每轮 10~50 格"的量化影响、不如回归可靠；窗口差随窗口长度收敛：
  1s→40s 时中位 17%→2~4%。两个口径的数字对不上是粒度/负载差异，不算互相印证。）
- **答辩演示建议**：同速的判据是"**累计进度锁步**"（五架飞过的总格数始终咬在一起，
  差不超过几个格子），不是某一次瞬时采样；这台 1 核宿主逐次数字有噪声，要留档就加
  `-icount shift=3,sleep=off` 把时间基准固定成指令数再跑。

### C2. 为什么内核态不重开中断？

陷入时硬件把 `sstatus.SIE` 清 0，整个陷阱处理过程中不重开。原因：本设计的
`sscratch` 在"内核正在处理例外"期间存的是**用户 sp**，如果这时内核再被时钟中断打断，
`SAVE_CONTEXT` 会把它当内核栈顶用，把陷阱帧写到用户栈上去。
所以锁的临界区用的是 `irq_save / irq_restore`（[kernel/locking/lock.c:16-31](../kernel/locking/lock.c#L16-L31)，
读 sstatus 存原值 → 关 → **按原值恢复**），而不是 `disable_interrupt/enable_interrupt`
直接配对 —— 后者会把内核态 SIE 无条件置 1。

### C3. 踩过的坑（主动说出来，能体现调试能力）

1. **`ENTRY/ENDPROC` 不生成返回指令**：`setup_exception` 漏写 `jr ra` 会直接"跌进"
   下一个函数执行（表现为 Store/AMO access fault）。
2. **抢占模式的第一次调度**：非抢占是 main 自己调 `do_scheduler`；抢占是 main 打开
   中断后被时钟打断，由 `handle_irq_timer` 把第一个任务送上场。因此内核自己也必须
   有合法的 `sscratch`（给它单独一页陷阱栈），否则陷阱帧会写到地址 0。
3. **Makefile 的 app 加载窗口**：原本按"本次构建顺序"逐次累加 `USER_ENTRYPOINT`，
   增量编译时只有改动的 app 重新链接，会拿到已被占用的窗口 —— 两个 app 撞进同一块
   地址、后加载的覆盖先加载的，运行时表现为"某个任务跑的是别人的代码"。已改成
   按名字（排序后序号）算地址。
4. **抢占模式下"先被选中的任务稳定快 20%"**（最曲折的一个）。定位过程值得讲：
   ① 先怀疑刷屏 —— 把 `sys_reflush` 整个关掉，偏差照旧，排除；
   ② 再看"分到的 CPU 少不少" —— 被调度次数 185/185/184、CPU 占比 35.9%/36.7%、
   片长中位数 188k/200k，全都相等，**不是"少给时间"**；
   ③ 于是在内核里**逐事件计数**（每完成一次迭代，各任务让内核做了多少事）：
   刷屏 1.000 次、吐格 1.12 个、putchar 1.12 次、putstr 20.8 字符 —— **两边完全一样**，
   说明"每格活"相同，问题一定在"时间"上；
   ④ 把选择顺序颠倒、再让**同一个程序跑两份**（同代码同窗口，只有 PCB/栈/队列位置
   不同），偏差仍是 26.7% —— 锁定"纯粹是位置效应"；
   ⑤ 把 `main` 里空闲的 `wfi` 临时换成忙等，偏差掉到几个百分点 —— 主要放大器找到；
   ⑥ 加 `-icount shift=3,sleep=off`（虚拟时钟 = 指令数）后偏差彻底消失（Δ0.006%，
   未修复的代码也 Δ≤0.2%）—— 根因确认是"**guest 的 rdtime 是宿主墙钟、不是执行量**，
   而宿主只有 1 核且常超载"，`wfi` 是 guest 里唯一主动让出宿主 CPU 的地方，一睡一醒
   把宿主调度器的相位与 guest 的轮转对齐，误差就从"随机"变成"按槽位稳定偏向"；
   ⑦ 修法：pid0 退出轮转、只做兜底怠速 —— print1/print2 从 20% 降到 1~5%
   （`-icount` 下 Δ0.006%）；五机同速的残差与两种口径见 C1。

### C4. 为什么 printf 每次直接刷屏，而不是像附录 §6.1 说的"在时钟中断里统一刷"？

因为**这就是课程 start code 自己的设计**：上游 `tiny_libc/printf.c` 的 `vprintf`
末尾就是 `sys_write(buff); sys_reflush();`（该文件相对 start code 零改动，逐字节一致），
而 `sys_reflush` 正是任务书点名要我们实现的 `TODO`（题面给的跳转表里没有刷屏项，
我们按名补了一项，见 `init/main.c` 与 `include/os/kernel.h` 的注释）。附录 §6.1
自我声明"并不会介绍新的任务，只是便于同学们理解 start code 中的相关代码"，且它
描述的"printf 只写显存"与 start code 的实际行为并不一致——所以答辩按"与 start code
保持一致、并实现它留的 `sys_reflush` TODO"回答即可。性能上没问题：`sys_reflush`
是**差分刷新**（只写变化过的格子，见 `drivers/screen.c`），五机同屏时每格的刷屏
开销就是"让飞机前进一格"要花的主要成本。
