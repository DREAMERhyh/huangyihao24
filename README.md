# C-Core 简易内核（UCAS 操作系统研讨课 Project 2）

五个任务（PCB 与轮转调度、互斥锁、真系统调用、抢占式调度、自适应复杂调度）全部实现，
并在 QEMU 上验证通过（无开发板）；最终验收在 `loadbootd`（DASICS 用户/内核地址隔离开启）
下复跑通过。
**本文件只列验收时实际要敲的命令**；原理与设计说明见 [docs/](docs/)。

---

## 一、构建

```bash
cd ~/oslab-course/huangyihao24      # 仓库根目录

make dirs                           # 建 build/ 目录
make elf                            # 交叉编译 bootblock / 内核 / 12 个 app
make image                          # createimage 产出 kernel.img + apps.img，再 cat 拼成 image
```

等价的一条命令：

```bash
make all
```

**构建成功标志**（`make image` 末尾会打印布局核算；下面是本分支当前的实际输出，
数字会随源码改动小幅变化，核对要点是各项自洽、没有 error）：

```
[layout] kernel.img : 22172 bytes = 512(boot) + 21212(kernel) + 448(appinfo)
[layout] apps.img   : 64356 bytes = 64252(apps) + 104(尾部对齐)
[layout] appinfo 偏移 : 21724 (byte)
[layout] apps 区偏移  : 22172 (byte) —— 内核字节数 21212，二者【不补到扇区】
[layout] total      : 86528 bytes = 169 sectors（末尾补 104 字节）
```

> 每个 app 的加载窗口按**名字**（排序后的序号）算，与"本次构建了哪些目标"无关，
> 所以改完某个 app 的源码直接 `make all` 即可，不会和别的 app 撞地址。

---

## 二、在 QEMU 上运行验证

```bash
make run
```

**QEMU 里的操作顺序**：

```
① 等约 6 秒（U-Boot 自检 + autoboot 走完，出现 "=> " 提示符）

② 输入：
        loadbootd          ← 开启 DASICS 地址隔离（任务 1/2 阶段用 loadboot 亦可）

③ 屏幕依次出现：
        It's Dreamerhyh's bootloader...
        Available tasks: fly, fly1, fly2, fly3, fly4, fly5, lock1, lock2,
                         print1, print2, sleep, timer
        Please input a task name ('list' for info, empty line to start):

④ 一行一个任务名（要哪几个就输哪几个），最后**敲一个空行**才开始调度：
        list               ← 可选：打印应用信息表（name / offset / size / entry）
        print1             ← 选中 print1
        print2             ← 选中 print2
        fly                ← 选中 fly
        (直接敲回车)        ← 空行 = 选完了，开始调度

⑤ 想换一组任务：按 Ctrl+C
        —— 内核会热重启一次，一秒左右回到 "Available tasks" 提示符，
           可以直接接着选下一组，不用退出 QEMU、不用重等 U-Boot

⑥ 退出 QEMU：
        Ctrl+a    然后按    x
```

**各任务的验收组合**：

| 任务 | 输入的任务名 | 期望看到 |
|---|---|---|
| 1 PCB 与轮转调度（图 P2-2） | `print1` `print2` `fly` | 前两行计数基本同步增长，飞机在 5~11 行自左向右移动并循环 |
| 2 互斥锁（图 P2-3） | `lock1` `lock2` | 互斥成立（两侧份额均衡、burst 窗口零重叠）；换手很紧密——约 2/3 的情况一次一换（一方跑完 `(0)~(4)` 就交给对方），其余多为连续 2~4 次；等待方绝大多数时间停在 `Applying for a lock.` |
| 3 真系统调用（图 P2-8） | `print1` `print2` `lock1` `lock2` `sleep` `timer` | 六行分别是两个计数、锁的一持一等、`test sleep. (n)`、`timing! (秒数/ticks)` |
| 4 抢占式调度 | 同任务 3 | 输出与任务 3 一致。六个测试程序里已按任务书要求**注释掉全部 `sys_yield`**，全靠时钟中断推进 |
| 5 自适应调度 | `fly1` `fly2` `fly3` `fly4` `fly5` | 五架飞机始终编队同速飞行（内核按"谁飞得少谁先飞"调度）；各机 `[flyN] cycles:` 除以自己的 `CYCLE_PER_MOVE` 应基本相等（实测差 <0.5%） |

---

## 三、自动化验证（可选）

`.scratch/` 下有两个临时工具（该目录已 gitignore）：

```bash
.scratch/p2-run.sh <日志> "6|loadbootd" "8|print1" "1|print2" "1|" "15|"
python3 .scratch/emu.py <日志>                  # 还原最终屏幕
python3 .scratch/emu.py <日志> --watch 3,4      # 第 3、4 行的内容变化时间线（核对锁交替）
```

前者起 QEMU 并按"延时|文本"注入输入（空行写成 `"1|"`，`\x03` 发 Ctrl+C），
后者把串口日志里的 VT100 控制码重放成 80×50 屏幕，用来像看屏幕一样核对输出。

**公平性留档**（可选，答辩时很有说服力）：把时间基准换成"指令数"再跑一遍，
调度公平性就是构造性成立的，不受宿主负载干扰：

```bash
QEMU_EXTRA="-icount shift=3,sleep=off" .scratch/p2-run.sh <日志> "30|loadbootd" "30|print1" "3|print2" "3|" "60|"
```

实测 print1/print2 的相对差 ~0.006%（绝对计数随宿主窗口浮动，不必和某一组具体
数字逐字对上）。注意 icount 下 guest 变慢，注入延时要比平时放大几倍。

---

## 四、上板（有开发板时）

```bash
make all                            # 构建（与 QEMU 完全相同）
make floppy                         # sudo dd if=build/image of=/dev/sdb3 conv=notrunc
make minicom                        # sudo minicom -D /dev/ttyUSB1
```

在 minicom 里同样先输入 `loadbootd`，然后按上面第 ③④ 步操作。

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
| 镜像信息字段 | `0x1f0`=AppInfo 字节偏移、`0x1f4`=apps 区字节偏移、`0x1f8`=app 数、`0x1fc`=内核扇区数、`0x1fe`=`55 aa` |
| 镜像内部布局 | **字节级紧密**（不补扇区）：bootblock 512B → 内核 → AppInfo(448B) → apps 首尾相接；仅 bootblock 补到 512（`0x1fc/0x1fe` 字段占位所需） |
| 系统调用约定 | 调用号放 `a7`、参数放 `a0~a4`、返回值写回 `a0`；号表在内核 `arch/riscv/include/asm/unistd.h` 与用户 `tiny_libc/include/syscall.h` **两处保持同值** |
| 时钟与时间片 | `TIMER_INTERVAL = 150000` ticks 是默认时间片；任务 5 起时间片按"每前进一格平均烧的 CPU"自适应，且有下限（绝不为 0） |

## 附二：文档索引

- P2 答辩口径（对照 Lecture 2 第 23、46 页逐题作答）：`docs/Project2/P2-design-review.md`
- P2 汇总报告（整体架构 / 验收状态 / 已知限制）：`docs/Project2/P2-总结报告.md`
- P2 任务书、课件、环境手册原文：`docs/Project2/`
- P1 教学文档 11 件套 + 设计评审 + 代码演进史：`docs/Project1/`
- 各任务日志与工作流程：`log/`

## 附三：分支

全部工作在 `Project2` 分支：`p2-start`（合并 upstream/Project2 起点代码）之后，
每个任务一个中文 commit（见 `git log`）。
