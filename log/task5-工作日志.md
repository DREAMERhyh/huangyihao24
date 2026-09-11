# 任务5 工作日志：双镜像 + 运行时动态加载 + list 命令

## 任务书 / 课件原文引用（一律以 docs/taskbook-extracts-p1.md 为准）

完整逐字文本见 `docs/taskbook-extracts-p1.md`。本日志只摘与本任务直接相关者：

- **§4.6 正文**："……把内核部分和用户程序分别做成两个镜像文件。在内核启动的过程中，
  只把内核镜像从SD卡读取到内存中。用户程序在要执行的时候再动态的从SD卡中加载到
  内存地址执行，从而模拟系统中存在一个复杂的文件系统……"
- **§4.6 要点 1**："在createimage中区分编译出来的文件哪些属于内核，哪些属于用户程序。并分别写入不同的文件。"
- **§4.6 要点 2**："修改Makefile，将两个文件都写入SD卡。"
- **§4.6 要点 3**："在内核镜像中标识用户镜像文件中各个用户程序的地址、大小等信息，用于后续的加载。"
- **§4.6 要点 4**："修改用户程序的加载过程，只加载要运行的用户程序。"
- **§4.4**："……task_info_t结构体也需要写入image中作为AppInfo……具体的格式，以及应该
  怎么排列image文件中各个结构的顺序，请同学们自行思考实现。"
- **slide 25**："When kernel launches, only the kernel image is loaded" /
  "You can provide commands to show user application information after the kernel is loaded"

> **验收方要求**（非任务书原文）："启动日志证明只加载内核镜像"等验收条款是我们
> 自上定的验收标准，标注为"验收方要求"。

## 冲突表

| # | 冲突 | 处理 |
|---|---|---|
| 1 | Makefile 修改授权：任务 1 初始态把 Makefile 列为"不需要修改"，§4.6 要点 2 明示"修改Makefile" | 以任务书发布约束为准，本次修改 Makefile |
| 2 | bios/common.c 不可改，其 `sd_read` 硬编码 virtio device 0 → QEMU 第二块 `-drive` 读不到 | 死约束 → 单 SD 卡镜像内两分区（任务书明言"模拟分区管理"） |
| 3 | AppInfo 位置：任务书只说"写入 image 中作为 AppInfo"，未给确切位置 | 必然性论证：AppInfo 只能放 kernel.img（见 §设计决策） |

对照"验收方要求"：`app1=1+K+A=7` 布局合规（§4.4 布局自主权原文背书），
不再作为解释项。

## 改动清单

| 文件 | 改动 |
|---|---|
| `tools/createimage.c` | **双镜像输出**：写 `kernel.img`（bootblock+内核+AppInfo）与 `apps.img`（各 app 紧凑），绝不再直接写 `image`；AppInfo `start_sector` 改为 **apps.img 内相对扇区号**；两文件各自整扇区 + 布局自检 |
| `Makefile` | `image` target：`createimage` 产出两文件后 `cat kernel.img apps.img > image`；`floppy` 采用方案 A（单 dd），注释写明方案 B 的 `offset=1+K+A` 计算式 |
| `include/os/task.h` | 新增 `boot_apps_base` 声明（a2） |
| `arch/riscv/boot/bootblock.S` | 增传 `a2 = 1+K+A`（apps 区基址），连同 a0/a1 一起跳内核 |
| `arch/riscv/kernel/head.S` | 清完 BSS 后存 `a2 → boot_apps_base` |
| `init/main.c` | 定义 `boot_apps_base`；新增 `put_uint`/`put_hex`/`show_task_list()`；主循环识别 `list` 命令 |
| `kernel/loader/loader.c` | `bios_sd_read(vaddr, num, boot_apps_base + tasks[i].start_sector)`——相对扇区 + 基址，**只读目标 app** |
| `docs/taskbook-extracts-p1.md` | 新增：任务书/课件逐字原文入库（唯一权威引用源） |
| `log/Phase0-代码勘察报告.md` | 勘误 #4：原文引用一律以入库文本为准 |

未动：`riscv.lds`、`crt0.S`、`arch/riscv/bios/`、`biosdef.h`。官方 `createimage` 权限 644。

## 关键设计决策

### 1. 双镜像制品与"cat 拼接 == 连续扇区写入"等价性

```
kernel.img = bootblock(1) + 内核(K) + AppInfo(A=1)      → (1+K+A) × 512 字节
apps.img   = 各 app 紧凑排列                              → Σ(app_sec) × 512 字节
build/image = kernel.img ∥ apps.img（Makefile 的 cat，createimage 绝不直接写）
```

- **等价性论证**：image 物理上是连续字节流，BIOS 按扇区号寻址、无文件边界概念；
  cat 拼接就是"两文件分别写入 SD 卡连续扇区"在文件层面的等价物（板卡上扇区
  0..1+K+A-1 是 kernel.img、1+K+A 起是 apps.img）。QEMU 侧只有一个 `-drive`
  （`bios_sd_read` 死约束，冲突表 #2），所以必须单文件内分区。
- **make floppy**：方案 A（单 dd 写 image，与 QEMU 同一文件）；方案 B 的
  `offset=1+K+A` 计算式留在 floppy 注释里备上板。

### 2. AppInfo 必须位于 kernel.img（必然性论证）

内核启动时 `init_task_info()` 要读 AppInfo 填 tasks[]。若 AppInfo 放 apps.img，
内核**启动阶段**就得读 apps 区——违反任务书正文"只把内核镜像从SD卡读取到内存中"
与 slide 25"only the kernel image is loaded"。故 AppInfo 只能放 kernel.img。

### 3. start_sector = apps.img 内相对扇区号

- 原文背书：要点 3"标识**用户镜像文件**中各个用户程序的地址、大小"——相对语义。
- 解耦：AppInfo 只依赖 apps.img 自身布局，与 kernel.img 长度 K 无关；若未来
  拼接方式变化，"相对 + 基址"仍成立，绝对扇区则会全错。
- 加载：`bios_sd_read(vaddr, num, boot_apps_base + rel)`，base 只在一处（a2）。

### 4. 传参 a2 = 1+K+A

bootblock 从 0x1fc 读 K，`addi a2, t0, 1`（t0 循环结束时 = 1+K，A=1）。内核
从 `boot_apps_base` 取用。三个参数（a0=AppInfo扇区 / a1=app数 / a2=apps基址）
都在 head.S 清完 BSS 后入库。

### 5. list 命令（slide 25 可选项）

```c
if (strcmp(name, "list") == 0) { show_task_list(); continue; }
```
`show_task_list()` 每次打印表头 + `name / start(相对扇区) / size / entry(hex)`。
十进制/十六进制打印用自写 `put_uint`/`put_hex`（内核无 itoa）。

### 6. 启动阶段读范围精确表述（slide 25 "only the kernel image is loaded"）

- u-boot 预读扇区 `0..1`（QEMU BOOTCOMMAND，非我们的代码）；
- bootblock 读扇区 `1..K`（**唯一 1 处 bios_sd_read 调用点**）；
- 内核 `init_task_info()` 读 AppInfo 扇区 `1+K`；
- 合计 `0..1+K` ⊆ kernel.img（`0..1+K+A−1`）；
- **apps 区零读取**（1+K+A 起直到按名启动才被 loader 读）。

## 构建过程与制品核验

```bash
make elf && make image
```

createimage 布局表：

```
0x50200000: bootblock sec  0 ..  0  (130 bytes, 1 sector)
0x50201000: main     sec  1 ..  6  (3032 bytes, 6 sectors)
-APPINFO--- appinfo  sec  7 ..  7  (448 bytes, 1 sectors)
0x52000000: 2048     relsec  0 ..  9  (4752 bytes, 10 sectors)
0x52010000: auipc    relsec 10 .. 11  (644 bytes, 2 sectors)
0x52020000: bss      relsec 12 .. 12  (368 bytes, 1 sectors)
0x52030000: data     relsec 13 .. 21  (4536 bytes, 9 sectors)

[layout] kernel.img : 8 sectors = 1(boot) + 6(kernel) + 1(appinfo)
[layout] apps.img   : 22 sectors = 22(apps)
[layout] apps base (SD 扇区) : 8 (= kernel.img 总扇区 = 1+K+A)
[layout] app#1 相对扇区 : 0（apps.img 内，无固定空泡）
```

核验：
- `kernel.img = 4096B = 8 扇区`、`apps.img = 11264B = 22 扇区`、`image = 15360B = 30 = 8+22`；
- 首扇区尾部 `0400 / 0600 / 55aa`：
  - 0x1f8 = app 数 4；0x1fc = 内核 6 扇区（0x1fc+lh 官方推荐读取约定）；0x1fe = 签名 ✓
- AppInfo（kernel.img 扇区 7）：`2048: start=0 num=10 entry=0x52000000`、
  `auipc: start=10 num=2 entry=0x52010000`、`bss: start=12 num=1`、`data: start=13 num=9`，
  未用槽全零——**相对扇区号铁证**。

## 自动化验证（verify.sh，20 条断言全过）

注入：`loadboot → "list\n" → "auipc\n" → "bss\n" → "data\n" → "2048\n" → "\n"(菜单) → "q" → "nope\n" → "\n"(空行)`

制品断言：两镜像整扇区、`image == kernel.img + apps.img`；
AppInfo `2048.start=0`、`auipc.start=10`（相对扇区）；
`0x1f8=4 / 0x1fc=内核扇区数 / 0x1fe=55aa`；
list 表头 + 4 个 entry（0x52000000/0x52010000/0x52020000/0x52030000）；
4 个 app 按名启动全回归；2048 退出返回；未知名 nope 拒绝；空行 invalid。

QEMU 日志关键节选：

```
Available tasks: 2048, auipc, bss, data
Please input a task name: list
name              start size   entry
2048            0    10    0x52000000
auipc           10    2    0x52010000
bss             12    1    0x52020000
data            13    9    0x52030000
Please input a task name: auipc[auipc] Info: testpc = 0x52010064
Please input a task name: bss[bss] Info: passed bss check!
Please input a task name: data[data] Info: checking data --- [OK]!
Please input a task name: 2048 2048 - Game ... [2048]: Exit 2048 game ...
Please input a task name: nopeError: unknown task or load failed!
Please input a task name: Error: invalid input!
```

**验收方要求核对**：启动日志证明 2048 的 entry 地址 0x52000000 首现于 list 命令
（文本层面），而 QEMU 日志中按名启动前无对 apps 区数据的输出——结合代码路径
审查（bootblock 唯一 1 处 sd_read 调用、loader 只在匹配后按 `base+rel` 读取），
满足"启动阶段只加载内核镜像、应用按需动态加载"。

## head.S 对齐假设复查（Task 5 必查）

`__bss_start = 0x50201bd8`，`%8 == 0` → 对齐假设成立，`sd` 清 BSS 安全
（本次新增 `boot_apps_base` 进 BSS 后已复查）。

## 踩坑记录

1. **grep -F 下 `\[` 不转义**（第三次踩）：`grep -Fq "\[auipc\]"` 匹配的是字面
   `\[auipc\]`，不是 `[auipc]`。断言误用转义导致 4 条误报失败（app 实际正常启动）。
2. **AppInfo 偏移手算错误**：`auipc` 记录的 start 字段在 `0xe2c`（非 `0xe30`，
   后者是 its sector_num）。以鲁棒的公式计算槽偏移，不再手加。
3. **createimage 自检的 fclose 后 fseek 隐患**：初版大小自检在 `fclose(img)` 后又
   `fseek(img,...)`，是 UB。改为不依赖文件重开、直接用 phyaddr 断言。
4. **`make image` 现在会**同时**产出 kernel.img/apps.img/image 三份**：验证脚本必须
   以 createimage 打印值复核，不要对 kernel.img 大小硬编码（本轮 8 扇区，
   后续代码变化会变）。

## slide 26 design review 素材（积累）

- ① 代码框架与相关文件：双镜像相关文件清单见上"改动清单"。
- ② bootBlock 与内核如何调用 BIOS：BIOS 调用约定（a7 功能号 + a0.. 参数，跳
  `0x50150000` 的 ecall+ret）始终一致；本次 bootblock 只在 1 处调用 `BIOS_SDREAD`。
- ③ 内核如何加载启动应用：`init_task_info` 读 AppInfo → `load_task_img(name)`
  匹配后 `bios_sd_read(base + rel)` → 显式 `jalr`（la ra,1f 双保险）→ crt0 恢复现场
  返回 → main 循环。
- ④ image 组成与布局：kernel.img ∥ apps.img，AppInfo 相对扇区、a2=1+K+A，
  等价性论证见调和文件。

## 结论

Task 5 自检全部通过：双镜像产出与 cat 拼接成立、AppInfo 相对扇区解耦两文件、
bootblock 只读 kernel.img 范围（code-path 审查级验证）、按名加载只读目标 app、
list 命令输出正确、4 app 全回归、未知名/空行拒绝、head.S 对齐复查通过。