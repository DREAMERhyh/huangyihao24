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

代码：[init/main.c:326-364](../init/main.c#L326-L364)（`init_pcb`）

对每个被选中的 app：

1. **分配内核栈**：`allocKernelPage(1) + PAGE_SIZE`（`mm.c` 的分配器，地址段见
   任务书表 P2-1：内核动态区 0x50500000-0x52000000）；
2. **分配用户栈**：`allocUserPage(1) + PAGE_SIZE`（用户动态区 0x52500000 起）。
   **两个栈必须分开、每个任务各一套**：用户程序在自己的用户栈上跑用户态代码；
   一陷入内核，现场落在它自己的内核栈上（陷阱帧压在栈顶，C 处理函数的栈帧在下面），
   两者不会互相踩，也不会和别的任务互相踩；
3. **按名从镜像装载**：`load_task_img(名字)`，把 app 搬到它的加载窗口并返回入口地址；
4. 填 PCB：`pid`、`status = TASK_READY`、`cursor_x/y = 0`、`wakeup_time = 0`、
   `time_slice = TIMER_INTERVAL`（任务5 估出成本前的默认值）；
5. **`init_pcb_stack`** 伪造陷阱帧 + switch_to 现场（见 A5）；
6. `list_add_tail` 把 PCB 挂进就绪队列；
7. 最后 `current_running = &pid0_pcb`，并把 pid0 置成 `TASK_RUNNING`。

> 为什么 pid0 也要有 PCB：屏幕光标（`screen_write_ch` 里的 `current_running->cursor_x/y`）、
> 调度都要经由 `current_running`；pid0 是内核自己的空转任务，留在就绪队列里可以在
> 所有用户进程都被阻塞时兜底（任务3 靠它持续转着才能把睡眠进程唤醒）。

### A3. `switchto_context_t` 里会保存哪些内容？为什么？

代码：`include/os/sched.h` 的 `switchto_context_t { reg_t regs[14]; }`

保存 **ra、sp、s0~s11** 共 14 个（顺序即 `switchto_context_t`，偏移即
`SWITCH_TO_RA=0 / SWITCH_TO_SP=8 / SWITCH_TO_S0=16 ...`）。

- 为什么只有这些：RISC-V ABI 把寄存器分为 caller-saved 与 callee-saved。
  `switch_to` 是内核主动调用的**普通函数**，调用点之前 caller-saved 已由编译器
  保存在调用者栈帧里，所以这里只需保存"被调用者必须负责维护"的 callee-saved。
- ra 决定"这个任务被恢复后从哪一条指令继续"，sp 决定"用哪个栈"——这两个不存就回不来。

### A4. 你设计的内核调度器（`do_scheduler`）的工作流程？

代码：[kernel/sched/sched.c:111-156](../kernel/sched/sched.c#L111-L156)

```
1. check_sleeping()              —— 先把睡够的进程放回就绪队列
2. prev = current_running
   prev->cpu_used += now - prev->last_tick   —— 结算它这一轮占的 CPU（任务5 用）
3. prev 若是 RUNNING 且不是 pid0 → 置 READY，挂到就绪队列**尾部**（时间片轮转）
   （已被 do_block 置成 BLOCKED 的不再排回，等于"从 CPU 上消失"）
4. 选下一个：
   · 就绪队列非空        → 取队首（list_entry + list_del）
   · 就绪队列空且不是 pid0 → 让 pid0 上场怠速（内核兜底）
   · 已经在怠速          → 继续跑 pid0
5. 置 RUNNING、current_running = next、next->last_tick = now
6. 任务5：bios_set_timer(now + next->time_slice)
   —— 下一次时钟中断就是"下一个任务时间片用完"的时刻
7. switch_to(prev, next)
```

> **为什么 pid0 不参与轮转**（这是调试出来的，答辩时值得讲）：让它跟着轮转会白占
> 一个时间片；更要命的是它跑的是 `main` 里的 `wfi`，会让 QEMU 在宿主机上真的睡下去，
> 宿主的调度延迟使 guest 的 rdtime 白白流走——而 rdtime 正是抢占用的时钟，于是"排在
> wfi 之后"的任务时间里混进无效流逝，实测能让两个**完全相同**的任务产出差 20%。改成
> "只在没别的任务可跑时上场"后，偏差降到 1~3%（详见 C3）。

调度的触发点有三个：抢占模式下**时钟中断** `handle_irq_timer`、进程主动
`sys_yield`、以及阻塞时的 `do_block`。

### A5. 当一个进程被阻塞时，内核会进行哪些处理？

以互斥锁为例（[kernel/sched/sched.c:173-197](../kernel/sched/sched.c#L173-L197)、
[kernel/locking/lock.c:97-127](../kernel/locking/lock.c#L97-L127)）：

1. 抢不到锁 → `do_block(&current_running->list, &lock->block_queue)`；
2. `do_block`：把 PCB 的 `status` 置 `TASK_BLOCKED`、用**同一个 list 结点**
   挂到该锁的阻塞队列上，然后调 `do_scheduler`；
3. `do_scheduler` 看到 prev 不是 RUNNING，就不再把它排回就绪队列 → 它从 CPU 上消失。
   注意：**它的用户态现场早就存在自己内核栈的陷阱帧里了**（它是通过 ecall 进来的），
   所以在队列里睡着的只是一个 PCB 结点；
4. 唤醒：持有者 `do_mutex_lock_release` → `do_unblock(队首)` → `list_del` +
   `status = READY` + 挂回就绪队列 → 下一次调度它时从陷阱帧恢复、`sret` 回用户态，
   对用户程序而言就是"`sys_mutex_acquire` 返回了"。

> 睡眠（`do_sleep`）走的是同一条路，只是挂在 `sleep_queue` 上并记了 `wakeup_time`。

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
| `irq_table[IRQC_S_TIMER]` | `handle_irq_timer` | [irq.c:43](../kernel/irq/irq.c#L43)，其余进 `handle_other` |
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

代码：[init/main.c:278-324](../init/main.c#L278-L324)

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

> 实现细节：就绪队列为空时 `do_scheduler` 会提前 return，但它**先重设了 timer**，
> 所以时钟不会断，`check_sleeping` 仍会被下一次中断触发。

---

## C. 主动补充（答辩时容易被追问的点）

### C1. 任务 5 的自适应调度算法

代码：[kernel/sched/sched.c:35-109](../kernel/sched/sched.c#L35-L109)

- **内核先验知识为零**：不知道 `CYCLE_PER_MOVE`，也不知道起始位置，只能靠
  `set_sche_workload` 上报的位置。
- **估成本**：用两次上报之间的**差分**反推"每前进一格要烧多少 CPU"：
  `cost = ΔCPU / Δ格数`（起始位置不同不影响差分）。
  **必须用"自起飞以来的累计平均"**：单格成本抖得极凶（一格可能只改几个屏幕格子，
  而每轮收尾要清三行、动上百个格子，实测同一任务单格成本在 6k~290k 之间跳）。
- **分配**：`time_slice ∝ cost`，以"所有已知成本的平均值"为基准、平均时间片取
  `TIMER_INTERVAL`；并有下限 `SLICE_MIN`（**绝不为 0**，题目要求不能有飞机停下）
  和上限 `SLICE_MAX`。
- **为什么这样就同速**：设成本 `c_i`、时间片 `s_i ∝ c_i`，则进程 i 的 CPU 份额
  ∝ `s_i`，单位时间前进的格数 = 份额 / `c_i` = 常数 —— 对所有进程相同。
- **落地**：`do_scheduler` 把 timer 定在 `next->time_slice` 之后，时间片直接就是
  被抢占的间隔，不再被固定节拍量化。
- **实测**（修复"pid0 空转污染时钟"之后）：默认速度下五机走过
  1390/1380/1440/1400/1400 格（±2%）；把 fly1 改成 `CYCLE_PER_MOVE=5`、fly5 改成
  250 且起始位置也从 10 改到 45（相差 **50 倍**）后仍为
  1480/1540/1560/1560/1440 格（±4%）。

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
   ⑤ 把 `main` 里空闲的 `wfi` 临时换成忙等，偏差立刻掉到 -2.8% —— 元凶找到：
   `wfi` 让 QEMU 在宿主上真的睡下去，宿主的调度延迟使 guest 的 rdtime 白流，
   而这些流逝记在了"排在 wfi 之后"的任务头上（本机只有 1 核，load 常 3~9，被放大）；
   ⑥ 修法：pid0 退出轮转、只做兜底怠速 —— print1/print2 从 20% 降到 1~5%，
   五机从 ±15% 降到 ±3.5%。
