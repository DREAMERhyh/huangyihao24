# P3 任务5 工作日志：细粒度锁、C-core 自适应调度与双核稳定性战役

> 分支：`Project3`。相关提交：`0d9a9bb`（本任务）。
> 验证环境：QEMU `-smp 2` + `loadbootm`；对比数据用
> `QEMU_EXTRA="-icount shift=3,sleep=off"` 采集（虚钟=指令量，
> 免疫宿主负载——P2 的老教训，见下）。
> 两份镜像：`make`（大内核锁，对照）与 `make FINE_LOCK=1`（细粒度锁）。

## 任务书要求（§5.4-5.6，课件 Lecture 3 p28）

1. **细粒度锁**：把大内核锁拆成按资源的细粒度锁，多核各自进入内核互不排队；
2. **两份镜像**（大锁 / 细粒度）在 **fly 干扰** 下的对比：同一负载下，
   细粒度版的吞吐下降明显小于大锁版（课件口径：开 fly 后收包/消费速率
   下降不超过一半）；
3. **C-core 自适应调度**：5 架 `CYCLE_PER_MOVE` 不同的飞机要飞得一样快——
   不猜每格要多少 CPU，用"进度"当反馈，落后多飞（本题"任何时间片不得为 0"）。

## 改动清单

| 文件 | 改动 |
|---|---|
| `Makefile` | `FINE_LOCK=1` 开关（`-DFINE_GRAIN_LOCK`，随 KERNEL_CFLAGS 一起进 .c 与 .S） |
| `include/os/smp.h` + `kernel/smp/smp.c` | 五把域锁：`sched_lock / sync_lock / console_lock / loader_lock / bios_lock`；锁序注释；`lock_kernel/unlock_kernel` 在细粒度版退化为空函数 |
| `kernel/locking/lock.c` | 所有同步原语按域加锁（判条件+挂队同处一个跨核临界段） |
| `kernel/sched/sched.c` + `include/os/sched.h` | 调度域加锁；`running_cpu`（路线A 核号）；`wake_pending`（唤醒纪律）；`task_on_cpu()` 判据；C-core pick-min + `WL_RUN_MAX=8` 配额；时间片 1/4 节拍 |
| `arch/riscv/kernel/entry.S` | `new_task_entry` 放**交棒**的 sched_lock（与 do_scheduler 返回点对偶） |
| `arch/riscv/kernel/head.S` + `init/main.c` | **路线A**：`csrr mhartid` 只在启动各读一次（经 a0 传进 main），运行期零 M 态 trap |
| `arch/riscv/kernel/smp.S` | 删除（唯一内容就是运行期 `csrr mhartid`） |
| `arch/riscv/bios/common.c` + `kernel.h` | `call_bios` 全程持 `bios_lock`（M 态串行化）；`bios_set_timer` 保留 `call_jmptab`（sstc 路线实验失败，注释留档） |
| `drivers/screen.c` | 屏幕写按 `console_lock` 串行 |
| `include/os/task.h`、`tools/createimage.c` | 随镜像布局/任务结构的小幅配套调整 |
| `test/test_project3/{producer_rate,consumer_rate}.c` | §5.6 生产/消费速率测试（每秒打印速率，按真实经过 tick 归一） |

## 关键设计决策

1. **五把域锁与锁序**（`sync → sched → console`，`bios` 最内）：
   - `sched_lock`：就绪/睡眠/阻塞队列、task 状态、`core_running`、栈池、PCB 池；
   - `sync_lock`：barrier/condition/mailbox 元数据与 `mlocks/*.owner`；
   - `console_lock`：屏幕缓冲；`loader_lock`：exec 的 staging 缓冲与地址表；
   - `bios_lock`：**进 M 态固件（U-Boot）的整段串行化**——见调试战役①。
   - 纪律：自旋锁不可重入；带锁绝不进 `do_scheduler`；被阻塞的原语
     "判条件+挂队"必须同处一个跨核临界段。
2. **路线A（核号的携带）**：把"我是哪个核"从运行期 `csrr mhartid`（M 态 trap）
   改成 PCB 字段 `running_cpu`：启动时每核只读一次 mhartid 写进自己的 idle PCB，
   之后每次派遣由本核写 `next->running_cpu`。任务迁核后核号自动跟随。
3. **sched_lock 交棒**：`do_scheduler` 从入口持锁直到 **switch_to 完成之后**
   才由"接棒方"（被切回的老任务 / 新任务的 `new_task_entry`）放锁——
   消灭了"prev 已挂回就绪队列、现场却还没保存"的窗口（见调试战役②）。
4. **唤醒纪律 `wake_pending`**：`do_unblock` 若发现被唤醒者**还在某个核上**
   （`task_on_cpu()`，它的 core_running 项还在），只把它挂回就绪队列并打标记，
   `pick_next` 见到标记先跳过；由它自己的 `do_scheduler` 开头清标记
   （此刻它要么已完整下核，要么正持着交棒的锁切走）——消灭"双核同跑一个
   PCB"的另一条窗口（见调试战役③）。
5. **栈/PCB 回收纪律**：任务**下核之后**才能归还它的两张栈。
   `do_exit` 不再归还自己的栈（它还踩着内核栈跑完调度与 switch_to！），
   改在 `alloc_pcb` 真正复用某 PCB 时归还（那时 `task_on_cpu` 为假）；
   `do_kill` 只在目标是"已下核"时才立即归还。
6. **C-core 自适应调度（pick-min + 配额）**：就绪队列里有"飞控任务"时，
   挑累计前进格数最少的一架先飞；`wl_run` 配额（`WL_RUN_MAX=8`）防止
   "进度永远不变的坏上报"霸占 CPU 饿死全场。**不需要知道 CYCLE_PER_MOVE**——
   宿主机抢 CPU、时钟抖动都只会被自动补回（实测五机累计进度差 ≤5 格）。

## 调试战役

### ① 双核 + 高频 syscall 下的间歇崩溃 → 路线A + bios_lock

- **现象**：细粒度版跑高频系统调用（fly/mbox 混合）时，分钟级随机崩溃；
  PC 飞进数据段（`ra=0` → PC=0；`ra=&sched_lock`）。
- **排除**：`rdtime` trap（计数 0）；shell 轮询触发（关壳照崩）；
  sstc 路线（U-Boot 未开 menvcfg.STCE，S 态写 stimecmp 是非法指令，实测
  50201296 处炸进 U-Boot 的 `hang`，回退留注释）。
- **根因**：撤掉 BKL 后双核可同时 `ecall` 进 U-Boot M 态，U-Boot 的
  trap 处理出现并发问题（间歇性把 `csrr mhartid` 判非法 → 指令被跳过 →
  寄存器残值被当返回值 → 下游内存破坏）。
- **修复**：① 路线A 让**运行期不再有任何** `csrr mhartid`（启动期 trap
  普查：修复前每核每次 syscall 都踩，修复后只剩启动 3 次）；
  ② `call_bios` 全程持 `bios_lock`，恢复大锁版"进 M 态天然全串行"的语义。
- **证据**：trap 普查（计数 3 → 0）；长测不再出现该类崩溃。

### ② "prev 先发布、后存现场" → sched_lock 交棒

- **现象**：上述修复后，频繁调度场景仍有分钟级崩溃，签名是
  `switch_to` 的 `jr ra` 跳进垃圾（`ra=0` 或 `ra=&sched_lock`）。
- **定位**：内核 tripwire（switch 帧自洽性探针）抓到
  `pid=3 sp=50504d90 savsp=垃圾 ra=0 (h3=1 h4=1)`——某个**正在运行**的任务，
  它的 PCB `kernel_sp` 指向一段已被"活代码"（call_bios 调用链，实测
  `0x50150000`）覆写的旧帧。
- **根因**：`do_scheduler` 原来"先把 prev 挂回就绪队列、放锁、再 switch_to"，
  中间窗口里另一核 `pick_next` 挑中 prev，读到的 `kernel_sp` 还是上一轮的
  旧值 → `jr ra` 跳进数据区。锁交棒把整个 `switch_to` 圈进 `sched_lock`，
  现场一定先写完别的核才可能看见它。
- **证据**：交棒版跑 165s 重负载：0 崩溃、tripwire h3=h4=0、ps 如实双核并行；
  BIOS 流量统计（settimer 5.52M / getchar 5.05M / putc 569）确认负载形态。

### ③ 残存"双派发"（hit1=4）→ 唤醒纪律 + 栈回收纪律

- **现象**：交棒版 300s 长测中，派发探针 `hit1`（`next` 仍被另一核认领）
  仍报 4 次，并有 3 次同签名崩溃（此时 h3/h4=0：帧不再被写坏，但另一个核
  会从**旧帧**把同一个任务再派发一遍）。现场数据：
  `{pid=3, 另一核=0, 本核=1, sp=0x50504d90}`。
- **定位（代码审查，两条同族窗口）**：
  - **唤醒窗口**：任务在 `block_on_queue`/`do_block` 里"置 BLOCKED、放锁"
    与"走进自己的 do_scheduler"之间只有几步，但另一核可以在这几步里
    替它放锁并 `do_unblock` 它——它进了就绪队列而人还在核上；
  - **回收窗口**：`do_exit` 在**切走之前**就把自己的两张栈还回空闲池
    （等于把自己的立足之地提前交给下一个 exec 去踩）；`do_kill` 对
    仍在核上的目标同样立即归还。
- **修复**：`wake_pending` 静默（决策 4）+ 栈只在 PCB 复用时归还（决策 5）。
- **证据**：修复版跑 240s 重负载（producer+consumer+2×fly）：0 崩溃、
  0 双派发、ps 四任务状态如实（见验收表）。

### ④ 宿主冻结污染测量 → 改用 icount

- **现象**：对比矩阵第一次采集（真实时钟）中，消费者速率出现大段 0 值和
  8 万+/s 的尖峰；同一镜像两段时间相差 30 倍。`/proc/loadavg` 记录：
  实验期间宿主负载 0.24 → 13.68（开发机冻结问题，另一会话在联查）。
- **根因**：guest 的 `rdtime` 是**宿主墙钟**（P2 已证），速率指标 =
  "每墙钟秒消费数"——宿主一冻结，guest 每秒只执行几个循环体，速率≈0；
  冻结解除后虚钟滞后又造成虚高。
- **修复/口径**：改用 `-icount shift=3,sleep=off`：虚钟 = 指令数，
  "每秒" = 固定 125M 条指令的执行预算，**与宿主负载无关**（P2 用同一方法
  把调度偏差从 20% 压到 0.006%）。此后所有对比数据均为该口径。

### ⑤ 自伤两枚（记录现场，防止重蹈）

- **启动崩溃**：新 `get_current_cpu_id()` 读 `current_running->running_cpu`，
  但我在 `init_pcb()` **之前**的 printl 里就调了它——此时 tp 还是 U-Boot 残留
  值，直接 `Load access fault @ 50205426`。修复：启动第一条 printl 用 a0
  传进来的核号实参。
- **静默不重编**：改头文件后 `make` 不触发任何重编（Makefile 无头依赖），
  镜像 md5 不变、实验白跑。**纪律：动过头文件必须 `make clean`**。
  （顺带验证过：`FINE_LOCK` 开关切换也必须 `make clean`。）

## 验收证据（icount 口径，loadbootm `-smp 2`）

负载：`exec producer_rate &` + `exec consumer_rate &`（每秒打印消费速率），
60 个虚秒后 `exec fly &` 再跑 80 个虚秒；速率按真实经过 tick 归一。

| 镜像 | fly-off 中位速率 | fly-on 中位速率 | 保留率 | 崩溃 | ps 终态 |
|---|---|---|---|---|---|
| 细粒度 `FINE_LOCK=1`（md5 f16184…） | 253 /s | 284 /s | **112%** | 0 | shell RUNNING@core1、producer READY、consumer BLOCKED、fly RUNNING@core0，掩码全 0x3 |
| 大锁 `make`（md5 693162…） | 崩溃前中位 ~375 /s（仅 fly-off 段） | —（未跑到） | — | **1（早期崩溃）** | —（未到 ps） |

单位说明：icount 口径下"秒"= 固定指令预算（shift=6，1 虚秒 = 15.6M 指令），
速率 = 每虚秒消费次数；两镜像同口径可比，且与宿主负载无关。**判据（课件 p28）：
细粒度版开 fly 后下降不超过一半 → 实测保留 112%，通过。**

**大锁版对照的两个独立证据**：
1. 前一会话真实时钟基线：fly-off 29396/s → fly-on 786/s（保留 2.7%，坍塌）；
2. **本次 icount 轮暴露的关键发现**：大锁版在"两核指令级交替"的节奏下，
   只跑 producer+consumer（fly 还没上场）就崩了，签名
   `exception code: 1, Instruction access fault, epc 0, ra 0`——正是
   "挑到切换中的任务、从其陈旧 switch 帧 `jr ra`"家族（遗留事项 2 的那个
   窗口：`unlock_kernel` 与 `switch_to` 之间）。真实时钟下该窗口只有
   ~10 条汇编指令宽、几乎撞不上；icount 把两核交替粒度压到指令级，
   它就稳定复现了。**同一条件下细粒度版（整个 switch 在 sched_lock 内）
   全程 0 崩溃**——反证了交棒设计的价值。
   （另有一次真实时钟矩阵轮：大锁版 fly-off 中位 17726/s → fly-on 1176/s，
   保留 6.6%，但该轮宿主 loadavg 0.24→13.68，仅作方向性参考。）

**稳定性总账**：细粒度版在修复②③后累计跑过 240s 重负载（真实时钟）
+ 矩阵轮（真实时钟，0 崩溃）+ 矩阵轮（icount，0 崩溃、0 双派发）；
大锁版真实时钟下多轮 0 崩溃，icount 下暴露切换窗口崩溃（见上）。

## 遗留事项

1. **hit1 的最后一条路径未 100% 闭环**：代码审查找到的两条同族窗口都已修
   （且 240s 重负载不再复现），但"300s 长测 hit1=4"的每个现场没有逐条对号
   入座（数据只有一份快照）；探针已随验收从镜像中移除，若要继续追，可把
   `wake_pending` 探针恢复再加"派发原因"记录。
2. **大锁版残留窗口（已被 icount 实锤）**：`unlock_kernel` 与 `switch_to`
   之间还有几条汇编指令（大锁不覆盖 switch 本身），就是②类窗口。
   真实时钟下窗口只有 ~10 条指令宽、从未复现；但 icount 把两核交替粒度
   压到指令级后稳定复现（见验收表注释）。**修法与细粒度版同构**：大锁也
   交棒（switch 前不放 BKL，由接棒方继续持有），需同时改 `new_task_entry`
   与 idle 路径的持锁配对——涉及 BKL 不变量在 idle/wfi 下的边界情形，
   本次时间不够，留作后续。
3. **环境**：开发机（VirtualBox 宿主）周期性冻结 + SSH 断连问题由另一会话
   联查中；本文所有对比数据已用 icount 规避该噪声。
4. `docs/中间产物/`（11MB，用户维护的课件提取）未纳入版本库，待定夺。
