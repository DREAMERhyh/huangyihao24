# UCAS 操作系统研讨课 Project 4 · C-Core 现场验收

五项任务已在课程 PYNQ 开发板上用 `loadbootm` 双核启动验收，板测内核基线为 `78f95fb`。使用已经烧录的验收卡时，从第一节开始。需要重建镜像时，先按第四节操作。

| 任务 | 验收内容 | 用例 |
|---|---|---|
| 1 虚拟内存 | 双核启用 Sv39，运行独立地址空间中的用户进程 | `ps`、`fly`、`kill`、`clear` |
| 2 按需调页 | 第一次读写缺页时建映射，分页开启后互斥锁仍可用 | `rw`、`lock` |
| 3 FIFO 换页 | 超过 128 个常驻可换出页后，写入 SD 卡并换回 | `swap` |
| 4 线程 | 创建、等待和结束线程，三组线程异步收发 | `join`、`mailbox` |
| 5 共享内存 | 按 key 共享物理页，解除映射后回收，九个进程完成选举 | `consensus` |

## 一、接串口并双核启动

1. 把验收用 SD 卡插入开发板，启动跳线选 SD，`SW1` 拨到双核位置（远离板边）。用 Micro-USB 连接宿主机，在 VirtualBox 的 USB 设备中选中 `Xilinx TUL`。
2. 在 Linux 虚拟机的终端打开串口。串口参数为 **115200、8N1**，硬件和软件流控都设为 `No`。需要调整时可打开 `sudo minicom -s` 的串行端口设置。

   ```bash
   ls -l /dev/serial/by-id
   minicom -D /dev/ttyUSB1 -b 115200
   ```

   课程开发板的串口对应 `Xilinx TUL` 的 `if01`。若设备号变化，用上面列出的对应路径。终端尽量显示 50 行，测试程序通常在上半屏输出，shell 从第 21 行起显示。

3. 按一下开发板的 **SRST**，等 `bbl(NutShell) >` 提示符出现，然后执行下面的双核启动命令。

   ```text
   loadbootm
   ```

   成功时每个核各打印一行，随后出现 shell 提示符。

   ```text
   > [INIT] CPU #0 has entered kernel with VM!
   > [INIT] CPU #1 has entered kernel with VM!
   > root@UCAS_OS:
   ```

## 二、现场操作约定

第三节的命令都在开发板的 `root@UCAS_OS:` 提示符下逐条输入。`exec` 成功时会打印 `Info: execute ... successfully, pid = N ...`。每次按本轮显示的 PID 操作，进程表会保留以前的 `EXITED` 记录。输出较长时，等 shell 重新显示提示符，再输入下一条。

| 命令 | 用途 |
|---|---|
| `ps` | 查看 PID、状态、核亲和掩码和正在运行时的核号 |
| `clear` | 清屏并把光标移回 shell 区 |
| `exec <name> [args...] [&]` | 启动程序，追加 `&` 时在后台运行 |
| `kill <pid>` | 结束指定 PID 的线程组。把 `<pid>` 换成实际数字 |
| `taskset <mask> <name> [args...] [&]` | 按掩码启动程序，`0x1` 为核 0，`0x2` 为核 1，`0x3` 为两核 |
| `taskset -p <mask> <pid>` | 设置现有进程的核亲和掩码 |
| `help` | 查看命令提示 |

`fly`、`lock`、`mailbox` 会持续运行。每项观察结束后，用 `kill` 清理该项的后台进程。退出 Minicom 时按 **Ctrl+A**，松开后按 **X**。

## 三、五项验收步骤和预期结果

### 任务 1 · 虚拟内存与用户进程

先确认第一节的两个 `entered kernel with VM!` 提示。进入 shell 后，查看进程表并让飞机在后台运行。

```text
ps
exec fly &
ps
```

- 第一次 `ps` 应看到 PID 1 的 shell，状态为 `RUNNING`，默认掩码为 `0x3`。
- `exec fly &` 返回 PID，上半屏第 11–17 行出现移动的飞机。第二次 `ps` 可见该进程，状态通常为 `RUNNING` 或 `READY`。在 `RUNNING` 时还会显示 `Running on core N`。
- 用 `kill <fly 的 PID>` 结束飞机进程，再执行 `ps`。该 PID 应变为 `EXITED`，shell 仍可输入命令。执行 `clear` 后飞机残影消失，shell 提示符重新出现。

### 任务 2 · 动态页表、按需调页与互斥锁

下面的三个用户地址初始均未映射。`rw` 首次写入会触发缺页，在建立映射后读回写入值。

```text
clear
exec rw 0x10800000 0x80200000 0xa0000320
ps
```

屏幕上应出现以下四行，随机数随运行变化。

```text
0x10800000, <随机数>
0x80200000, <随机数>
0xa0000320, <随机数>
Success!
```

`Success!` 表示每个地址的读回值都一致。`ps` 中该 `rw` 进程应为 `EXITED`。

接着在不同的显示行启动两个使用同一把锁的进程。

```text
clear
exec lock 5 &
exec lock 7 &
```

- 观察上半屏两行输出。两路轮流显示 `Applying for a lock.` 和 `Has acquired lock and running.(0)` 到 `(4)`，每次只有一个进程进入临界区。`Has acquired lock and exited.` 表示这一轮离开临界区。
- 记下两个 PID，先用 `kill` 结束其中一个。另一个应继续打印持锁运行的提示。再结束另一个，执行 `ps`，两者都应为 `EXITED`。
- 快速刷屏时如果偶尔露出 `8;36H` 一类文字，可用 `ps` 是否响应、两路是否轮流持锁以及结束一路后另一路是否继续来判定。`8;36H` 是串口光标定位指令的一部分。

### 任务 3 · FIFO 换页与 SD 卡读写

```text
clear
exec swap
```

测试依次写入 256 个 4 KiB 页，超过每进程 128 个常驻可换出页的限制，然后逐页读回校验。开机后第一次执行会同时显示 SD 读写和数据校验结果。

```text
swap test: writing 256 pages...
swap: first SD write OK (slot 0)
swap: first SD read OK (slot 0)
swap test: reading back...
swap test PASSED (256 pages)
```

首写和首读提示分别对应本次启动后的首次成功 SD 交换区写入和读取。同一次开机重跑时，用 `PASSED (256 pages)` 核对数据结果。测试结束后 shell 应返回，`ps` 中 `swap` 为 `EXITED`。如有读回错误，用例会打印 `FAILED at page N`。

### 任务 4 · pthread 与三组 mailbox

先运行会自动退出的线程用例，检查 `pthread_create` 和 `pthread_join`。

```text
clear
exec join
```

工作线程返回后，等待它的主线程应打印 `pthread_join PASSED`，随后回到 shell。`ps` 中这个 PID 的两条线程记录都应为 `EXITED`。

接着启动三个 `mailbox` 实例，记下 shell 打印的三个 PID。

```text
clear
exec mailbox a &
exec mailbox b &
exec mailbox c &
ps
```

- 上半屏出现 `[a-send]`、`[a-recv]` 等六路输出。连续观察几秒，三组 `send` 的 `to` 和 `recv` 的 `from` 计数持续增长。
- `ps` 中每组各有两条相同 PID 的记录，对应主线程和接收线程。同组线程共用页表并绑在同一核，等待消息或休眠时显示 `BLOCKED`。
- 逐条 `kill` 三个实际 PID，再执行 `ps`。六条线程记录都应为 `EXITED`，shell 仍能接收下一项命令。

### 任务 5 · 多进程共享内存

```text
clear
exec consensus &
```

每轮只执行一次启动命令。用例先测试共享页的解除和重新映射，再自动创建 8 个子进程。一共 9 个进程进入选举。

1. 首个进程打印 `is the first!`，之后出现 9 条 `ConsensusTask(<pid>) is ready at line N!`。
2. 观察 9 轮选举。每轮有一位胜者打印 `(<pid>) exit now`，仍在参选的进程用 `we selecte` 打印同一位胜者。已经选中的进程会显示自己在哪一轮被选中。
3. 最后上半屏应出现 9 条 `(<pid>) exited!`。执行 `ps`，本轮的 9 个进程都应为 `EXITED`。
4. 要验证共享页回收后还能使用同一个 key，等本轮全部退出后，执行 `clear` 并再运行一次 `exec consensus &`。应看到新一轮 `is the first!`，仍有 9 个进程就绪并全部退出。

## 四、重新构建与烧录

在虚拟机的仓库根目录使用课程工具链构建。`make` 的默认配置为大内核锁版，与上板验收配置相同。切换 `FINE_LOCK=1` 等编译选项后也从 `make clean` 开始。

```bash
cd ~/oslab-course/huangyihao24
export PATH=/opt/riscv64-linux/bin:$PATH
make clean && make
cat build/kernel.img build/apps.img > build/p4-sd.img
```

构建末尾会打印 `[layout] kernel.img`、`[layout] apps.img` 和总扇区数。应用包括 `shell`、`fly`、`rw`、`lock`、`swap`、`join`、`mailbox`、`consensus`。板卡使用 `build/p4-sd.img`，其布局依次为 512 字节 bootblock、内核、AppInfo、连续的应用数据和末尾扇区填充。Makefile 同时生成 `build/image`，额外附加 64 MiB 供 QEMU 的块设备使用。

使用课程预分区的 SD 卡。将它插入读卡器并交给虚拟机，通过设备列表确认原始镜像分区。

```bash
lsblk -o NAME,SIZE,TYPE,TRAN,FSTYPE,PARTUUID,MOUNTPOINTS
```

把下面的 `SD_PART` 替换为 **确认未挂载的 SD 卡第三分区**，例如 `/dev/sdb3`。烧录从这个分区的首扇区开始，换页区位于同一分区的 32 MiB 偏移处。

```bash
SD_PART=/dev/sdX3
IMAGE=build/p4-sd.img
SECTORS=$(( $(stat -c %s "$IMAGE") / 512 ))

sudo dd if="$IMAGE" of="$SD_PART" bs=512 oflag=direct conv=notrunc,fsync status=progress &&
  sudo blockdev --flushbufs "$SD_PART" &&
  sudo dd if="$SD_PART" bs=512 count="$SECTORS" iflag=direct status=none | cmp -- "$IMAGE" - &&
  echo 'SD 镜像回读一致'
```

看到 `SD 镜像回读一致` 后，把卡插回开发板，从第一节开始验收。

## 五、关键地址与镜像字段

| 项 | 值 |
|---|---|
| bootblock 加载地址 | `0x50200000` |
| 内核入口（物理 / 内核虚拟地址） | `0x50202000` / `0xffffffc050202000` |
| KVA 直接映射偏移 | `0xffffffc000000000`（`KVA = PA + 偏移`） |
| 内核启动栈顶 | `0xffffffc050500000`（物理 `0x50500000`） |
| 常驻内核根页表 / 启动根页表 | `0x51000000` / `0x51001000` |
| 内核动态页起始 | `0xffffffc052000000`（物理 `0x52000000`） |
| 用户程序入口 | `0x10000`（每进程独立页表） |
| 主线程用户栈 | 顶 `0xf00010000`，向下 8 页 |
| pthread 用户栈 | `0xe00000000` 起，步长 `0x2000` |
| 共享内存虚址范围 | `0x100000000` 至 `0x100100000`（右端不含） |
| SD 卡换页区 | 第三分区相对扇区 `0x10000`（32 MiB）起，共 8192 页 |
| 每进程常驻可换出页上限 | 128 页（`USER_PAGE_LIMIT`） |
| 镜像信息字段 | `0x1f0` 为 AppInfo 偏移，`0x1f4` 为 apps 偏移，`0x1f8` 为应用数，`0x1fc` 为内核扇区数，`0x1fe` 为 `55 aa` |
| 新增系统调用号 | `58/59` 为共享内存获取和解除，`60/61` 为线程创建和等待 |
| 系统调用约定 | `a7` 为调用号，`a0`–`a4` 为参数，`a0` 接收返回值 |

课程提交分支为 `Project4`。P1–P3 的课程材料和工作日志见 [docs](docs/) 与 [log](log/)。
