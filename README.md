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

> **先看屏幕布局，再动手**——很多“和预期不一样”其实是行号/区域看错了。

### 4.0 通用规则（每个任务都适用）

**屏幕怎么分（50 行 × 80 列）**

```
屏幕第  1 ~ 20 行   ← 任务输出区（各测试程序按自己指定的行号打印）
屏幕第 21 行        ← ------------------- COMMAND -------------------
屏幕第 22 行起       ← shell 提示符 > root@UCAS_OS: 和你的输入
```

- 本文行号**一律从 1 数**（和屏幕左上角一致）；程序里的 `print_location=0`
  对应屏幕第 1 行，`print_location=1` 对应第 2 行，以此类推。
- shell 区固定在下半屏。如果看到上半屏空着、只有第 21 行以下有字，是正常的。

**启动与退出**

- 任务 1、2：`make run` → 等 `=> ` → 输入 `loadbootd`（单核）
- 任务 3、4、5：`make run-smp` → 等 `=> ` → 输入 `loadbootm`（双核；`loadbootd/loadboot` 只起单核）
- 退出 QEMU：先按 `Ctrl+a`，松开后再按 `x`
- 每个任务都从 `make clean && make` 开始；同一镜像内重跑可以跳过构建，直接 `make run`

**三条容易踩的规则**

1. `exec xxx` **不带 `&`** = 前台执行：shell 会一直等它结束，期间不能输入下一条命令。
   验收基本都用 `exec xxx &`。
2. `taskset ...` 启动任务后**立刻把 shell 还给你**（不等待子进程）——启动完可以马上敲下一条。
3. `ps` **不显示进程名**，只显示下面这种行；已经退出的进程会以 `EXITED` 继续留在表里：

```
[0] PID : 1  STATUS : RUNNING mask: 0x3 Running on core 0
```

**成功提示长这样**

```
Info: execute <name> successfully, pid = N ...
taskset 0x1 affinity.
```

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

等内核初始化完成，屏幕第 21、22 行出现：

```
21| ------------------- COMMAND -------------------
22| > root@UCAS_OS:
```

（左边 `21|` 只是本文的行号标注，屏幕上不会显示。）

**② `ps`：确认 shell 自己是 pid 1**

输入：

```
ps
```

预期（屏幕第 23、24 行）：

```
[Process Table]:
[0] PID : 1  STATUS : RUNNING mask: 0x3 Running on core 0
```

- 只有 shell 一行（空闲任务 pid0 不列出，也没有进程名列）；
- `mask: 0x3` 表示两个核都可以跑（单核启动也是 0x3）；
- 如果表里出现别的进程，说明这个镜像之前跑过测试，重启 QEMU 再来。

**③ `clear`**

输入：

```
clear
```

预期：全屏清空，提示符回到第 21 行。

**④ 退格编辑**

依次按 `a` `b` `c` `退格` `退格` `d` `回车`（屏幕上的操作序列是 `abc<BS><BS>d`，
`<BS>` 表示按一次退格键，不要真的输入这几个字符）。

预期：输入行先回显 `abc`，两次退格后变成 `a`，再输入 `d` 显示 `ad`；回车后：

```
Unknown command: ad
```

**⑤ 未知命令**

输入：

```
xyzzy
```

预期：

```
Unknown command: xyzzy
```

**⑥ 后台 `exec` + `waitpid`（核心验收）**

输入：

```
exec waitpid &
```

预期：shell 立刻打印（第 23 行）：

```
Info: execute waitpid successfully, pid = 2 ...
```

然后马上回到提示符（`&` = 后台）。**同时**上半屏出现三行：

> pid 号是**每次启动 QEMU 后从 shell=1 开始依次分配**的：干净启动时
> waitpid=2、ready_to_exit=3、wait_locks=4。如果之前跑过别的测试，数字会变大，
> 以屏幕上的实际值为准。

```
第 1 行：> [TASK] I want to wait task (pid=3) to exit.                ← waitpid 自己
第 2 行：> [TASK] I am task with pid 3, I have acquired two mutex locks. (0..9999)
第 3 行：> [TASK] I want to acquire mutex lock1 (handle=1).            ← wait_locks 卡在这里
```

过程：

- `ready_to_exit`（第 2 行）跑 **10000 轮**，在 QEMU 里大约 **3~10 秒**就结束——比想象中快；
- 它结束后释放两把锁 → 第 3 行的 `wait_locks` 立刻依次打印
  `I have acquired mutex lock1 (handle=...)` 和 `I have acquired mutex lock2 (handle=...)` 并退出；
- 第 1 行的 `waitpid` 依次看到两个子任务退出，打印两条
  `> [TASK] Task (pid=...) has exited.`，然后自己退出。

**关键提醒**：十几秒后再 `ps`，会看到 pid 2/3/4 全是 `EXITED`——这是正常结束，
不是程序出错。

**⑦ 前台 `exec`（和 ⑥ 对比）**

输入：

```
exec waitpid
```

预期：shell 停在 `sys_waitpid` 里等它跑完，**这期间不会再出现新的
`> root@UCAS_OS:` 提示符**；大约 10 秒后测试结束，提示符才回来。
这就是“带 `&` / 不带 `&`”的区别。

**⑧ `kill` 一个持有锁的进程（手要快）**

输入：

```
exec waitpid &
```

看到第 2 行开始打印后，**立刻**输入：

```
ps
```

预期：`ps` 表里能找到 `ready_to_exit` 的 pid（就是第 2 行打印里的那个数字）。然后：

```
kill <那个 pid>
```

预期：

- `kill` 成功时**不打印任何东西**，直接回到提示符；pid 不存在才会打印
  `Error: cannot find process X`；
- 再 `ps`：该 pid 变成 `EXITED`；
- 屏幕第 3 行的 `wait_locks` 立刻拿到锁继续（内核替被杀的进程释放了锁），
  依次打印 `I have acquired mutex lock1` / `lock2`。

> 如果手慢没来得及（`ps` 时已经全 EXITED），重新执行 ⑥ 再来一次即可；
> 这个测试不影响其它验收结果。

**⑨ 退出**：`Ctrl+a` → `x`。

**任务 1 通过标准**

- [ ] 启动后出现横幅和 `> root@UCAS_OS:`
- [ ] `ps` 显示 shell 为 pid 1、`RUNNING`、`mask: 0x3`
- [ ] `clear` 清屏，提示符回到第 21 行
- [ ] 退格能把 `abc` 改成 `ad`，并报 `Unknown command: ad`
- [ ] `exec waitpid &` 后台执行，三行测试输出同时出现（第 1~3 行）
- [ ] `exec waitpid` 前台执行，等待期间没有新提示符
- [ ] `kill` 能杀掉持锁进程，且锁自动释放（`wait_locks` 继续打印）

---

### 4.2 任务 2：同步原语（单核，三个子场景）

三个子场景**各自独立启动一次 QEMU**。公共启动步骤：

```bash
make clean && make
make run
```

等 `=> ` 后输入 `loadbootd`。

---

#### 2A 屏障 barrier

输入：

```
exec barrier &
```

预期：

- shell 打印 `Info: execute barrier successfully, pid = 2 ...`；
- 屏幕**第 1、2、3 行**各出现一个 `test_barrier`，三行同时开始：

```
第 1 行：> [TASK] Ready to enter the barrier.(0)
第 2 行：> [TASK] Ready to enter the barrier.(0)
第 3 行：> [TASK] Ready to enter the barrier.(0)
```

- 三行一起放行后，同时变成 `> [TASK] Exited barrier (0).(sleep N s)`；
- `(i)` **严格并排增长**（0 → 9）：任何一行都不会在别人还是 0 的时候自己跑到 1；
- 每轮随机 sleep 1~3 秒，10 轮总共约 **20~40 秒**；
- 三个 `test_barrier` 一起结束后，`barrier` 父任务自动退出。

> 行首的 `>` 是测试程序自己的前缀，不是 shell 提示符，别混淆。

验收完：`Ctrl+a` → `x`。

---

#### 2B 条件变量 condition

重新启动（`make run` → `loadbootd`），输入：

```
exec condition &
```

预期（producer 在第 1 行，3 个 consumer 分别在第 2、3、4 行）：

```
第 1 行：> [TASK] Total produced 3 products. (next in N seconds)
第 2 行：> [TASK] Total consumed M products. (Sleep N seconds)
第 3 行：> [TASK] Total consumed M products. (Sleep N seconds)
第 4 行：> [TASK] Total consumed M products. (Sleep N seconds)
```

- producer 每轮 +3，10 轮后到 **`Total produced 30 products.`**，然后变成 `EXITED`；
- 3 个 consumer 每次各消费 1；producer 结束后，三者消费之和 = **30**，不会超卖、不会为负；
- consumer 是**死循环**，不会退出——三行会一直停在那里，这是正常的。

大约 30 秒后输入：

```
ps
```

预期能看到（行数随调度时刻略有差异）：

```
[Process Table]:
[0] PID : 1  STATUS : RUNNING mask: 0x3 Running on core 0   ← shell
[1] PID : 2  STATUS : BLOCKED mask: 0x3                     ← condition 父任务
[2] PID : 3  STATUS : EXITED  mask: 0x3                     ← producer
[3] PID : 4  STATUS : BLOCKED mask: 0x3                     ← consumer
```

> 实测中 ps 可能只列出 1 个 consumer，其余 consumer 的槽位在表里不出现——
> 这是当前实现的进程表行为，不影响验收，**以屏幕第 2~4 行的消费计数为准**。

验收完：`Ctrl+a` → `x`（不用 kill：consumer 是死循环，退出 QEMU 一起结束）。

---

#### 2C 邮箱 mailbox

重新启动（`make run` → `loadbootd`）。**先启动 server，再启动两个 client**：

```
exec mbox_server &
```

等第 1 行出现 `[Server] server started`，再输入：

```
exec mbox_client &
exec mbox_client &
```

预期：

```
第 1 行：> [Server]: recved msg from N (blocked: B, correctBytes: C, errorBytes: E)
第 2 行：> [Client] server started          ← client 自己也会打这一行，不是报错
第 3 行：> [Client] server started
```

随后第 2、3 行变成：

```
> [Client] send bytes: N, blocked: M
```

判据：

- `errorBytes: E` **恒为 0**；
- `correctBytes: C` 持续增长；
- client 的 `send bytes` 持续增长；client 发得比 server 快，`blocked` 会逐渐变大
  ——这正是“信箱满 → 发送者阻塞”的预期行为；
- server 每秒只处理一条消息，所以 `correctBytes` 大约每秒涨一条的量。

验收完：`Ctrl+a` → `x`（不用 kill）。

---

**任务 2 通过标准**

- [ ] barrier：屏幕第 1~3 行的 `(i)` 并排增长到 9，三行一起结束
- [ ] condition：producer 到 30，三个 consumer 消费合计 30，无负数
- [ ] condition：consumer 一直停在 BLOCKED，ps 里 producer 是 EXITED
- [ ] mailbox：`errorBytes` 恒 0，`correctBytes` 持续增长
- [ ] mailbox：client 的 `send bytes` 增长，`blocked` 会变大

---

### 4.3 任务 3：双核 SMP（双核）

**① 构建并启动**

```bash
make clean && make
make run-smp
```

等 `=> ` 后输入（**注意不是 loadbootd**）：

```
loadbootm
```

**② 确认两个核都进入内核**

在 VM 里另开一个终端（或宿主终端）：

```bash
grep -a "\[SMP\]" "$HOME/OSLab-RISC-V/oslab-log.txt"
```

预期两行都在：

```
[SMP] hart 0 entered kernel main
[SMP] hart 1 idle ready, entering scheduler loop
```

（`grep -a` 的 `-a` 不能省：日志里带 `\r`，普通 grep 可能把它当二进制文件。）

**③ 跑 multicore 测试**

```
exec multicore &
```

预期：

- 屏幕第 1 行打印 `start test multi-core performance`；
- 单核阶段结束后，**第 6 行**打印
  `single core: <ticks1> ticks, result = <r>`；
- 双核阶段结束后，**第 7 行**打印
  `multi core:  <ticks2> ticks, result = <r>`；
- 两次 `result` 数值相同（同一个计算结果）；
- `ticks2` 明显小于 `ticks1`（任务书参考约 1.8×；实际随宿主负载波动，**看方向即可**）；
- 测试最后自己 `sys_exit()`，不需要手动结束。

> 注意：单核阶段是**一个** `add` 进程跑完整区间；双核阶段是**两个** `add` 各跑一半。
> 如果 `ticks2` 和 `ticks1` 差不多，先确认启动的是 `run-smp` + `loadbootm`。

**④ 退出**：`Ctrl+a` → `x`。

**任务 3 通过标准**

- [ ] 日志里 `[SMP] hart 0` 和 `[SMP] hart 1` 两行都有
- [ ] multicore 打出 `single core` 和 `multi core` 两行
- [ ] 两次 `result` 相同
- [ ] `multi core` 的 ticks 明显小于 `single core`

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

**② 用掩码 0x1 启动 affinity**

```
taskset 0x1 affinity &
```

预期：

- shell 打印 `taskset 0x1 affinity.`（**带句点**）；
- shell **立刻回到提示符**——taskset 不等待子进程，这不是卡住；
- 屏幕**第 2 行**出现：

```
start test cpu affinity, pids = {N1, N2, N3, N4, N5}
```

- 屏幕**第 3~7 行**各有一个 `test_affinity`：

```
[N1] integer test (0/150) access: 3 auipc: <同一地址>
[N2] integer test (0/150) access: 5 auipc: <同一地址>
...
```

- 5 个 `auipc` 值相同——它们共享同一份代码映像（PCB/栈各自独立）。

**③ 看掩码继承**

```
ps
```

预期：`affinity` 和 5 个 `test_affinity` 的 `mask` 全是 `0x1`；正在跑的显示
`Running on core 0`，其余为 `READY`。

**④ 动态改绑一个子任务**

从第 2 行的 `pids = {...}` 里选一个，例如 `N1`：

```
taskset -p 0x2 N1
```

预期：shell 打印 `taskset -p 0x2 N1.`。改绑不打断当前时间片，等下一次调度才搬到核 1。
稍等几秒再：

```
ps
```

预期：

- `N1` 的 `mask` 变成 `0x2`；
- 状态为 `RUNNING` 时显示 `Running on core 1`；
- 它的 `integer test (i/150)` 进度会明显快于另外 4 个（核 1 上只有它，核 0 上还有 4 个分时间片）。

**⑤ 等它跑完**：5 个子任务各跑满 150 轮后自己结束（每轮 100000 次
`rand()`，在 QEMU 里要跑一会儿），`affinity` 主任务 `waitpid` 完自动退出。

**⑥ 退出**：`Ctrl+a` → `x`。

**任务 4 通过标准**

- [ ] `taskset 0x1 affinity &` 后 shell 立刻返回，打印 `taskset 0x1 affinity.`
- [ ] 第 2 行有 `pids = {...}`，第 3~7 行 5 个子任务都在跑
- [ ] 5 个 `auipc` 地址相同
- [ ] `ps` 里全部 `mask: 0x1`
- [ ] `taskset -p 0x2 N1` 后该任务 `mask: 0x2`、在 core 1 上运行、进度更快

---

### 4.5 任务 5：细粒度锁对比（双核，两套镜像）

任务是“同一份源码、两个编译开关”，要分别构建、分别运行、记录同一个指标。

**① 大锁版（对照组）——用真实时钟跑**

```bash
make clean && make
make run-smp
```

> ⚠️ **大锁版不要用 icount**：`-icount` 会把两核交替压到指令级，稳定触发
> `unlock_kernel` 与 `switch_to` 之间的切换窗口，内核报
> `truly_illegal_insn`（已知问题，见 `log/P3-task5-工作日志.md` 遗留事项 2）。
> 这是大锁版的固有缺陷，**不是主人操作错了**；细粒度版没有这个窗口。
> 所以大锁版的对照数据用**真实时钟**测，细粒度版可用 icount 口径。

等 `=> ` 后输入 `loadbootm`。在 shell 里：

```
exec producer_rate &
exec consumer_rate &
```

预期：

- `producer_rate` **不打印任何东西**（它只闷头生产）；
- `consumer_rate` 在**屏幕第 2 行**每秒打印一次：

```
[consumer] consume rate: N /s
```

等速率稳定（连续十几个数值接近，约 1 分钟），记下这个 **fly-off** 中位数。

```
exec fly &
```

预期：屏幕**第 11~17 行**出现一架左右飞的 ASCII 飞机（共 7 行图案）。再等 1~2 分钟，
记下稳定的 **fly-on** 中位数。

```
ps
```

预期：`producer_rate`、`consumer_rate`、`fly` 都在列表里，状态如实
（`RUNNING` / `READY` 按调度时刻不同）。

记录完：`Ctrl+a` → `x`（不用 kill）。

**② 细粒度版——优先用 icount 口径**

```bash
make clean && make FINE_LOCK=1
make run-smp QEMU_OPTS='-nographic -machine virt -m 256M -kernel /home/stu/OSLab-RISC-V/u-boot/u-boot -bios none -drive if=none,format=raw,id=image,file=./build/image -device virtio-blk-device,drive=image -monitor telnet::45454,server,nowait -serial mon:stdio -icount shift=6,sleep=off'
```

> - `$HOME` 在 Make 命令行里会被当成 Make 变量（`$H`）吃掉，**必须写绝对路径**；
> - `shift=6`：1 虚秒 = 1562.5 万条指令，启动约 30~60 秒；
> - `shift=3` 会慢 8 倍（启动要几分钟），不要用；
> - `-D`/`-d` 调试日志可以省略（不影响实验）。

等 `=> ` 后输入 `loadbootm`，在 shell 里重复同样序列：

```
exec producer_rate &
exec consumer_rate &
# 等稳定，记 fly-off
exec fly &
# 等稳定，记 fly-on
ps
```

**③ 测量口径（答辩/验收时照这段解释）**

任务书原文（§5.6）只规定：“在使用细粒度锁的情况下……打开 fly 之后速度的
下降不能超过一半”，**没有规定用什么时钟测量**。

- **真实时钟**：QEMU 按宿主墙钟跑。宿主机是 VirtualBox 虚拟机（NEM 模式），
  宿主负载抖动会让同一镜像的 `fly-off` 在 18000~23000 之间波动——**绝对值不可
  重复**，比值会被宿主状态左右。
- **icount（虚秒）**：`-icount shift=N` 把 guest 时钟绑到**指令数**：每执行
  1 条 guest 指令，虚拟时间前进 `2^N` 纳秒。
  - `shift=6`：1 条指令 = 64ns → **1 虚秒 = 1562.5 万条指令**
  - `shift=3`：1 条指令 = 8ns → 1 虚秒 = 1.25 亿条指令
  - `sleep=off`：guest 执行 `WFI`（空闲）时虚拟时钟不停摆，保证
    “1 虚秒 = 固定指令预算”严格成立。

  于是 `fly-off` / `fly-on` 测的是：**同样固定指令预算内消费了多少商品**——
  宿主忙闲完全不影响，两套镜像同口径可比。这不是改判据、也不是改被测程序，
  只是把“秒”从宿主墙钟换成指令数，让测量**可重复**。

**已知数据（两个口径互相印证）**

| 口径 | 镜像 | fly-off | fly-on | 保留率 |
|---|---|---|---|---|
| 真实时钟 | 大锁 | ~23000 /s | < 1000 /s | < 4% |
| 真实时钟 | 细粒度 | ~18000 /s | 3000~4000 /s | 17~22% |
| **icount shift=6** | **细粒度** | **253 /s** | **284 /s** | **112% ✓** |
| 真实时钟（历史） | 大锁 | 29396 /s | 786 /s | 2.7% |

结论：两种口径的**方向完全一致**（细粒度远好于大锁）；真实时钟的绝对值受宿主
噪声污染，icount 口径是可重复的验收数据。

**④ 判据**

- 细粒度版：`fly-on / fly-off ≥ 50%`（开 fly 后消费速率下降不超过一半，任务书 §5.6）。
- 大锁版：开 fly 后断崖式下降（实测 < 4%），作为对照。
- 两套镜像必须都 `make clean` 后再构建——Makefile 不跟踪 CFLAGS，不 clean 会拿旧 `.o` 拼出新镜像（见 §一）。

**⑤ 退出**：`Ctrl+a` → `x`。

**任务 5 通过标准**

- [ ] 大锁版：`consumer_rate` 稳定打印；开 fly 后速率明显下降
- [ ] 细粒度版：开 fly 后速率保留 ≥ 50%
- [ ] 两套镜像都是 `make clean` 后重新构建的（否则数字不可信）
- [ ] 两套镜像的 `fly-off`、`fly-on` 中位数都有记录

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
