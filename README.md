# C-Core 简易内核（UCAS 操作系统研讨课 Project 3）

五个任务（交互式 shell 与进程管理、同步原语与 mailbox、双核 SMP、taskset 绑核、
细粒度锁）全部实现，并在 QEMU 上验证通过（无开发板）；单核验收在 `loadbootd`
（DASICS 用户/内核地址隔离开启）下复跑，双核验收在 `loadbootm` + `-smp 2` 下复跑。
**本文件只列验收时实际要敲的命令**；原理与设计说明见 [docs/](docs/) 与 [log/](log/)。

---

## 一、构建

```bash
cd ~/oslab-course/huangyihao24      # 仓库根目录

make dirs                           # 建 build/ 目录
make elf                            # 交叉编译 bootblock / 内核 / 18 个 app
make image                          # createimage 产出 kernel.img + apps.img，再 cat 拼成 image
```

等价的一条命令：

```bash
make all
```

**两套镜像**（任务 5 要求大锁版与细粒度版各一份）：

```bash
make clean && make                  # 大内核锁版（默认 FINE_LOCK=0）
make clean && make FINE_LOCK=1      # 细粒度锁版（定义 FINE_GRAIN_LOCK）
```

> **切开关前必须 `make clean`**：Makefile 只检查源文件时间，不检查编译选项；
> 不清的话旧 `.o` 会被直接复用——你以为编了细粒度版，跑的还是大锁版。

**构建成功标志**（`make image` 末尾会打印字节级布局核算；数字随源码改动小幅变化，
核对要点是各项自洽、没有 error）：

```
[layout] kernel.img : ... bytes = 512(boot) + ...(kernel) + ...(appinfo)
[layout] apps.img   : ... bytes = ...(apps) + ...(尾部对齐)
[layout] appinfo 偏移 : ... (byte)
[layout] apps 区偏移  : ... (byte) —— 内核字节数 ...，二者【不补到扇区】
[layout] total      : ... bytes = ... sectors（末尾补 ... 字节）
```

> 每个 app 的加载窗口按**名字**（排序后的序号）算，与“本次构建了哪些目标”无关，
> 所以改完某个 app 的源码直接 `make all` 即可，不会和别的 app 撞地址。

**app 名单（18 个）**：`shell`、`fly`，以及 `test/test_project3/` 下的
`add`、`affinity`、`barrier`、`condition`、`consumer`、`consumer_rate`、
`mbox_client`、`mbox_server`、`multicore`、`producer`、`producer_rate`、
`ready_to_exit`、`test_affinity`、`test_barrier`、`wait_locks`、`waitpid`。

---

## 二、在 QEMU 上运行验证

### 单核（任务 1、任务 2 的基础验收）

```bash
make run
```

等 U-Boot 自检 + autoboot 走完（出现 `=> ` 提示符），输入：

```
loadbootd          ← 开启 DASICS 地址隔离（单核）
```

### 双核（任务 3、4、5）

```bash
make run-smp
```

等 `=> ` 提示符，输入：

```
loadbootm          ← 双核必须用它（loadbootd/loadboot 只起单核）
```

### 启动后

内核初始化完会**直接启动 shell**（PID 1，不再有“选任务”菜单），屏幕出现：

```
------------------- COMMAND -------------------
> root@UCAS_OS:
```

退出 QEMU：`Ctrl+a`，然后按 `x`。

---

## 三、shell 命令

| 命令 | 作用 |
|---|---|
| `ps` | 列出进程表：`[i] PID : n  STATUS : X mask: 0xN [Running on core N]` |
| `clear` | 清屏 |
| `exec <name> [args...] [&]` | 启动任务；不带 `&` 时 shell 用 `waitpid` 等它结束，带 `&` 则后台运行 |
| `kill <pid>` | 终止指定进程；若它持有锁，内核替它释放并唤醒等待者 |
| `taskset <mask> <name> [args...] [&]` | 按核掩码启动任务（`0x1`=核 0，`0x2`=核 1，`0x3`=两核都行） |
| `taskset -p <mask> <pid>` | 修改已存在进程的核掩码 |
| `help` | 打印命令提示 |
| 其它 | `Unknown command: xxx` |

shell 支持退格（BS/DEL）删除输入；输入区固定在屏幕下半部分（`SHELL_BEGIN=20`），
上半部分留给任务输出。

---

## 四、各任务验收步骤（一步一步）

> **通用约定**
> - 任务 1、2 用**单核**：`make run` → `loadbootd`；任务 3、4、5 用**双核**：`make run-smp` → `loadbootm`。
> - 下面代码块里的行，都是在 shell 提示符 `> root@UCAS_OS:` 后**逐行输入**（提示符本身不用输入）。
> - `<pid>`、`<mask>` 是占位符，按屏幕上的实际数字替换。
> - 退出 QEMU 统一是：先按 `Ctrl+a`，松开后再按 `x`。
> - 每个任务都从 `make clean && make` 开始；同一镜像内重跑可以跳过构建，直接 `make run`。

---

### 4.1 任务 1：交互式 shell 与进程管理（单核）

**① 构建并启动**

```bash
make clean && make
make run
```

U-Boot 出现 `=> ` 提示符后输入：

```
loadbootd
```

等内核初始化完成，屏幕出现：

```
------------------- COMMAND -------------------
> root@UCAS_OS:
```

**② 基本命令**

输入：

```
ps
```

期望：打印 `[Process Table]:`，下面只有 shell 一行（最终版**不列 idle**，也没有进程名列）：

```
[0] PID : 1  STATUS : RUNNING mask: 0x3 Running on core 0
```

输入：

```
clear
```

期望：清屏，提示符回到屏幕下半部分（第 20 行）。

输入 `abc`，按两次 Backspace，再输入 `d`，然后回车。操作序列写成：

```
abc<BS><BS>d
```

（`<BS>` 表示按一次退格键，不要真的输入这几个字符。）

期望：提示符后先回显 `abc`，两次退格后变成 `a`，再输入 `d` 显示 `ad`；回车后打印：

```
Unknown command: ad
```

输入一个不存在的命令：

```
xyzzy
```

期望：

```
Unknown command: xyzzy
```

**③ 后台 `exec` + `waitpid`**

输入：

```
exec waitpid &
```

期望：
- shell 打印 `Info: execute waitpid successfully, pid = N ...`，随后立刻回到提示符（`&` 表示后台运行）；
- 屏幕第 0 行：`> [TASK] I want to wait task (pid=...) to exit.`；
- 屏幕第 1 行：`ready_to_exit` 连续打印 `> [TASK] I am task with pid X, I have acquired two mutex locks. (i)`，`i` 从 0 涨到 9999；
- 屏幕第 2 行：`wait_locks` 打印 `> [TASK] I want to acquire mutex lock1 ...` 后停住（两把锁都在 `ready_to_exit` 手里）；
- `ready_to_exit` 跑满 10000 轮后释放两把锁并退出；`wait_locks` 随即打印 `I have acquired mutex lock1` / `lock2` 并退出；
- `waitpid` 任务依次打印两条 `> [TASK] Task (pid=...) has exited.`，然后自己结束。

**④ 前台 `exec` + `waitpid`**

输入：

```
exec waitpid
```

期望：shell 停在 `sys_waitpid` 里等这个测试跑完，期间**不打印下一条提示符**；测试结束后提示符才回来。这就是前台执行的语义（与 ③ 的 `&` 对比）。

**⑤ `kill` 一个持有锁的进程**

输入：

```
exec waitpid &
```

看屏幕第 1 行，记下 `ready_to_exit` 打印的 pid `X`（`ps` 不显示进程名，所以 pid 从它自己的打印里读）。趁它还在打印（它有 10000 轮，窗口足够），输入：

```
ps
```

确认列表里有这个 pid，然后：

```
kill X
```

期望：`kill` 成功时**不打印任何东西**；如果 pid 不存在，会打印 `Error: cannot find process X`。

```
ps
```

期望：该进程状态变成 `EXITED`（或已被新进程复用）；同时因为内核替它释放了两把锁，`wait_locks` 会打印 `I have acquired mutex lock1` 并继续，`waitpid` 任务也会被唤醒。若第 1 行已经不再刷新（测试已自己跑完），重新 `exec waitpid &` 再来一次。

**⑥ 退出**：`Ctrl+a` → `x`。

---

### 4.2 任务 2：同步原语（单核）

三个子场景**各自独立启动一次 QEMU**；每个子场景结束后直接 `Ctrl+a` → `x` 退出即可（condition / mailbox 的测试是死循环，不用 kill，退出 QEMU 就把它们一起结束了）。

**公共启动步骤**（三个子场景都先做这一步）：

```bash
make clean && make
make run
```

等 `=> ` 后输入：

```
loadbootd
```

#### 2A 屏障 barrier

输入：

```
exec barrier &
```

期望：屏幕第 0、1、2 行各有一个 `test_barrier`，交替打印：

```
> [TASK] Ready to enter the barrier.(i)
> [TASK] Exited barrier (i).(sleep N s)
```

三个任务的 `(i)` **严格并排增长**（0→9）：任何一个都不会在自己的 `i` 还没到 10 时独自进入下一轮——这就是屏障语义。10 轮后 3 个 `test_barrier` 一起结束，`barrier` 主任务 `waitpid` 完自动退出。

验收完：`Ctrl+a` → `x`。

#### 2B 条件变量 condition

重新启动（`make run` → `loadbootd`），输入：

```
exec condition &
```

期望：1 个 producer + 3 个 consumer 并发运行：

- 第 0 行：producer 打印 `> [TASK] Total produced 3 products. (next in N seconds)`，每轮 +3，直到 `Total produced 30 products.`，然后 EXITED；
- 第 1~3 行：3 个 consumer 打印 `> [TASK] Total consumed M products. (Sleep N seconds)`；
- producer 结束后，3 个 consumer 的 `M` 之和 = **30**（生产多少消费多少，不超卖、不为负）；之后 consumer 全部阻塞在条件变量上。

可以输入 `ps` 辅助确认：

```
ps
```

期望：producer 为 `EXITED`，3 个 consumer 为 `BLOCKED`，`condition` 父任务为 `BLOCKED`（它在 `waitpid` 等 4 个子进程）。

验收完：`Ctrl+a` → `x`（不用 kill）。

#### 2C 邮箱 mailbox

重新启动（`make run` → `loadbootd`），**先启动 server，再启动两个 client**：

```
exec mbox_server &
```

等它打印 `[Server] server started`，再输入：

```
exec mbox_client &
exec mbox_client &
```

期望：

- server 打印 `[Server]: recved msg from N (blocked: B, correctBytes: C, errorBytes: E)`；
- `E` 恒为 **0**，`C` 持续增长；
- 两个 client 各自打印 `[Client] send bytes: N, blocked: M`，`N` 持续增长；
- server 每秒只处理一条消息，client 发得更快时 `blocked` 会增长——这正是"信箱满 → 发送者阻塞"的预期行为；`correctBytes` 与两个 client 的累计 `send bytes` 只差信箱里尚未取走的有限缓冲，不会越差越多。

验收完：`Ctrl+a` → `x`（不用 kill）。

---

### 4.3 任务 3：双核 SMP（双核）

**① 构建并启动**

```bash
make clean && make
make run-smp
```

等 `=> ` 后输入：

```
loadbootm
```

**② 确认两个核都进入内核**

在宿主另一个终端查看 QEMU 日志（路径见 Makefile 的 `QEMU_LOG_FILE`，默认 `$HOME/OSLab-RISC-V/oslab-log.txt`）：

```bash
grep -a "\[SMP\]" "$HOME/OSLab-RISC-V/oslab-log.txt"
```

期望两行都在：

```
[SMP] hart 0 entered kernel main
[SMP] hart 1 idle ready, entering scheduler loop
```

（`grep -a` 的 `-a` 不能省：日志里带 `\r`，普通 grep 可能把它当二进制文件。）

**③ 跑 multicore 测试**

```
exec multicore &
```

期望：先打印 `start test multi-core performance`，然后分两阶段打印：

```
single core: <ticks1> ticks, result = <r>
multi core:  <ticks2> ticks, result = <r>
```

同一次运行中 `ticks2` 应明显小于 `ticks1`（任务书参考图 P3-7 约 1.8×；实际倍数随宿主/QEMU 波动，看方向即可）。测试最后自己 `sys_exit`。

**④ 退出**：`Ctrl+a` → `x`。

---

### 4.4 任务 4：taskset 绑核（双核）

**① 构建并启动**

```bash
make clean && make
make run-smp
```

等 `=> ` 后输入：

```
loadbootm
```

**② 用 mask 0x1 启动 affinity**

```
taskset 0x1 affinity &
```

期望：shell 打印 `taskset 0x1 affinity.`；`affinity` 在第 1 行打印：

```
start test cpu affinity, pids = {N1, N2, N3, N4, N5}
```

5 个 `test_affinity` 在屏幕第 2~6 行各自打印：

```
[pid] integer test (i/150) access: X auipc: <同一个十六进制地址>
```

5 个 `auipc` 值相同——它们共享同一份代码映像（只是各自的 PCB/栈不同）。

**③ 看掩码继承**

```
ps
```

期望：`affinity` 和 5 个 `test_affinity` 的 `mask` 全是 `0x1`；其中正在运行的显示 `Running on core 0`，其余为 `READY`。

**④ 动态改绑一个子任务**

从第 1 行的 `pids = {N1, ...}` 里选一个，例如 `N1`：

```
taskset -p 0x2 N1
```

期望：shell 打印 `taskset -p 0x2 N1.`。改绑不打断当前时间片，等它下一次被调度后才会搬到核 1；多试几次：

```
ps
```

期望：`N1` 的 `mask` 变成 `0x2`，并在状态为 `RUNNING` 时显示 `Running on core 1`；它的 `integer test (i/150)` 进度会明显快于其他 4 个（核 1 上只有它一个，核 0 上还有另外 4 个分时间片）。

**⑤ 等待结束**：5 个子任务各跑满 150 轮后自己结束，`affinity` 主任务 `waitpid` 完自动退出。

**⑥ 退出**：`Ctrl+a` → `x`。

---

### 4.5 任务 5：细粒度锁对比（双核，两套镜像）

任务是"同一份源码、两个编译开关"，要分别构建、分别运行、记录同一个指标。

**① 大锁版（对照）**

```bash
make clean && make
make run-smp
```

等 `=> ` 后输入 `loadbootm`。在 shell 里：

```
exec producer_rate &
exec consumer_rate &
```

期望：`consumer_rate` 在屏幕第 1 行每秒打印一次：

```
[consumer] consume rate: N /s
```

等速率稳定（连续十几次数值接近，约 1 分钟），记下这个 **fly-off** 中位数。

```
exec fly &
```

期望：屏幕第 10~16 行出现一架左右飞的 ASCII 飞机。再等 1~2 分钟，记下稳定的 **fly-on** 中位数。

```
ps
```

期望：`producer_rate`、`consumer_rate`、`fly` 都在列表里，状态如实（`RUNNING` / `READY` 按调度时刻不同）。

记录完：`Ctrl+a` → `x`（不用 kill）。

**② 细粒度版**

```bash
make clean && make FINE_LOCK=1
make run-smp
```

同样 `loadbootm`，重复：

```
exec producer_rate &
exec consumer_rate &
# 等稳定，记 fly-off
exec fly &
# 等稳定，记 fly-on
ps
```

**③ 判据**

- 细粒度版：`fly-on / fly-off ≥ 50%`（开 fly 后消费速率下降不超过一半，任务书 §5.6）。
- 大锁版：开 fly 后通常断崖式下降（实测方向：保留率只有个位数百分比）。
- 两套镜像必须都 `make clean` 后再构建——Makefile 不跟踪 CFLAGS，不 clean 会拿旧 `.o` 拼出新镜像（见 §一）。

**④ 退出**：`Ctrl+a` → `x`。

---

## 五、任务 5 的判据与记录口径（补充）

- 指标只看**同一镜像内**的 `fly-on / fly-off` 比值：`make run-smp` 用真实时钟，
  宿主负载会同时影响两个绝对值，但不影响"同一镜像、同一台机器"的比值方向。
- `fly-off` 取 `exec fly &` 之前连续 60 秒的中位数；`fly-on` 取开 fly 后
  60~120 秒的中位数；两者都按 `consumer_rate` 每秒打印的数值记。
- 细粒度版判据：保留率 ≥ 50%；大锁版作为对照，预期明显更低。

---

## 六、上板（有开发板时）

```bash
make all                            # 构建（与 QEMU 完全相同）
make floppy                         # sudo dd if=build/image of=/dev/sdb3 conv=notrunc
make minicom                        # sudo minicom -D /dev/ttyUSB1
```

在 minicom 里：单核输入 `loadboot`，双核输入 `loadbootm`（任务书 5.1）。

---

## 附一：关键地址与镜像字段（速查）

| 项 | 值 |
|---|---|
| bootblock 加载地址 | `0x50200000` |
| 内核入口 | `0x50201000` |
| 内核栈顶（main / pid0 空转用） | `0x50500000` |
| BIOS 跳转表 | `0x51ffff00` |
| app 窗口 | `0x52000000 + (按名字排序的序号)*0x10000` |
| 每个任务的内核栈 / 用户栈 | 各 4K，由 `mm.c` 分配（内核侧 `0x50501000` 起、用户侧 `0x52500000` 起） |
| 从核启动栈 / 陷阱栈 | `0x50301000` / `0x50302000`（`include/os/smp.h`） |
| 从核唤醒标志 | `0x50300000`（主核写 1，从核在 bootblock 自旋读） |
| 核亲和掩码 | `CPU_MASK_ALL = 0x3`（bit0=核 0，bit1=核 1） |
| 镜像信息字段 | `0x1f0`=AppInfo 字节偏移、`0x1f4`=apps 区字节偏移、`0x1f8`=app 数、`0x1fc`=内核扇区数、`0x1fe`=`55 aa` |
| 镜像内部布局 | **字节级紧密**（不补扇区）：bootblock 512B → 内核 → AppInfo → apps 首尾相接；仅 bootblock 补到 512 |
| 系统调用约定 | 调用号放 `a7`、参数放 `a0~a4`、返回值写回 `a0`；号表在内核 `arch/riscv/include/asm/unistd.h` 与用户 `tiny_libc/include/syscall.h` **两处保持同值** |
| 任务 4 系统调用号 | `SYSCALL_TASKSET = 57`（任务书未给号，自定；避让官方号段） |
| 时钟与时间片 | `TIMER_INTERVAL = 150000` ticks 为默认节拍；P3 时间片为 1/4 节拍（`SLICE_LEN`），C-core 自适应调度下绝不为 0 |
| 细粒度锁开关 | `make FINE_LOCK=1`（定义 `FINE_GRAIN_LOCK`，一份源码编出两版） |

## 附二：文档索引

- P3 任务书、课件：`docs/Project3/`
- P3 五个任务的工作日志（设计决策 / 调试战役 / 验收证据）：`log/P3-task1..5-工作日志.md`
- P2 答辩口径与汇总报告：`docs/Project2/`
- P1 教学文档 11 件套 + 设计评审 + 代码演进史：`docs/Project1/`
- 各任务日志与工作流程：`log/`

## 附三：分支

全部工作在 `Project3` 分支：`p3-start`（合并 upstream/Project3 起点代码）之后，
每个任务一个中文 commit（见 `git log`）。
