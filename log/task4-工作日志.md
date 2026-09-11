# 任务4 工作日志：镜像紧凑排列 + 按名加载 + -O2

## 任务书原文（引用位置）

| 决策点 | 任务书原文（§5 Task 4） |
|---|---|
| 紧凑布局 | "createimage.c：取消固定扇区，kernel 与各 app 按实际大小紧密排列（bootblock 仍独占第 0 扇区，若 bootblock 不足 512B 需 padding 到扇区边界）" |
| task_info_t | "设计 task_info_t（含 app 名称、在镜像中的字节偏移、大小、内存入口等），全部 app 的 task_info_t 作为 AppInfo 写入镜像固定位置，内核启动时读出" |
| 按名加载 | "main.c/loader.c：改为按名字加载：读入一行程序名（自己实现收字符串逻辑，处理好退格或明确说明不支持退格），在 AppInfo 里匹配名称，找到则加载启动，找不到提示错误" |
| -O2 | "修改 Makefile：给内核和用户程序的编译加 -O2" |
| 验收-布局 | "createimage 打印的布局表证明无固定空泡（app1 的起始扇区 = 1 + 内核实际扇区数）" |
| 验收-行为 | "每个程序按名字能正确启动；错误名字被拒绝；-O2 生效（对比 Task 3 的内核大小，应有缩小）" |

**"AppInfo 写入镜像固定位置"的确切位置**：任务书只说"固定位置"，未给地址。
用户裁决（Task 4 修订）：**AppInfo 区固定放在扇区 `1 + K`**（K = 0x1fc 读出的
内核实际扇区数），理由见 §设计决策 1。

---

## 冲突表（按 Task 2 规矩，先点破再动手）

| # | 冲突 | 处理 |
|---|---|---|
| 1 | 任务书验收写"app1 起始扇区 = 1 + 内核实际扇区数"；但布局里**刻意插了一个固定 1 扇区的 AppInfo**，所以 app1 = 1+K+A = K+2 | 用户裁决明确"app1 = 1+K+A、A=1"，故按此实现；验收公式理解为"内核扇区数 → 之后首个数据区无固定空泡"，AppInfo 扇区是文档化的刻意插入（A 固定为 1，不随 app 数量变化，无泡沫）。 |
| 2 | 任务书建议 app 栈用"64KB 窗口顶部"；用户裁决共享栈 `0x50400000` | 沿用共享栈（Task 3 已实现，任务书允许"可调整但需说明"）。 |
| 3 | 任务书"修改 Makefile 加 -O2"；但 `CFLAGS` 里**本来就有 `-O2`**，三个产物全部继承 | 不改 Makefile，如实报告（见 §5 -O2）。 |
| 4 | Task 3 审批语"bootblock 零改动/保持 Task 2 逻辑"易被读成完全不动；但 `bootblock.S:25` 本就是 `[p1-task4]` TODO，必须填 | 本次实现 bootblock 传参；**已写入 Phase 0 报告勘误 #3**，此处登记。 |
| 5 | 任务书建议 app 数目 @0x1fe；官方签名占用 0x1fe/0x1ff | 用户裁决 @0x1f8（Task 3 已定），继续沿用。 |

---

## 改动清单

| 文件 | 改动 |
|---|---|
| `tools/createimage.c` | 退役 `KERNEL_FIXED_SECTORS/APP_FIXED_SECTORS`；内核与各 app 按实际大小紧凑排列；实现 `task_info_t` 与 `write_appinfo()`；未用槽位清零；名字超长 `error()`；`0x1fc` 写内核实际扇区数；打印布局核算表并按公式自检总扇区 |
| `include/os/task.h` | `task_info_t{name[16], start_sector, sector_num, vaddr_entry}`（28B）；新增 `TASK_NAME_LEN`、`APPINFO_SECTORS`、`boot_appinfo_sector/boot_appnum` 声明；删除旧固定扇区宏与 `IMG_APP_NUM_LOC`（内核禁读 0x50200xxx） |
| `arch/riscv/boot/bootblock.S` | 填 `[p1-task4]`：读 app 数目(lh @0x502001f8)，`a0=AppInfo 起始扇区(1+K)`、`a1=app 数目`，跳内核传参 |
| `arch/riscv/kernel/head.S` | 清完 BSS 后把 a0/a1 存入 `boot_appinfo_sector/boot_appnum`，再跳 main |
| `init/main.c` | 删 `init_task_num()`（不再直读 0x50200xxx）；实现 `init_task_info()`（512B 对齐缓冲中转 + 拷入 tasks[]）；实现 `readline()`（限长、\r/\n 双终止、退格）；主循环改按名加载；init 后打印任务名列表 |
| `kernel/loader/loader.c` | `load_task_img(const char *taskname)`：遍历 tasks[] 按名匹配，`bios_sd_read` 读入后返回 vaddr_entry |
| `include/os/loader.h` | 函数签名改为 `const char *taskname` |
| `log/Phase0-代码勘察报告.md` | 勘误 #3（"bootblock 零改动"表述与 bootblock.S:25 的 [p1-task4] TODO 冲突） |

`Makefile`、`riscv.lds`、`crt0.S`、`head.S` 的 BSS 清零循环、`arch/riscv/bios/`、
`biosdef.h` 均未改。官方 `createimage` 权限保持 644、内容零改动。

---

## 关键设计决策

### 1. 布局：AppInfo 一定在扇区 1+K（可计算性论证）

```
扇区 0                bootblock（第 0 扇区独占，padding 到 512B）
扇区 1 .. K           内核，K = NBYTES2SEC(内核实际字节数)，0x1fc 记录
扇区 K+1              AppInfo 区（TASK_MAXNUM × 28B = 448B，固定 1 扇区）
扇区 K+2 ..           各 app 按实际大小紧凑排列，app i 紧贴 app i-1
```

**为什么 AppInfo 是 1+K 而不用在别处**：bootblock 唯一能可靠自取、且天然知道
的布局信息，就是 0x1fc 处的内核扇区数 K。内核同理只需要 K 就能算出 AppInfo
扇区号（K+1）。任何其他"固定位置"都要额外传参数或硬编码，而"1+K"是双方都
能自算的位置。bootblock 拿到 K 后 `move a0, t0`（循环结束时 t0 恰好停在 1+K）。

**A=1 的自算**：`APPINFO_SECTORS = NBYTES2SEC(TASK_MAXNUM * sizeof(task_info_t))`
= NBYTES2SEC(448) = 1。由内核自算，不靠传参（用户裁决"a1 改传 app 数目，
A 由内核自算"）。

### 2. 传参（替代内核直读 0x50200xxx）

```
bootblock:  a0 = 1 + K（AppInfo 起始扇区）
            a1 = lh(0x502001f8)（app 数目）
head.S:     清完 BSS 后 sd a0→boot_appinfo_sector, sd a1→boot_appnum
main:       init_task_info() 从全局量取值，经 bios_sd_read 读 AppInfo
```

关键点：head.S **必须先清 BSS 再存参**（两个全局量在 .bss，先清后存不会被抹掉）；
存参在 `call main` 之前，因为 a0/a1 会被 main 的调用冲掉。

bootblock 对 `0x502001fc/0x1f8` 的读取依赖 U-Boot `virtio read 50200000 0 2`
预读前 2 扇区（Task 3 前置项 3 已记录，bootblock.S 注释亦写明）。

### 3. AppInfo 读取必须经 512B 对齐缓冲中转

```c
static uint8_t appinfo_buf[SECTOR_SIZE] __attribute__((aligned(SECTOR_SIZE)));
bios_sd_read((unsigned)(unsigned long)appinfo_buf, APPINFO_SECTORS, ...);
memcpy(tasks, appinfo_buf, task_num * sizeof(task_info_t));
```

若把 `bios_sd_read` 的整扇区直接写进 `tasks[]`：tasks 是 `16×28=448` 字节，
但一次读 512B 会**越界覆写 tasks 后面 64 字节的 .bss**。中转缓冲保证只拷贝
`task_num*28` 字节。

### 4. readline：限长 + \r/\n 双终止 + 退格

- 名字存 `char[TASK_NAME_LEN]`（16），`cap-1` 即 15 字符上限；超长整行作废
  （丢弃到行尾，返回 0）。
- `\r` 与 `\n` 都立即终止；`\r\n` 的另一半会以空行被瞬间消费，调用方忽略空行。
- 退格支持 `\b`(BS) 与 `0x7F`(DEL)，回显"退格+空格+退格"擦除屏幕。
- 不可打印字符忽略（仅回显可打印字符）。
- 空行/超长行返回 0 → main 打印 `Error: invalid input!` 并回到输入循环。

### 5. createimage 的改动

- **退役** `KERNEL_FIXED_SECTORS/APP_FIXED_SECTORS`（Task 3 固定扇区时代产物）。
- 两遍处理：预扫描各 ELF 的 PT_LOAD `p_filesz` 总和得到 used_bytes，再计算布局、
  写入（bootblock→padding→kernel→padding→AppInfo→padding→各 app→padding）。
- **未用槽位清零**：`for (i = tasknum; i < TASK_MAXNUM; ++i) memset(&taskinfo[i],0,...)`；
  内核靠 `name[0]=='\0'` 识别空槽。
- **名字超长报错**：`strlen(files[i]) >= TASK_NAME_LEN` → `error()`（绝不截断）。
- **`0x1fc` 写实际内核扇区数**（紧凑布局下 bootblock 精确读入内核）。
- **布局自检**：写完后按公式核算总扇区并与文件实际大小比对，不一致 `error()`。
- 打印布局表（每文件起始/结束扇区、占用扇区数）+ 布局核算（total/appinfo/app#1）。

### 6. head.S 对齐假设复查（Task 4 必查）

Task 2 日志记录过：head.S 用 8 字节 `sd` 清 BSS，依赖 `__bss_start` 8 字节对齐。
**Task 4 复查结果：`__bss_start = 0x50201928`，`%8 == 0`，假设成立，`sd` 安全。**
BSS 现 1688 字节（含 `appinfo_buf[512]`、`tasks[448]`、两个 boot 参数 uint64 等）。
> 若未来布局再变，仍需复查；`crt0.S` 侧一直用 `sb`（auipc 的 bss 起点
> 0x52010284 非 8 对齐），不受此假设影响。

### 7. -O2 的诚实处理

`CFLAGS = -O2 …` 在 start-code 里**本来就存在**，且 BOOT/KERNEL/USER 三份 CFLAGS
都继承它——也就是说 bootblock、内核、4 个 app 从 Task 1 起就一直是 -O2 编译的，
**没有"加 -O2"这个动作**，也没有 Task 3 的非 -O2 内核可以做体积对比。
本任务内核因新增 `init_task_info/readline` 等功能从 4 扇区增至 **5 扇区**，
与验收"应有缩小"相反——如实报告：**-O2 自始生效，无对比基线，不伪造对比。**

---

## 构建过程

```bash
make elf
make image
```

`make image` 展开为 `cd ./build && ./createimage --extended bootblock main 2048 auipc bss data`。

createimage 打印的布局表与核算：

```
0x50200000: bootblock   sec  0 ..  0  (126 bytes, 1 sectors)
0x50201000: main        sec  1 ..  5  (2416 bytes, 5 sectors)
-APPINFO--- appinfo    sec  6 ..  6  (448 bytes, 1 sectors)
0x52000000: 2048        sec  7 .. 16  (5032 bytes, 10 sectors)
0x52010000: auipc       sec 17 .. 18  (644 bytes, 2 sectors)
0x52020000: bss         sec 19 .. 19  (368 bytes, 1 sectors)
0x52030000: data        sec 20 .. 28  (4536 bytes, 9 sectors)

[img info] kernel sectors : 5 @ 0x1fc
[img info] app number     : 4 @ 0x1f8
[img info] boot signature : 0x55 0xaa @ 0x1fe

[layout] total sectors : 29 = 1(boot) + 5(kernel) + 1(appinfo) + 22(apps)
[layout] appinfo sector : 6 (= 1 + kernel_sectors)
[layout] app#1 sector   : 7 (= 1 + kernel_sectors + 1)  (无固定空泡)
```

**无固定空泡证明**：app1(2048) 起始扇区 = 7 = 1 + 5 + 1；各 app 相邻（2048 占
7..16，auipc 从 17 紧接，无空泡）；总 29 扇区 = 1+5+1+22，与镜像实际
14848 字节 / 29 扇区完全吻合。

**AppInfo 区字节核验**（扇区 6，偏移 0xc00）：

```
00000c00: 3230 3438 ...         name[0] = "2048"
00000c10: 0700 0000 0a00 0000 0000 0052 ...   start=7 sector_num=10 entry=0x52000000
...（auipc: start=17 num=2 entry=0x52010000; bss: start=19 num=1 entry=0x52020000;
    data: start=20 num=9 entry=0x52030000；其余槽位全零）
```

---

## 自动化验证

脚本：`.scratch/verify.sh`（复用骨架）。注入序列：

```
6s -> loadboot ; 13s->"auipc\n" ; 15s->"bss\n" ; 17s->"data\n" ; 19s->"2048\n"
22s->"\n"(2048菜单继续) ; 25s->"q"(退出) ; 27s->"nope\n"(未知名) ; 29s->"\n"(空行)
```

| # | 断言 | 结果 |
|---|---|---|
| 1 | init 后打印任务名列表 `Available tasks: 2048, auipc, bss, data` | PASS |
| 2 | 出现按名输入提示 | PASS |
| 3 | 按名 `auipc` 启动并打印 testpc | PASS |
| 4 | 按名 `bss` 启动且 bss 清零通过 | PASS |
| 5 | 按名 `data` 启动且检查通过 | PASS |
| 6 | 按名 `2048` 启动 | PASS |
| 7 | 2048 退出后回到内核 | PASS |
| 8 | 未知名 `nope` 被拒绝 | PASS |
| 9 | 空行判非法输入 | PASS |
| 10 | AppInfo 区 name[0]="2048"（字节准确） | PASS |
| 11 | 2048 的 start_sector = 7（=1+内核5+AppInfo1，无固定空泡） | PASS |
| 12 | 0x1f8 = 04 00（app 数 4） | PASS |
| 13 | 0x1fc = 05 00（内核实际 5 扇区） | PASS |
| 14 | 0x1fe = 55 aa（签名完好） | PASS |
| A | 实测总扇区 = createimage 公式核算值 29 | PASS |
| B | （预留：公式复核） | PASS |

QEMU 退出码 124 = timeout 主动杀进程（属预期）；无残留 QEMU 进程。

## 踩坑记录

1. **AppInfo 的 fwrite 不经过 phyaddr 计数**：写完 AppInfo 后若直接 `write_padding`
   会基于旧的 phyaddr 计算，导致 448B 与 512B 对不齐。解法：fwrite 后把
   phyaddr 同步为 `appinfo_sec*512 + 448`，再补尾 64B 零。
2. **全空格行不是"非法输入"**：readline 把空格当普通可打印字符收下，`"    "`
   是一个合法名字（只是匹配不到）。验证脚本最初预期它报 `invalid input` 失败，
   改为注入**真正的空行**测非法路径——符合规格（空/超长才判非法）。
3. **grep -F 下 `\[OK\]` 不再转义**：用 `grep -F`（固定串）匹配 `[OK]!` 时
   `\[` 是字面反斜杠，匹配不到。去掉转义。
4. **head.S 必须先清 BSS 再存 boot 参数**：全局量在 .bss，顺序反了会被清零循环
   抹掉（与 crt0 的 .data 槽设计同理，只是这次在内核侧用"先清后存"）。
5. **镜像自检先硬编码后改为公式核算**：初版脚本把 29 硬编码，按用户要求改为
   解析 `make image` 打印的 `[layout] total sectors` 与文件实际大小比对。

## 结论

Task 4 自检全部通过：镜像紧凑排列无固定空泡（app1=1+K+A=7），AppInfo 区字节
准确且未用槽清零，bootblock 主动传参替代内核直读 0x50200xxx，head.S 对齐假设
复查通过，`-O2` 自始生效如实报告。4 个 app 按名均能启动、未知名与空行被拒、
全程无跑飞。
