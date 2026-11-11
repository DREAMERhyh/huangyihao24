# C-Core 简易内核（UCAS 操作系统研讨课 Project 4）

P4 的五个任务全部实现并在 QEMU 上验证通过；P3 的 shell、同步原语、双核 SMP、
taskset 与细粒度锁也保留可用。

| 任务 | 实现要点 | 验收程序 |
|---|---|---|
| 1 虚拟内存 | 每进程独立 Sv39 页表；用户程序统一链接到 `0x10000`；内核空间走 KVA 直接映射 | shell / `ps` |
| 2 按需调页 | 三类缺页（12/13/15）；首次访问才分配物理页；已映射页只补 A/D 位 | `rw` |
| 3 换页 | 每进程最多 128 个常驻可换出页；超出后 FIFO 写 SD 卡 32MB 处 | `swap` |
| 4 线程 | `pthread_create/join`；线程共享 pid/页表，独占内核栈与一页用户栈 | `mailbox` |
| 5 共享内存 | 按 key 找/建同一物理页；动态用户虚址 `0x100000000` 起；引用计数 | `consensus` |

**本文件只列验收时实际要敲的命令**；设计细节见源码注释、[docs/](docs/) 与 [log/](log/)。

---

## 一、构建

```bash
cd ~/oslab-course/huangyihao24      # 仓库根目录

make clean && make                  # 一条命令：编译 + 生成 build/image
```

`make` 会依次编译 bootblock、内核、7 个用户程序（`shell`、`fly`、`lock`、`rw`、
`swap`、`mailbox`、`consensus`），再由 `createimage` 拼出镜像：

```
bootblock 512B → 内核 → AppInfo → apps（字节级紧密，不补扇区）
```

**swap 区**：镜像末尾由 Makefile 补 64MB 零空间；SD 换页区从 32MB
（扇区 `0x10000`）开始，共 8192 页。

**构建成功标志**（末尾会打印布局核算，数字随源码改动小幅变化）：

```
[layout] kernel.img : ... bytes = 512(boot) + ...(kernel) + ...(appinfo)
[layout] apps.img   : ... bytes = ...(apps) + ...(尾部对齐)
[layout] total      : ... bytes = ... sectors（末尾补 ... 字节）
```

> 切换编译选项（如 `FINE_LOCK=1`）前必须 `make clean`：Makefile 只检查源文件
> 时间，不跟踪 CFLAGS。

---

## 二、运行

### 单核（任务 1、2、3 的默认口径）

```bash
make run
```

等 U-Boot 出现 `=> ` 提示符后输入：

```
loadboot
```

### 双核（任务 4、5 推荐）

```bash
make run-smp
```

等 `=> ` 后输入：

```
loadbootm
```

> **P4 用 `loadboot` / `loadbootm`，不要再用 P3 的 `loadbootd`。**
> P4 内核运行在 KVA 直接映射上，`loadbootd` 的 DASICS 地址隔离会让启动卡住。

启动完成后，内核直接拉起 shell（PID 1），屏幕出现：

```
------------------- COMMAND -------------------
> root@UCAS_OS:
```

退出 QEMU：先按 `Ctrl+a`，松开后再按 `x`。

---

## 三、shell 命令

| 命令 | 作用 |
|---|---|
| `ps` | 列出进程表：`[i] PID : n  STATUS : X mask: 0xN [Running on core N]` |
| `clear` | 清屏 |
| `exec <name> [args...] [&]` | 启动任务；不带 `&` 时 shell 等待它结束，带 `&` 则后台运行 |
| `kill <pid>` | 终止指定进程（P4 起按线程组处理） |
| `taskset <mask> <name> [args...] [&]` | 按核掩码启动任务（`0x1`=核 0，`0x2`=核 1，`0x3`=两核） |
| `taskset -p <mask> <pid>` | 修改已存在进程的核掩码 |
| `help` | 打印命令提示 |

P3 的同步原语（mutex / barrier / condition / mailbox）与 `waitpid` 仍可用；
P4 新增的系统调用号为 `58/59`（shm get/dt）、`60/61`（pthread create/join）。

---

## 四、P4 各任务验收步骤

> 行号一律从屏幕第 1 行开始数；shell 固定在下半屏（第 21 行起）。
> 每次改完源码先 `make clean && make`。

### 任务 1：虚拟内存与用户进程

**① 启动**

```bash
make run
loadboot
```

启动日志里应出现（双核时两行都有）：

```
> [INIT] CPU #0 has entered kernel with VM!
```

**② 确认 shell 在独立页表下运行**

```
ps
```

预期：

```
[Process Table]:
[0] PID : 1  STATUS : RUNNING mask: 0x3 Running on core 0
```

**③（可选）跑一个用户程序**

```
exec fly &
```

屏幕第 11~17 行出现左右飞的 ASCII 飞机，说明用户态程序在各自页表下正常运行。

**④ kill 一个进程**

`fly` 是死循环，用它验证 kill：

```
ps
kill 2          # 用 ps 里 fly 的实际 pid
ps
```

预期：第一条 `ps` 里 fly 为 `RUNNING/READY`；`kill` 成功后不打印任何东西；
第二条 `ps` 里该 pid 变为 `EXITED`，屏幕上的飞机停止。

**通过标准**

- [ ] 启动打印 `entered kernel with VM`
- [ ] shell 为 PID 1，`ps` 正常
- [ ] `exec fly &` 能飞
- [ ] `kill <fly 的 pid>` 后该进程变 `EXITED`，系统继续响应

---

### 任务 2：缺页异常与按需调页

`rw` 程序会向命令行给出的地址写入一个随机数再读回。`0x20000000` 在程序
映像之外，第一次写必然触发缺页；内核在缺页处理里分配物理页并建映射。

```
exec rw 0x20000000
```

预期：

```
0x20000000, <随机数>
Success!
```

再 `ps`：

```
[1] PID : 2  STATUS : EXITED mask: 0x3
```

**通过标准**

- [ ] `exec rw 0x20000000` 打印随机数与 `Success!`
- [ ] 进程正常 `EXITED`，没有 panic / 异常现场

**互斥锁 lock（任务 2 的同步部分）**

`lock` 会反复申请同一把 key=42 的互斥锁，并在持锁期间打印
`Has acquired lock and running.(0..4)`。启动 2~3 个实例：

```
exec lock 0 &
exec lock 1 &
exec lock 2 &
```

预期：三行各自先打印 `> [TASK] Applying for a lock.`；**同一时刻只有一行**
打印 `Has acquired lock and running.(i)`，三行轮流进入临界区，不会交叉。

再验证 kill 持锁者会释放锁：

```
ps                  # 找到某个 lock 进程的 pid
kill <pid>
```

预期：被 kill 的进程变 `EXITED`；其余 lock 实例继续打印（内核替被 kill
的持锁者放锁并唤醒等待者），系统不崩溃。

**通过标准**

- [ ] 2~3 个 lock 实例的输出不交叉（互斥生效）
- [ ] `kill` 一个持锁实例后，其余实例继续运行
- [ ] 没有进程"复活"、没有异常现场

---

### 任务 3：FIFO 换页

`swap` 程序依次写 256 个虚拟页（每页 4KB，共 1MB），超过每进程 128 个常驻页
的上限；内核按 FIFO 把最早进入的页写回 SD，再逐页读回校验。

```
exec swap
```

预期（约 10~20 秒）：

```
swap test: writing 256 pages...
swap test: reading back...
swap test PASSED (256 pages)
```

`ps` 中该进程随后为 `EXITED`。

**证明"真的换了页"（只 PASS 还不够）**

`PASSED` 只能说明读回值正确；要证明换页确实落到了 SD，可在宿主检查镜像
32MB 偏移处（swap 区起点）是否被写过：

```bash
dd if=build/image bs=1M skip=32 count=1 2>/dev/null | tr -d '\0' | wc -c
```

预期输出 **大于 0**（正常一次 swap 测试是几百字节的非零内容）。如果为 0，
说明 256 页都在内存里、没有触发换出，测试不算数。

**通过标准**

- [ ] 打印 `swap test PASSED (256 pages)`
- [ ] 读回值全部正确（有错会打印 `FAILED at page N`）
- [ ] 镜像 32MB 处非零（确实写了 SD）
- [ ] 无 SD 读写错误、无异常现场

---

### 任务 4：pthread 线程

`mailbox` 每个进程创建一个接收线程，主线程负责发送；a/b/c 三个实例互相通信。
线程与进程共享 pid 和页表，只独占内核栈与一页用户栈。

```
exec mailbox a &
exec mailbox b &
exec mailbox c &
```

预期：

- 每个实例先打印 `[x-send] started` / `[x-recv] started`；
- 随后持续出现 `to b: N`、`to c: N`、`from a: N` 等收发计数；
- `ps` 中每个 mailbox 有 **两行相同 PID**（主线程 + 接收线程），状态多为
  `BLOCKED`——mailbox 是死循环，属正常。

验收完 `Ctrl+a` → `x`。

**通过标准**

- [ ] a/b/c 都能创建接收线程并开始收发
- [ ] `ps` 中能看到同一 PID 的两行 PCB
- [ ] 无异常现场

> 也可用任意 `pthread_create` + `pthread_join` 的自写程序验证 join：
> 线程函数返回后由用户侧 trampoline 自动 `sys_exit`，join 会唤醒等待者。

---

### 任务 5：共享内存

`consensus` 主进程先 `sys_shmpageget(42)` 建立共享页，再 `sys_exec` 8 个子进程；
9 个进程通过共享内存里的 atomic 变量、mutex 和 barrier 选主，最后全部退出。

**只启动一次**（程序内部会自己创建 8 个子进程）：

```
exec consensus &
```

预期：

- 9 行 `ConsensusTask(<pid>) is ready at line N!`；
- 若干轮选举后每个进程打印 `(<pid>) exited!`；
- 约 40~60 秒后 `ps`：

```
[Process Table]:
[0] PID : 1  STATUS : RUNNING ...
[1] PID : 2  STATUS : EXITED ...
...
[9] PID : 10 STATUS : EXITED ...
```

（PID 具体值以屏幕为准；关键是 9 个 consensus 进程都变成 `EXITED`。）

**通过标准**

- [ ] 9 个任务都打印 ready 并最终 `exited`
- [ ] 选举结果唯一，无死锁
- [ ] `ps` 中 9 个进程全部 `EXITED`
- [ ] 无 `shmpageget failed`

---

## 五、关键地址与镜像字段（速查）

| 项 | 值 |
|---|---|
| bootblock 加载地址 | `0x50200000` |
| 内核入口（物理 / 内核虚地址） | `0x50202000` / `0xffffffc050202000` |
| KVA 直接映射偏移 | `0xffffffc000000000`（`KVA = PA + 偏移`） |
| 内核启动栈顶 | `0xffffffc050500000`（物理 `0x50500000`） |
| 内核根页表 | `0x51000000` |
| 内核动态页起始 | `0xffffffc052000000`（物理 `0x52000000`） |
| 用户程序入口 | `0x10000`（每进程独立页表） |
| 主线程用户栈 | 顶 `0xf00010000`，向下 8 页 |
| pthread 用户栈 | `0xe00000000` 起，步长 `0x2000` |
| 共享内存虚址范围 | `0x100000000` ~ `0x100100000` |
| swap 区 | SD 扇区 `0x10000`（32MB）起，8192 页；镜像补到 64MB |
| 每进程常驻可换出页上限 | 128 页（`USER_PAGE_LIMIT`） |
| 镜像信息字段 | `0x1f0`=AppInfo 字节偏移、`0x1f4`=apps 区字节偏移、`0x1f8`=app 数、`0x1fc`=内核扇区数、`0x1fe`=`55 aa` |
| 系统调用约定 | 号在 `a7`、参数在 `a0~a4`、返回值写回 `a0`；内核 `arch/riscv/include/asm/unistd.h` 与用户 `tiny_libc/include/syscall.h` 两处同值 |

---

## 六、文档索引与分支

- P1~P3 任务书、课件与教学文档：`docs/Project1/`、`docs/Project2/`、`docs/Project3/`
- P2/P3 工作日志：`log/`
- 全部工作在 `Project4` 分支，**每个任务一个提交**：

```
git log --oneline
feat: P4 任务5 共享内存
feat: P4 任务4 pthread 线程
feat: P4 任务3 FIFO 换页与 SD 交换区
feat: P4 任务2 缺页异常与按需调页
feat: 合并 P4 起始代码并完成任务1（启用虚拟内存与用户进程）
```

- P3 的 `FINE_LOCK=1` 细粒度锁开关仍保留（`make clean && make FINE_LOCK=1`），
  P4 验收默认使用大锁版。
