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
出 two 文件解耦，加载时 `base + rel`。

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