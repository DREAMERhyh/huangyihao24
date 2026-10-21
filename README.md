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

## 四、各任务验收组合

| 任务 | 输入 | 期望看到 |
|---|---|---|
| 1 shell / 进程管理 | `ps`；`exec waitpid &`；`exec waitpid`（无 `&`）；`kill <pid>`；`clear`；退格；`xyzzy` | `ps` 显示 PID 0 idle、PID 1 shell；`waitpid` 测试里 `ready_to_exit` 拿两把锁→释放→退出，`wait_locks` 阻塞→被唤醒→拿锁→退出；`kill` 后目标转 EXITED；未知命令报错 |
| 2 barrier | `exec barrier &` | 3 个 `test_barrier` 的 `(i)` 计数严格并排增长，10 轮一起收尾（屏障语义成立） |
| 2 condition | `exec condition &` | producer 10 轮后 EXITED，3 个 consumer 的 `produced = consumed` 守恒、无负值（基于条件变量的生产者-消费者） |
| 2 mailbox | `exec mbox_server &`，再 `exec mbox_client &`（可启动多个） | server 的 `errorBytes` 恒 0、`correctBytes` 持续增长；client 的 `send bytes` 合计 ≈ server 收量 |
| 3 双核 | `make run-smp` + `loadbootm`；`exec multicore &` | 串口日志有 `[SMP] hart 0 entered kernel main` 与 `[SMP] hart 1 idle ready, entering scheduler loop`；multicore 打印单核/双核 ticks，双核更快（参考图 P3-7 约 1.8×） |
| 4 taskset | `taskset 0x1 affinity &`；`ps`；`taskset -p 0x2 <pid>`；`ps` | 5 个子任务 mask 全为 `0x1`（继承父进程）；改绑后 `ps` 显示 `mask: 0x2 Running on core 1`，该子任务明显变快；5 个 `test_affinity` 的 `auipc` 相同（共享一份代码映像） |
| 5 细粒度锁 | 两套镜像；`exec producer_rate &`、`exec consumer_rate &`，稳定后 `exec fly &` | 细粒度版开 fly 后 consumer 消费速率下降 **≤ 50%**；大锁版下降明显更大（任务书 §5.6） |

> 任务 2 的 condition / mailbox 测试是**常驻不退出**的：验收完用 `ps` 看状态，
> 再用 `kill <pid>` 收场，不要等程序自己结束。

---

## 五、细粒度锁双镜像对比（任务 5）

```bash
make clean && make                  # 构建大锁版 → build/image
# 运行：make run-smp → loadbootm
#   exec producer_rate &
#   exec consumer_rate &
#   （记录 fly-off 时 consumer 的每秒消费量）
#   exec fly &
#   （记录 fly-on 时 consumer 的每秒消费量）

make clean && make FINE_LOCK=1      # 构建细粒度版 → build/image
# 同样跑一遍，比较 fly-on / fly-off 的保留率
```

**判据**：细粒度版开 fly 后消费速率下降不超过一半（保留率 ≥ 50%）。
大锁版在大负载下会明显更差。

> 留档建议：加 `QEMU_EXTRA="-icount shift=3,sleep=off"`（或等价方式）把时间基准
> 固定成指令数，避免宿主负载/冻结污染速率口径；icount 下 guest 变慢，注入延时
> 要放大几倍。

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
