# 任务5 设计提案（修订版，已按验收方修正指令整改）

> 本文引用的任务书 / 课件原文均逐字取自
> `docs/taskbook-extracts-p1.md`（唯一权威引用源），标注出处；其余为本设计
> 自定方案或"验收方要求"，一律明确标注，不冒充原文。

---

## §0 任务书 / 课件原文逐字引用

以下全部引自 `docs/taskbook-extracts-p1.md`：

### §4.6 正文（guide_book_p1.pdf，节标题：任务5：SD卡镜像分区管理和动态加载用户程序）

> "本任务是做C-core的同学必须完成的。在前面的任务中，用户程序和内核被放到同一个
> 镜像文件中，并被一起拷贝到了SD卡上。本任务要求同学们模拟文件系统的分区管理，
> 把内核部分和用户程序分别做成两个镜像文件。在内核启动的过程中，只把内核镜像从
> SD卡读取到内存中。用户程序在要执行的时候再动态的从SD卡中加载到内存地址执行，
> 从而模拟系统中存在一个复杂的文件系统……"

### §4.6 要点解读（恰好 4 条）

> "1、在createimage中区分编译出来的文件哪些属于内核，哪些属于用户程序。并分别写入不同的文件。
> 2、修改Makefile，将两个文件都写入SD卡。
> 3、在内核镜像中标识用户镜像文件中各个用户程序的地址、大小等信息，用于后续的加载。
> 4、修改用户程序的加载过程，只加载要运行的用户程序。"

### §4.4 相关句（布局自主权）

> "同时，各个用户程序的task_info_t结构体也需要写入image中作为AppInfo，等待内核的
> 读取。具体的格式，以及应该怎么排列image文件中各个结构的顺序，请同学们自行思考实现。"

### slide 25（Lecture 1.pdf，标题 Task 5 (C-Core)）

> "Write kernel and user applications into two separate images
> The kernel image needs to store the information about user applications, such
> as the addresses, sizes of applications
> When kernel launches, only the kernel image is loaded
> You can provide commands to show user application information after the kernel
> is loaded
> When a user application starts, you need to load the application from the SD
> card at runtime"

> **注意**：任务书中不存在"§5"编号，Task 5 即 §4.6；任务书没有任何"启动日志证明
> 只加载内核镜像"之类的验收条款，那些是我们自行定死的**验收方要求**，不冒充原文。

---

## 1. 冲突表

| # | 冲突 | 处理 |
|---|---|---|
| 1 | **Makefile 修改授权**：任务 1 初始态（表 P1-1）把 Makefile 列为"不需要修改"，但 §4.6 要点 2 原文明示"修改Makefile，将两个文件都写入SD卡" | 以任务书发布约束为准，本次修改 Makefile（新增 kernel.img 与 apps.img 的拼接/写盘步骤）。 |
| 2 | **bios/common.c 不可改 vs 双设备不可行**：`arch/riscv/bios/common.c` 属"不需要修改"清单，其中 `sd_read()` 经 U-Boot 的 `blk_read_devnum(if_type_virtio, 0, ...)` **硬编码 virtio device 0**（Phase 0 §3 结论 3 已勘察确认），QEMU 挂第二块 `-drive` 永远读不到 | **死约束**：双镜像不能各占一个块设备 → 必须落地为**单个 SD 卡镜像内两分区**（任务书明言"模拟文件系统的分区管理"）。等价性论证见 §4。 |
| 3 | AppInfo 在整个 image 内的位置（任务书只说"写入 image 中作为 AppInfo"，未给确切扇区） | 布局必然性论证见 §2.2：**AppInfo 必须位于 kernel.img**。 |

---

## 2. 制品与布局

### 2.1 createimage 输出两个镜像（要点 1）

| 文件 | 内容 | 长度 |
|---|---|---|
| `build/kernel.img` | bootblock(1) + 内核(K) + AppInfo(A=1) | `(1+K+A) × 512` 字节，恰好整扇区 |
| `build/apps.img` | 各 app 按实际大小紧凑排列 | `Σ (app_sec × 512)` 字节，恰好整扇区 |

- `tools/createimage.c` **只写这两个文件，绝不直接写 `image`**。
- `0x1fc`(内核扇区数) / `0x1f8`(app 数) / `0x1fe`(签名) 约定保留在 kernel.img
  第 0 扇区尾部；`0x1fc`+`lh` 读取为官方推荐写法（§4.3，出处：guide_book_p1.pdf
  要点讲解，已由验收方背书，见任务 5 之前汇报）。
- 两个文件各自做布局自检：实测大小必须 == 公式核算，否则 `error()` 退出。

### 2.2 布局与 apps 区基址（可计算性 + 必然性）

```
kernel.img:  扇区 0          bootblock
             扇区 1..K       内核（0x1fc 记录 K）
             扇区 K+1        AppInfo（A = NBYTES2SEC(16×28) = 1 扇区）
apps.img:    扇区 0..        各 app 紧凑
```

**AppInfo 必须位于 kernel.img（必然性论证——矛盾法）**：
内核启动时 `init_task_info()` 要读 AppInfo 填 `tasks[]`（§4.4：AppInfo 等待内核读取；
slide 25：内核镜像需要存储应用信息）。若 AppInfo 放在 apps.img，则内核**启动阶段**
就必须读 apps 区——这与任务书正文"在内核启动的过程中，只把内核镜像从SD卡读取到
内存中"、slide 25"only the kernel image is loaded"直接冲突。因此 **AppInfo 只能放
kernel.img**，这是布局的必然结论，不是自由选择。

**apps 区基址（SD 绝对扇区）= 1 + K + A**：
kernel.img 恰好占 `(1+K+A)` 个扇区，apps.img 与之拼接后基址即 `1+K+A`；bootblock
从 `0x1fc` 读 K、+1（bootblock）、+A（自算）即得，内核同理。双方都能自算。

**bootblock 增传 `a2 = 1+K+A`（apps 基址）**：
1. 与 slide 20"bootblock 读 AppInfo 并传参给内核"的官方意图一致，布局契约显式为寄存器；
2. 将来若 kernel.img 内部结构变化，只改 bootblock 一处传值，内核代码不动；
3. 内核同时持有 `boot_appinfo_sector(=1+K)` 与 `boot_apps_base(=1+K+A)`，语义清晰。

### 2.3 AppInfo `start_sector` 字段语义：apps.img 内相对扇区号（推荐）

**原文背书**：§4.6 要点 3 原文"在内核镜像中标识**用户镜像文件中**各个用户程序的地址、
大小等信息"——"用户镜像文件中"即相对 apps.img 的局部语义。这为**相对扇区号**
提供了任务书原文背书。

论证对比：

| | apps.img 内相对扇区号（推荐） | SD 绝对扇区 |
|---|---|---|
| 与 kernel.img 解耦 | 完全解耦：AppInfo 只依赖 apps.img 自身布局 | 存 `1+K+A+rel`；若拼接方式/K 变化，全部槽位错 |
| 加载公式 | `bios_sd_read(BASE + rel, num)`，BASE=a2（一处取值） | `bios_sd_read(abs, num)`，需 K/A/BASE 三处全对 |
| 原文契合 | "用户镜像文件中……的地址"（要点 3） | 语义上是 SD 地址而非"用户镜像内偏移" |

**结论：`start_sector` 存 apps.img 内相对扇区号**，两文件真正解耦。

---

## 3. bootblock 启动路径（只读 kernel.img；slide 25 "only the kernel image is loaded"）

```
bootblock:
  ① 打印标志串
  ② 内核加载循环：lh(0x502001fc) -> K；bios_sd_read(0x50201000 + i*512, 1, 1+i)，
     i = 0..K-1（读扇区 1..K）
  ③ 传参：a0 = 1+K（AppInfo 扇区），a1 = lh(0x502001f8)（app 数），
           a2 = 1+K+A（apps 基址）
  ④ jal kernel
```

- **bootblock 的 bios_sd_read 调用点只有 1 处**（② 内核加载循环）；AppInfo 由内核
  启动时读取（bootblock 不读）。
- **启动阶段读范围的精确表述**：
  - u-boot 预读扇区 `0..1`（QEMU BOOTCOMMAND `virtio read 50200000 0 2`，非我们的代码）；
  - bootblock 读扇区 `1..K`；
  - 内核 `init_task_info()` 读 AppInfo 扇区 `1+K`；
  - 合计 `0..1+K` **⊆ kernel.img（0..1+K+A−1）**；
  - **apps 区（1+K+A 起）零读取** ✓ 满足 slide 25 bullet 3。
- 验证方式：**代码路径审查级**（bootblock.S 仅 1 处 sd_read 调用点，范围固化在
  `1..K`）+ 动态日志证实（启动段无任何对 apps 区的读；按名启动时才出现对
  `apps 基址 + 相对扇区` 的读取）。

---

## 4. 等价性论证（双镜像 == 单 SD 卡两分区）

任务书要求"修改Makefile，将两个文件都写入SD卡"（要点 2）；而 QEMU 侧课程脚本只传
单个 `-drive`（`build/image`），且 `bios_sd_read` 硬编码 device 0（冲突表 #2），
**两个镜像不可能各占一个块设备**。

因此 QEMU 落地为**单文件内两分区**：

```
build/image = kernel.img ∥ apps.img        （由 Makefile 的 cat 拼接，非 createimage）

SD 扇区映射：
  扇区 0 .. 1+K+A-1           = kernel.img（bootblock+内核+AppInfo）
  扇区 1+K+A .. 总大小         = apps.img（各 app 连续、按相对扇区寻址）
```

**等价性**：物理上 image 就是连续字节流，BIOS 按扇区号寻址、无文件边界概念；
"cat 拼接"就是"两个文件分别写入 SD 卡连续扇区"在文件层面的等价物。kernel.img
的扇区数 `1+K+A` 就是 apps.img 的写入偏移，双方可自算。

**make floppy 方案（二选一，写清选择）**：
- **方案 A（采用）**：保留单 `build/image`，`dd if=build/image of=/dev/sdX# conv=notrunc`
  一次写满——与 QEMU 同一文件，镜像一致性验证最简单；
- 方案 B（备选，写入 floppy 注释）：两条 `dd`（`kernel.img` 到 sdX 首部、`apps.img`
  到偏移 `1+K+A` 扇区），与"两文件分别写入连续扇区"字面一致，但偏移需在 dd 参数里
  再算一遍。
- 本机无板卡、一切到 QEMU 层，故选方案 A；方案 B 的 offset 计算式留在 Makefile
  floppy 注释里，将来上板可切换。

---

## 5. 内核侧改动

1. `head.S`：清完 BSS 后新增 `sd a2 → boot_apps_base`（与 a0/a1 一同保存）。
2. `init/main.c`：新全局 `boot_apps_base`（extern 于 task.h）。
3. `kernel/loader/loader.c`：`load_task_img(name)` 匹配后
   `bios_sd_read(vaddr_entry, sector_num, (uint32_t)(boot_apps_base + tasks[i].start_sector))`
   —— 只加载目标 app（要点 4）。
4. `0x1f8/0x1fc` 约定保留；`task_num` 仍来自 `boot_appnum`。
5. **信息显示命令**（slide 25 可选项，做）：
   - 输入行 == `"list"` 时打印表头 + 每个 task 的 `name / start(rel) / size(sec) / entry`，
     然后回到输入循环；
   - 十进制打印 helper：`put_uint()`（除 10 取余、倒序输出）。

---

## 6. 验证计划（verify.sh 断言扩展）

| 层面 | 断言 |
|---|---|
| 制品 | `kernel.img` 大小 == `(1+K+A)×512`；`apps.img` 大小 == Σapp×512；两文件 hexdump 关键点（标志串、0x1fc/0x1f8/0x1fe、AppInfo 槽位）与 createimage 打印一致 |
| 拼接 | `image` 大小 == 两文件之和；`head -c kernel.img image` 与 `tail -c apps.img image` 分别 == 两文件 |
| 行为回归 | 4 个 app 按名启动全回归；未知名/空行/超长被拒 |
| 信息命令 | `list` 输出含 `2048/auipc/bss/data`，各行的 rel start / size / entry 与 AppInfo 字节一致 |
| 布局 | 无固定空泡（app1 rel=0；相邻无空洞） |

verify.sh 注入序列：
```
loadboot → "list\n" → "auipc\n" → "bss\n" → "data\n" → "2048\n" → "\n"(菜单) → "q"
→ "nope\n" → "\n"(空行) → 结束
```

---

## 7. head.S 对齐假设复查（Task 5 必查）

`__bss_start % 8 == 0` 仍须成立：本任务新增 `boot_apps_base`（8B，进 BSS），
布局会微移，**build 后复查**；若失去 8 对齐则把清 BSS 循环改成 `sb`（crt0.S 已验证）。

---

## 8. 素材积累（slide 26 design review 四要点）

从本任务开始按四要点积累最终项目文档素材：
① 代码框架与相关文件；② bootBlock 与内核如何调用 BIOS；
③ 内核如何加载启动应用；④ image 组成与布局。
每任务日志已与四要点对应，最终项目文档据此重组。

---

## 9. 确认项（验收方已确认，开工即按此执行）

1. **AppInfo `start_sector` = apps.img 内相对扇区号**（含要点 3 原文背书）；
2. **bootblock 增传 `a2 = 1+K+A`**（apps 基址）；
3. **make floppy 用方案 A**（单 `dd`，注释写清方案 B 的 offset = `1+K+A` 计算式）；
4. **信息命令名 `list`**（保留名，文档化）。

## 10. 交付自查记录

- 【污染清理】已删除三处乱码残留（`os/../M 表 P1-1`、`usp=0`、`门牌 ②`）；
  冲突表 #1 中"floppy 双 dd"已改为与方案 A（单 dd）一致；§3"两条 sd_read 调用"
  已改为"1 处调用点（内核加载循环），AppInfo 由内核启动时读取，bootblock 不读"。
- 【自查声明】交付前全文做乱码 token 与内部矛盾自查，见 Task 5 汇报附注。