# P1 Design Review（C-Core 五任务收官文档）

> 按 slide 26 design review 四要点组织，素材整合自 `log/` 各任务日志与
> `docs/taskbook-extracts-p1.md`（任务书原文唯一引用源）。本仓库到 QEMU 层面验证，
> 无开发板。

---

## ① 代码框架与相关文件职责

```
./
├── Makefile                   构建/运行总入口：make elf / image / run / floppy
│                              Task 5 起 image = createimage 产出双镜像后 cat 拼接
├── riscv.lds                  三段共用链接脚本（ENTRY(_start)、TEXT_START 由 Makefile
│                              逐产物 --defsym 注入）—— 铁律：不许改
├── docs/
│   ├── taskbook-extracts-p1.md   任务书/课件逐字原文入库（唯一权威引用源）
│   └── P1-design-review.md       本文档
├── log/                       Phase0 勘察、工作流程、task1..5 日志、task5 设计提案
│
├── arch/riscv/
│   ├── bios/common.c          C 侧 BIOS 封装（只读标志）：call_bios 填 a7/a0 跳 0x50150000
│   ├── boot/bootblock.S       ★Task 1/2/4/5：打印 → 读内核 → 传参 a0/a1/a2 → 跳内核
│   ├── crt0/crt0.S            ★Task 3：app 的 C 环境（.data 槽、共享栈、sb 清 bss、返回）
│   ├── kernel/head.S          内核入口：清 BSS(sd) / 存 boot_* 参数 / sp=0x50500000 / 跳 main
│   └── include/
│       ├── asm/biosdef.h      BIOS 功能号表（只读）
│       └── asm.h / common.h / csr.h   宏与声明
│
├── include/
│   ├── type.h                 基础类型
│   └── os/
│       ├── task.h             ★布局常量 + task_info_t(28B) + boot_* 声明 + _Static_assert 护栏
│       ├── loader.h           load_task_img(const char*) 声明
│       ├── kernel.h           内核侧 bios_*：经跳转表 KERNEL_JMPTAB_BASE=0x51ffff00
│       └── string.h / sched.h 字符串函数 / P3 调度（本项目未用）
│
├── init/main.c                ★内核主程序：bss_check / init_jmptab / init_task_info /
│                                任务名列表 / 主循环(按名加载 + list 命令)
├── kernel/loader/loader.c     ★按名匹配 → bios_sd_read(base+rel) 只读目标 app
├── libs/string.c              memcpy/strcmp 等
├── tiny_libc/include/kernel.h app 侧 bios_*（复用内核跳转表，3 槽）
└── tools/createimage.c        ★双镜像生成：kernel.img / apps.img + 布局自检
```

**各任务归属**：Task 1=bootblock 打印；Task 2=bootblock 读内核 + head.S 清 BSS/建 C 环境
+ main 回显；Task 3=createimage 固定扇区 + taskid 加载 + crt0；Task 4=紧凑排列 + AppInfo +
按名加载 + bootblock 传参；Task 5=双镜像 + 运行时动态加载 + list 命令。

---

## ② bootBlock 与内核如何调用 BIOS

### 调用约定（汇编层面，官方）

```
a7 = 功能号；a0/a1/a2… = 参数；跳转 0x50150000
0x50150000 处由 U-Boot 写入一条 ecall + ret（见 u-boot cmd/loadboot.c:write_oslab_instrs）
```

| 宏 | 值 | 接口 | 参数 |
|---|---|---|---|
| `BIOS_PUTCHAR` | 1 | 打印单字符 | a0=ch |
| `BIOS_GETCHAR` | 2 | 读字符（无输入返回 -1） | — |
| `BIOS_PUTSTR` | 9 | 打印字符串 | a0=str |
| `BIOS_SDWRITE` | 10 | 写 SD | a0=addr a1=块数 a2=块号 |
| `BIOS_SDREAD` | 11 | 读 SD | a0=addr a1=块数 a2=块号 |

- **bootblock 侧**：纯汇编直接 `la a0,msg` / `li a7,BIOS_PUTSTR` / `call bios_func_entry`
  （call 自动填 ra；0x50150000 的 ret 靠 ra 返回）。
- **内核 C 侧**：`arch/riscv/bios/common.c` 的 `call_bios()` 把功能号放 **a7**、参数放
  a0..a4（伪函数指针调用，U-Boot `mcall_trap()` 读 `regs->a7` 分发、结果写回 `regs->a0`）。
- **内核跳转表 0x51ffff00**：`include/os/kernel.h` 定义 `KERNEL_JMPTAB_BASE=0x51ffff00`，
  `init_jmptab()` 把 `port_write/port_write_ch/port_read_ch/sd_read/sd_write` 的函数指针
  拷进去；内核所有 `bios_putstr/bios_getchar/bios_sd_read` 都是
  `*(fn*)(KERNEL_JMPTAB_BASE + slot*8)` 间接调用。跳转表位于内存映射高位、远离内核
  代码与 app 窗口，跨 app 存活。
- **app 侧 tiny_libc**：`tiny_libc/include/kernel.h` 只暴露 3 个槽
  （PUTCHAR/GETCHAR/PUTSTR），app 经同一跳转表调用，复用内核已填好的表。

### 与 U-Boot 的交互（QEMU）

- QEMU `-kernel u-boot` 启动，autoboot 末尾 `virtio read 50200000 0 2` 预读镜像前 2 扇区
  → bootblock 与内核扇区 1 已在内存；
- 用户在 `=>` 输入 `loadboot` → `mret` 到 0x50200000（S 态、PMP 全开 RWX）；
- 之后内核经 0x50150000 的 ecall 陷入 U-Boot M 态分发。

---

## ③ 内核加载启动应用的完整链路

```
bootblock.lh(0x502001fc)=K
   │  a0=1+K(AppInfo)  a1=lh(0x502001f8)=app数  a2=1+K+A(apps基址)
   ▼ jal kernel
head.S: 清 BSS(sd) → sd a0→boot_appinfo_sector / a1→boot_appnum / a2→boot_apps_base
   │    → li sp,0x50500000 → jal main
   ▼
main:  bss_check → init_jmptab → init_task_info()
   │    init_task_info: bios_sd_read(512B 对齐缓冲 appinfo_buf, A, 1+K)  [A=自算=1]
   │                     memcpy(tasks, buf, task_num*28)
   │    打印 "Available tasks: ..." → 主循环：
   ▼
"auipc\n" → readline()（限长15 / \r\n 双终止 / 退格）
   │    strcmp(name,"list") → show_task_list()
   │    load_task_img(name)：
   │      for i: tasks[i].name 匹配
   │      bios_sd_read(vaddr_entry, sector_num, boot_apps_base + start_sector(rel))
   │        └ 只读目标 app（要点 4）
   ▼
return entry → 显式跳转：
   la ra,1f; jalr a0,%0; 1:   [双保险：与 GCC 隐式 jalr ra,rs 等价，见声明偏差]
   ▼
crt0._start:
   la t2,saved_ra;  sd ra,0(t2)
   la t3,saved_sp;  sd sp,0(t3)       ← .data 槽，非 .bss
   li sp,0x50400000                   ← 共享 app 栈
   la t0,__bss_start; la t1,__BSS_END__
   循环 sb zero,0(t0)                 ← 逐字节清（auipc 的 bss 非 8 对齐，仅 4B）
   call main
   恢复 sp/ra → jr ra                  ← 回到 main 循环回边，提示再现
```

**关键保证**：
- crt0 全程只用 `ra/sp/t0..t3`，**不碰 s0-s11**（内核 main 的 s 寄存器跨 app 存活，
  -O2 把 task_num/提示串地址 hoist 到 s 寄存器）；
- 现场槽在 `.data` 而非 `.bss`（否则清 bss 会抹掉刚存的值）；
- `.data` 槽方案 vs 借寄存器：a0 会被 app main 冲掉，s 寄存器违反被调用者 ABI。

---

## ④ image 组成与布局

### 双镜像（Task 5，§4.6"分别做成两个镜像文件"）

```
kernel.img = bootblock(1) + 内核(K) + AppInfo(A=1)      = (1+K+A)×512 字节
apps.img   = 各 app 按实际大小紧凑排列                    = Σ(app_sec)×512 字节
build/image = kernel.img ∥ apps.img   （Makefile cat 拼接，createimage 不直接写 image）
```

- **cat 拼接等价性论证**：image 物理上是连续字节流，BIOS 按扇区号寻址、无文件边界；
  QEMU 侧单 `-drive`（`bios_sd_read` 硬编码 device 0，见声明偏差），故单卡内两分区；
  板卡上即"两文件写入 SD 卡连续扇区"，offset = `1+K+A`。
- **floppy**：方案 A（单 dd 写 image，默认）；方案 B（双 dd，kernel.img + apps.img @
  `offset=(1+K+A)*512`，计算式在 Makefile floppy 注释，上板切换用）。

### 信息字段（kernel.img 第 0 扇区尾部，自建 createimage 写）

| 偏移 | 内容 | 读取方 |
|---|---|---|
| `0x1f8` | app 数目（2B LE） | bootblock `lh` → a1 |
| `0x1fc` | 内核扇区数 K（2B LE，**实际 p_filesz 口径**） | bootblock `lh` → 读内核循环 |
| `0x1fe` | 官方签名 `55 aa` | — |

### AppInfo（kernel.img 扇区 K+1，A=1）

`task_info_t{name[16], start_sector, sector_num, vaddr_entry}`（28B），16 槽，未用槽清零。
**`start_sector` = apps.img 内相对扇区号**（要点 3"用户镜像文件中……的地址"背书）：
两文件解耦，加载时 `base + rel`。

### 内存映射总表

| 地址 | 用途 |
|---|---|
| `0x50000000 – 0x501fffff` | U-Boot(BIOS)；`0x50150000` BIOS 入口（ecall+ret） |
| `0x50200000` | bootblock 加载地址（镜像第 0 扇区） |
| `0x502001f8 / 0x502001fc / 0x502001fe` | 信息字段（bootblock 扇区尾部） |
| `0x50201000` | 内核入口（head.S `_start`） |
| `0x50400000` | **app 共享栈顶**（向下生长，内核栈独享 0x50400000–0x50500000） |
| `0x50500000` | **内核栈顶**（KERNEL_STACK，向下生长，官方指定） |
| `0x51ffff00` | 内核 BIOS 跳转表（KERNEL_JMPTAB_BASE，5 槽×8） |
| `0x52000000 + i*0x10000` | 第 i 个 app 的任务窗口（入口 = 窗口基址） |

---

## 关键设计决策与声明偏差清单

| 项 | 决策/偏差 | 理由 |
|---|---|---|
| app 共享栈 `0x50400000` | 任务书建议"每 app 64KB 窗口顶部"，我们改为**全部 app 共享一个栈** | ①内核栈独享 0x50400000–0x50500000，两个向下生长的栈不交叠；②app 串行执行不并发；③任务书允许"可调整但需说明"。早期提案 0x504fffff 被否（与内核栈差 1 字节必撞） |
| `list` 命令名 | 保留名（验收方确认） | 信息显示命令（slide 25 可选项） |
| `sd_read` 死约束 | `bios/common.c` 不可改 + U-Boot 硬编码 device 0 → **不能挂第二块盘** | QEMU 上双镜像落地为**单 SD 卡镜像内两分区**（cat 拼接等价）；任务书明言"模拟文件系统的分区管理" |
| "只读 kernel.img"验证方式与局限 | **代码路径审查级 + 日志证据**：bootblock 唯一 1 处 sd_read（内核加载循环，范围 1..K）；内核启动只多读 AppInfo 扇区 1+K；loader 只在按名匹配后读 `base+rel` | QEMU 无法隔离出判别读 I/O 区域的日志，故按"代码审查 + 行为日志"两层论证，请以验收方要求为准 |
| A=1 的假设 | bootblock 硬编码 `addi a2, t0, 1`；AppInfo 固定 1 扇区 | `_Static_assert(sizeof*TASK_MAXNUM ≤ SECTOR_SIZE)` 编译期护栏；TASK_MAXNUM 增大时编译器立即报错 |

---

## 遗留风险（最终汇总）

1. **`bios_sd_read` 硬编码 device 0**：本方案依赖单设备，将来若课程环境允许多设备可重审；
2. **head.S 用 `sd` 清 BSS 依赖 `__bss_start` 8 字节对齐**（当前 `0x50201bd8` 通过）——每次都需复查，不满足就改 `sb`；
3. **bootblock 硬编码 A=1**：由 `_Static_assert` 守护其前提；
4. **工具链 jalr 显示约定**：GAS 对 `jalr ra,…` 编码正确（rd=x1），但 objdump 缩写显示为
   `jalr a0`（Task 3 曾误读）；内核已用 `la ra,1f` 显式设置返回地址作双保险。

## 验收证据索引

- 各任务自检：`log/taskN-工作日志.md`；验证脚本 `.scratch/verify.sh`（最终 20 断言）；
  QEMU 日志 `.scratch/task5-qemu.log`。
- 镜像产物：`build/kernel.img` / `build/apps.img` / `build/image`。
---

## 现场演示流程（9.16 评审用，引文一律取自 .scratch/task5-qemu.log 真实日志）

> 命令序列即最终 Build → 制镜像 → 运行 → 交互的完整路径。所有"预期输出"为真实
> 日志摘录（`^M` 是终端回车，`\e[...m` 是 2048 的 ANSI 上色，均按 `cat -v` 原样）。

### 步骤 0：构建（make dirs → make elf → make image）

```bash
$ make dirs && make elf         # 创建 build/，交叉编译 bootblock/内核/4 个 app
$ make image
```

`make image` 真实输出节选（含完整布局核算）：

```
cd ./build && ./createimage --extended bootblock main 2048 auipc bss data
0x50200000: bootblock sec  0 ..  0  (130 bytes, 1 sector)
0x50201000: main     sec  1 ..  6  (3032 bytes, 6 sectors)
-APPINFO--- appinfo  sec  7 ..  7  (448 bytes, 1 sectors)
0x52000000: 2048     relsec  0 ..  9  (4752 bytes, 10 sectors)
0x52010000: auipc    relsec 10 .. 11  (644 bytes, 2 sectors)
0x52020000: bss     relsec 12 .. 12  (368 bytes, 1 sectors)
0x52030000: data    relsec 13 .. 21  (4536 bytes, 9 sectors)
[layout] kernel.img : 8 sectors = 1(boot) + 6(kernel) + 1(appinfo)
[layout] apps.img   : 22 sectors = 22(apps)
[layout] apps base (SD 扇区) : 8 (= kernel.img 总扇区 = 1+K+A)
[layout] app#1 相对扇区 : 0（apps.img 内，无固定空泡）
cd ./build && cat kernel.img apps.img > image
```

**这一步验证了什么**：双镜像制品形态（kernel.img=8 扇区=1+6+1、apps.img=22=Σapp）、
AppInfo 相对扇区（2048 rel=0、auipc rel=10…）、apps 基址=8=1+K+A、cat 拼接等价。
制品断言（扇区整、`image==kernel+apps`）由 `.scratch/verify.sh` 自动核验。

### 步骤 1：启动（make run）

```bash
$ make run
```

真实日志节选（等待约 6 秒的 U-Boot autoboot 后出现 `=>` 提示符）：

```
U-Boot 2024.07UCAS_OS DASICS v3.1.0 ...
DRAM:  256 MiB
Hit any key to stop autoboot:  0
virtio read: device 0 block # 0, count 2 ... 2 blocks read: OK
=> loadboot
It's Dreamerhyh's bootloader...
Hello OS!
bss check: t version: 2
Available tasks: 2048, auipc, bss, data
```

**这一步验证了什么**：U-Boot 预读前 2 扇区 → `loadboot` 跳 0x50200000 → bootblock 打印
标志串 → 读内核（0x1fc=lh 得 6 扇区）→ head.S 清 BSS（`bss check: t` 过）→ main 出
欢迎信息与任务名列表。

### 步骤 2：查看应用信息（list）

真实日志：

```
Please input a task name: list
name              start size   entry
2048            0    10    0x52000000
auipc           10    2    0x52010000
bss             12    1    0x52020000
data            13    9    0x52030000
```

**这一步验证了什么**：slide 25 可选项"显示应用信息"命令成立；`start` 为 apps.img
相对扇区，`entry` 为各 app 的 Elf 入口（=窗口基址）。

### 步骤 3：按名启动四个 app（auipc → bss → data → 2048）

真实日志：

```
Please input a task name: auipc[auipc] Info: testpc = 0x52010064
Please input a task name: bss[bss] Info: passed bss check!
Please input a task name: data[data] Info: checking data!
[data] Info: checking data --- [OK]!
Please input a task name: 2048 2048 - Game
 * Use h-j-k-l / w-a-s-d keys to move the tiles.
 ...
Press 'Enter' key to continue.. 
 [2048]: Exit 2048 game ...
```

**每一步验证了什么**：按名匹配 → `bios_sd_read(base+rel)` 只读该 app → crt0 建立 C 环境
（auipc 打印 PC=0x52010064 说明代码确实在窗口内执行；bss 通过 = crt0 的 `sb` 逐字节
清 BSS 生效；data 全量校验通过；2048 进菜单后 q 退出 = crt0 交回控制权、回到内核
主循环）。

### 步骤 4：错误处理（nope 拒绝、空行判非法）

真实日志：

```
Please input a task name: nopeError: unknown task or load failed!
Please input a task name: Error: invalid input!
```

**这一步验证了什么**：未知名被拒（loader 遍历 tasks[] 未匹配 → 返回 0 → 提示错误并
回到输入循环）；空行判非法（readline 返回 0）；全程无跑飞。

### 完整交互序列（一次性可复制）

```text
make dirs && make elf && make image && make run
# 约 6s 后：
loadboot
list
auipc
bss
data
2048
<Enter>      # 2048 菜单继续
q            # 退出 2048，回到内核
nope         # 未知名
<Enter>      # 空行（主动提示 + 错误演示）
```

---

## 预答问题清单（评审问答预演）

1. **Q：app 共享栈顶为什么是 0x50400000，而不是紧贴内核栈的 0x504fffff，也不是每个 app 自己的 64KB 窗口顶部？**
   A：`KERNEL_STACK=0x50500000` 向下生长；若 app 栈顶是 0x504fffff，与内核栈只差一个
   字节，两个向下生长的栈必然交叠、互相踩踏。而 0x50400000 让内核栈独享
   0x50400000–0x50500000（1MB），app 栈向下仍有约 2MB，且架构是**串行单任务**——同一
   时刻只有一个 app 在跑，共享一个栈安全。不用每 app 窗口顶部，是因为那样 4 个窗口
   各留一截栈顶空间、浪费且任务书只要求"可调整但需说明"（依据：`arch/riscv/crt0/crt0.S`
   的 `APP_STACK_TOP`；否决 0x504fffff 的裁决记录在 `log/Phase0-代码勘察报告.md` 勘误 2）。
2. **Q：AppInfo 为什么必须放在 kernel.img 里？**
   A：内核启动时 `init_task_info()` 要读 AppInfo 填 tasks[]；若它在 apps.img，内核
   **启动阶段**就得读应用区，直接违反任务书正文"只把内核镜像从SD卡读取到内存中"与
   slide 25 "only the kernel image is loaded"。这是矛盾法推出的**必然性**，不是自由选择
   （依据：`kernel/loader/loader.c`→`init/main.c:init_task_info`；布局论证见
   `log/task5-设计提案.md` §2.2）。
3. **Q：AppInfo 的 start_sector 为什么用 apps.img 内相对扇区号，而不是 SD 绝对扇区？**
   A：要点 3 原文"标识**用户镜像文件中**各个用户程序的地址"就是相对语义的原文背书；
   只有相对扇区才能让两文件真正解耦——AppInfo 与 kernel.img 的长度 K、拼接顺序无关，
   加载时 `bios_sd_read(base + rel)` 的 base 只有一处取值。若存绝对扇区，K 一变全部
   槽位错（依据：`tools/createimage.c` 填充 `start_sector=app_rel_sec[i]`；
   `kernel/loader/loader.c` 用 `boot_apps_base + tasks[i].start_sector`）。
4. **Q：为什么不能挂第二块盘？"单卡两分区"的等价性怎么讲？**
   A：`arch/riscv/bios/common.c` 属于不可改清单，其中 `sd_read` 经 U-Boot
   `blk_read_devnum(if_type_virtio, 0, …)` **硬编码 device 0**——QEMU 第二块 `-drive`
   的设备 1 永远读不到（Phase 0 §3 结论 3 勘察确认）。所以双镜像只能落地为"单个
   sd.img/sd 卡内两分区"。等价性：image 是连续字节流、BIOS 按扇区号寻址、无文件边界，
   Makefile 的 `cat kernel.img apps.img > image` 就是"两文件写入 SD 卡连续扇区"在文件
   层面的精确模拟（依据：Makefile image 规则；`log/task5-设计提案.md` §4）。
5. **Q：crt0 如何把控制权交回内核？s 寄存器怎么保证？**
   A：进入 crt0 时把内核的 `ra`/`sp` 写进 **`.data` 槽**（`saved_ra`/`saved_sp`），app
   `main` 返回后再从槽恢复并 `jr ra`；之所以不借寄存器，是因为 a0 会被 app main 冲掉、
   s 寄存器又违反被调用者 ABI（内核 main 的 s 寄存器跨 app 存活，-O2 会把 task_num 等
   hoist 进 s 寄存器）。槽放 `.data` 而非 `.bss`，否则清 bss 会抹掉刚存的值。crt0 全程
   只用 `ra/sp/t0..t3`，四个 app 反汇编核验 **s0–s11 出现次数 = 0**（依据：
   `arch/riscv/crt0/crt0.S`；`log/task3-工作日志.md` 前置项 2）。
6. **Q：init_task_info 为什么要 512B 对齐缓冲中转，不直接整扇区读进 tasks[]？**
   A：tasks[] 是 16×28=448 字节，一次 `bios_sd_read` 读 512B 会**越界覆写 tasks 后面
   64 字节的 .bss**。用 `aligned(512)` 的 `appinfo_buf` 缓冲后再 `memcpy(task_num*28)`
   就只拷真实数据（依据：`init/main.c:init_task_info`；越界风险见
   `log/task4-工作日志.md` §3）。
7. **Q：-O2 到底怎么回事？任务书说 Task 4 加 -O2，怎么没有对比？**
   A：`start-code 的 Makefile` 的 `CFLAGS` 里**本来就有 -O2**，bootblock/内核/app 三份
   产物从 Task 1 起就一直是 -O2 编译，因此不存在"Task 4 加 -O2"的动作，也没有 Task 3
   的非 -O2 基线可对比——Task 4 内核还因新增功能从 4 增至 6 扇区。如实报告，无伪造
   对比（依据见 `log/task4-工作日志.md` §7 与 Makefile）。
8. **Q：上板时怎么写盘？双镜像和单 image 怎么切换？**
   A：`make floppy` 默认**方案 A**：单 `dd if=build/image of=/dev/sdX3`（与 QEMU 同一
   文件，一致性好）。上板要"两个文件分别写入连续扇区"时用**方案 B**：先写 kernel.img，
   再 `dd if=apps.img of=/dev/sdX seek=(1+K+A)`，偏移计算式（1+K+A、A=1）已留在
   `Makefile` 的 floppy 注释里（依据：Makefile floppy 规则注释；
   `log/task5-设计提案.md` §4）。
9. **Q：启动阶段怎么保证没读 apps 区？（"只读内核镜像"怎么论证）**
   A：**代码路径审查**：bootblock 全文件只有 1 处 `bios_sd_read`（内核加载循环，
   范围固化在扇区 1..K，见 bootblock.S）；u-boot 预读只覆盖扇区 0..1；内核启动只多读
   AppInfo 扇区 1+K；三者在 `0..1+K ⊆ kernel.img(0..1+K+A-1)` 内。**日志证据**：QEMU
   日志在按名启动前没有对 apps 内容的任何输出；loader 只在匹配后按 `base+rel` 读。
   **局限**：QEMU 无法输出读 I/O 的分区归属日志，故这是"代码审查+行为日志"两级论证。
10. **Q：跳转 app 的 `la ra, 1f` 双保险是什么意思？objdump 显示 `jalr a0` 是不是坑？**
    A：GAS 对 `jalr ra, x` 的编码是**正确的 rd=x1**（Task 3 曾因 objdump 把 rd=ra 的
    jalr 缩写显示成 `jalr a0` 而误判成"返回地址进 a0"，已用反汇编+Python 位段解码
    复核纠错）。`la ra,1f` 显式把返回地址指向循环回边，与编译器隐式行为等价，纯属
    双保险、把"返回地址在哪"写进源码（依据：`init/main.c` 跳转注释；
    `log/task3-工作日志.md` 前置项 1 最小复现）。
