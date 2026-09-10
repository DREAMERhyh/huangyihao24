# 任务3 工作日志：自建 createimage + 按 taskid 加载运行用户程序

## 任务书原文对照（引用位置）

| 决策点 | 任务书原文（§5 Task 3） |
|---|---|
| 固定扇区数 | "默认 `KERNEL_FIXED_SECTORS=32`、`APP_FIXED_SECTORS=15`，做成宏" |
| 镜像信息 | "实现 `write_img_info()`，把'内核扇区数 + 用户程序数目'写进镜像固定位置。约定（推荐，可在汇报中提出替代方案）：内核扇区数 2 字节 @镜像偏移 `0x1fc`（沿用官方约定），app 数目 2 字节 @`0x1fe`" |
| 布局打印 | "createimage 必须打印每个程序的'实际占用扇区数/分配扇区数'对照表，实际>分配时报错退出" |
| loader | "实现 `load_task_img(taskid)`：从 SD 卡把第 taskid 个 app 的固定扇区读到 `0x52000000 + taskid*0x10000`，然后跳到该入口" |
| crt0 | "设 app 自己的栈（建议设在该 app 64KB 窗口顶部向下生长，如 `0x52000000+(taskid+1)*0x10000-16`，可调整但需说明）、清 app 的 bss（边界方案以 Phase 0 勘察结论为准）、调用 app main、app 返回后跳回内核" |
| main | "打印欢迎信息后交互式读入 taskid，调用 `load_task_img`；taskid 必须 < app 总数，越界输入要提示错误并回到输入循环，不得跑飞" |
| 验收 | "依次输入每个合法 taskid，对应 app 正确运行输出；输入越界 id 被拒绝；全程无跑飞" |
| 构建 | "从本任务起允许并应当使用 `make all` / `make image`" |

**"main 跑哪些任务"**：Makefile 的 `SRC_USER = $(wildcard $(DIR_TEST_PROJ)/*.c)`，
即 `test/test_project1/*.c` 全部按字母序参与，因此合法 taskid 是 `0..3`：
`0=2048`、`1=auipc`、`2=bss`、`3=data`。Task 3 未增删任何 app 源文件。

---

## 与任务书/先前指令的冲突点（按要求显式指出）

1. **app 数目的镜像偏移**：任务书写 `@0x1fe`，但官方签名 `55 aa` 就占用
   `0x1fe/0x1ff`，写上去会破坏签名。Phase 0 已由用户裁决改为 **`@0x1f8`**，
   本次照此实现。
2. **app 栈位置**：任务书建议"该 app 64KB 窗口顶部向下生长"，用户裁决改为
   **所有 app 共享栈顶 `0x50400000`**（内核栈独享 0x50400000–0x50500000）。
   任务书写明"可调整但需说明"，故采用共享栈并在本文说明理由。
3. **入口地址来源**：任务书只说"跳到该入口"，未规定从哪取。本次**用窗口基址论证**
   而不是从镜像里读 `e_entry`（理由见下），Task 4 改按名加载时 `e_entry` 会进
   `task_info_t`。
4. `kernel/loader/loader.c` 第 9–12 行的 TODO 是**注释块**，不是可执行代码；
   原样保留并填充实现（与 Task 2 的 head.S 情况同类）。

---

## 改动清单

| 文件 | 改动 |
|---|---|
| `tools/createimage.c` | 新增 `KERNEL_FIXED_SECTORS`/`APP_FIXED_SECTORS`/`IMG_APP_NUM_LOC` 宏；padding 改为固定扇区数并打印对照表、超限报错；实现 `write_img_info()` |
| `include/os/task.h` | 新增同名两个固定扇区数宏 + `IMG_APP_NUM_LOC`（注明必须与 createimage.c 一致） |
| `kernel/loader/loader.c` | 实现 `load_task_img(taskid)` |
| `arch/riscv/crt0/crt0.S` | 实现 app 的 C 运行环境与返回路径 |
| `init/main.c` | 新增 `task_num`、`init_task_num()`、`read_taskid()`；把回显循环改成 taskid 分发循环 |

未动：`Makefile`（Task 3 不改；`-O2` 已在 `CFLAGS` 里）、`riscv.lds`、`biosdef.h`、
`arch/riscv/bios/`、`bootblock.S`（读扇区数的逻辑保持从 `0x1fc`，天然兼容）、
`arch/riscv/kernel/head.S`、4 个 app 源文件、`tools/createimage` 官方二进制。

## 关键设计决策

### 1. 镜像布局与扇区号

```
扇区 0                                  bootblock
扇区 1 .. 32                            内核（KERNEL_FIXED_SECTORS=32）
扇区 33..47                             2048
扇区 48..62                             auipc
扇区 63..77                             bss
扇区 78..92                             data
共 93 扇区 = 47616 字节
```

loader 的公式：`block = 1 + KERNEL_FIXED_SECTORS + taskid * APP_FIXED_SECTORS`。
注意那个 `+1` 是给 bootblock 的扇区 0 让位，漏掉会把内核最后 31 个扇区当成 app 0。

### 2. `write_img_info()` 写什么

```
0x1f8 : 用户程序数目（2 字节，小端）   ← tasknum = nfiles - 2 = 4
0x1fc : 内核占用的扇区数（2 字节，小端）← NBYTES2SEC(nbytes_kernel) = 4
0x1fe : 官方签名 0x55 0xaa
```

**内核扇区数写"实际占用"而不是"分配的 32"**：bootblock 按这个值精确读入内核
（Task 2 的 `lh` + 循环读），若写 32 会把 12KB 的 padding 也读进内存。
写实际值在 Task 4 改成紧凑排列后同样成立，bootblock 一行都不用改。

### 3. 入口地址用窗口基址（而不是从镜像读 e_entry）

三条依据：
1. `riscv.lds` 里 `ENTRY(_start)`，且 `.text` 段第一个输入段是 `*(.entry_function)`；
2. `crt0.S` 的 `_start` 定义在 `.entry_function` 段，所以排在 app 自己的代码之前；
3. Makefile 用 `-Wl,--defsym=TEXT_START=<窗口基址>` 逐个 app 赋值（编完一个自增 0x10000）。

因此每个 app 的 `_start` 恰好在自己的窗口基址。实测 `readelf` 四个 app 的
`e_entry` 与窗口基址完全一致：`2048=0x52000000 auipc=0x52010000 bss=0x52020000 data=0x52030000`。

Task 3 的镜像只存扇区数和 app 数目，不带 per-app 信息，所以用窗口基址最省；
Task 4 设计 `task_info_t` 时会把 `e_entry` 存进镜像。

### 4. crt0.S：内核现场用 `.data` 槽保存

```asm
la    t2, saved_ra
sd    ra, 0(t2)
la    t3, saved_sp
sd    sp, 0(t3)
li    sp, APP_STACK_TOP          # 0x50400000
... sb 逐字节清 BSS ...
call  main
la    t2, saved_sp
ld    sp, 0(t2)
la    t3, saved_ra
ld    ra, 0(t3)
jr    ra
```

- **为什么用 `.data` 固定槽**：不能借 `a0`（app main 会冲掉），不能借 `s0-s11`
  （被调用者按 ABI 不能破坏内核里活着的 s 寄存器）。
- **为什么槽必须在 `.data` 而不是 `.bss`**：否则紧接着的 BSS 清零循环会把自己
  刚存进去的值抹掉。
- **全程只用 t/a 寄存器**，不碰 s 寄存器。
- **BSS 清零必须用 `sb` 逐字节**：实测 `auipc` 的 `__bss_start = 0x52010284`
  （非 8 对齐）、bss 只有 4 字节，用 `sd` 会越界写坏后面 4 字节。
  （对比：内核的 `__bss_start = 0x502015b0` 是 8 对齐的，head.S 沿用 `sd`，
  该对齐假设已在 Task 2 日志记录、Task 4 复查。）
- **共享栈 `0x50400000`**：内核栈 `0x50500000` 向下生长占用
  0x50400000–0x50500000，两者不交叠；app 栈向下到 0x50200000 仍有约 2MB。
  所有 app 串行执行、不并发，共享一个栈安全。

### 5. main.c：taskid 分发

- `read_taskid()` 逐个读字符并回显，只把 `'0'..'9'` 当 taskid，其余（含回车）
  只回显不参与解析；`-1` 继续跳过（否则会打出乱码）。
- 越界判定 `id >= task_num`（`task_num` 从镜像 `0x1f8` 读），打印
  `Error: task id out of range!` 后 `continue` 回到输入循环。
- 加载失败判定 `entry == 0`（`load_task_img` 用 0 作错误哨兵，合法入口恒 ≥ 0x52000000）。
- 每轮循环都重新打印 `Please input a task id:`，app 返回后提示会再次出现。

### 6. 跳转 app 的返回地址设计（含勘误）

GCC 把 `((void(*)(void))(uintptr_t)entry)()` 编译成 **带链接的 jalr
（rd=ra）**，返回地址本来就会进 ra——早期误读 objdump 画风以为进了 a0，
见文末"附：前置项 1 jalr 根因复核"。

当前写法显式先置 ra 再做间接跳转（双保险，与 GCC 隐式行为完全等价），
且让"返回地址落在哪"在源码里一目了然：

```c
asm volatile(
    "la   ra, 1f\n\t"
    "jalr a0, %0\n\t"
    "1:\n\t"
    :
    : "r"(entry)
    : "ra", "a0", "memory"
);
```

实测内核生成：

```
502011be: auipc ra,0x0
502011c2: addi  ra,ra,12  # 502011ca
502011c6: jalr  a0,a5     # 跳 app 入口（ra 已显式指向 1:）
502011ca: j 50201146      # ← app 返回后落在这里，正好是循环回边
```

`1:` 落在循环回边上，所以 app 一返回就回到 `main` 的循环顶部重新打印提示。

## 构建过程

```bash
make elf
make image          # Task 3 起首次使用自建 createimage
```

`make image` 展开为 `cd build && ./createimage --extended bootblock main 2048 auipc bss data`。

布局对照表（createimage 打印）：

```
0x50200000: bootblock   actual  1 sector(s) / allocated  1 sector(s)
0x50201000: main        actual  4 sector(s) / allocated 32 sector(s)
0x52000000: 2048        actual 10 sector(s) / allocated 15 sector(s)
0x52010000: auipc       actual  2 sector(s) / allocated 15 sector(s)
0x52020000: bss         actual  1 sector(s) / allocated 15 sector(s)
0x52030000: data        actual  9 sector(s) / allocated 15 sector(s)

[img info] kernel sectors : 4 @ 0x1fc
[img info] app number     : 4 @ 0x1f8
[img info] boot signature : 0x55 0xaa @ 0x1fe
```

镜像 `build/image` = 47616 字节 = **93 扇区**（= 1 + 32 + 4×15，与布局吻合）。
尾部实测：`000001f0: … 0400 0000 0400 55aa`
→ `0x1f8 = 0x0004`（app 数 4）、`0x1fc = 0x0004`（内核 4 扇区）、`0x1fe = 55 aa`。

自检项：无一项出现 `actual > allocated`；若出现，`createimage` 会
`error()` 直接退出（`exit(EXIT_FAILURE)`），不会产出错误镜像。

## 自动化验证

脚本：`.scratch/verify.sh`（已 gitignore，保留为通用骨架）。

注入序列（共 40s，`timeout` 兜底）：

```
6s -> loadboot
11s -> "1"    auipc
13s -> "2"    bss
15s -> "3"    data
17s -> "0"    2048
20s -> "\n"   越过 2048 菜单
23s -> "q"    退出 2048
25s -> "4"    越界
27s -> "x"    非数字
```

QEMU 日志关键节选：

```
=> loadboot^M
It's Dreamerhyh's bootloader...^M
^MHello OS!^M
^Mbss check: t version: 2^M
^MPlease input a task id: 1[auipc] Info: testpc = 0x52010064^M
Please input a task id: 2[bss] Info: passed bss check!^M
Please input a task id: 3[data] Info: checking data!^M
[data] Info: checking data --- [OK]!^M
Please input a task id: 0 2048 - Game^M
...
 [2048]: Exit 2048 game ...^M
Please input a task id: 4Error: task id out of range!^M
^MPlease input a task id: x
```

| # | 预期 | 实际 | 结果 |
|---|---|---|---|
| 1 | bootblock 标志串（Task 1 回归） | 第 24 行 | PASS |
| 2 | `bss check: t`（Task 2 回归） | 第 26 行 | PASS |
| 3 | 出现 taskid 输入提示 | 有 | PASS |
| 4 | taskid=1 启动 auipc 打印 testpc | 第 27 行 `0x52010064`（落在 auipc 窗口内） | PASS |
| 5 | taskid=2 启动 bss 且通过检查 | 第 28 行（证明 crt0 的 `sb` 清 BSS 生效） | PASS |
| 6 | taskid=3 启动 data 且检查通过 | 第 30 行 | PASS |
| 7 | taskid=0 启动 2048 | 第 31 行 | PASS |
| 8 | 2048 退出（app main 返回 → crt0 跳回内核） | 第 52 行 | PASS |
| 9 | taskid=4 越界被拒绝 | 第 53 行 | PASS |
| 10 | 顺序 24<26<27<28<30<31<52<53 | 成立 | PASS |
| 11 | 越界报错后提示再次出现（未跑飞） | 成立 | PASS |
| 12 | 提示共出现 ≥5 次（多次往返） | 6 次 | PASS |
| 13 | 越界报错恰 1 次（`x` 未误报） | 1 次 | PASS |
| 14 | 非数字 `x` 被回显 | 报错行之后可见 | PASS |

QEMU 退出码 124 = `timeout` 主动杀进程（内核循环不会自行退出，属预期）；
收尾确认无残留 QEMU 进程。

## 踩坑记录

> 坑 1、2 的根因描述**【勘误】**：详见文末"附：前置项 1 jalr 根因复核"。
> 结论：`jalr ra, a0` 与 GCC 函数指针调用的编码都是 rd=x1（返回地址进 ra），
> 并不存在"返回地址进 a0"或"GAS 错编"的问题；当时是把 objdump 的省略画风
> （rd=ra 时显示为 `jalr a0`）误读成了指令语义。`la ra, 1f` 属双保险，保留。

1. **objdump 的 `jalr` 画风会误导人**：rd=ra 时它显示 `jalr a0`（省略 ra），
   与 rd=a0 时显示 `jalr a0, a0` 混在一起看很容易误判。判断真实编码必须
   用 `python` 解 `(enc>>7)&0x1f` 或看 `xxd` 原始字节。
2. **函数指针调用的编码形态与上下文有关**：GCC 在"调用后无后继代码"时用
   `jr`（不链接，rd=x0）；在"调用后仍有代码"时用带链接的 jalr（rd=ra）。
   本任务内核在 while 循环里调用，返回地址确实进 ra，链路本来畅通。
3. **GAS 不会为访存指令自动物化地址**（两次踩到）：
   Task 2 的 `lh t2, 0(os_size_loc)`，本任务的 `sd ra, saved_ra`。
   都必须先 `la`/`li` 把地址算进寄存器再用。
4. **`\L1` 没被 GCC 展开**：inline asm 里写 `\L1` 会原样传给汇编器并报错
   `illegal operands 'la ra,\L1'`。改用数字局部标签 `1f` / `1:` 正常
   （本例在 `while(1)` 里、GCC 不会复制该 asm，标签不会重复定义）。
5. **`auipc` 的 `__bss_start = 0x52010284` 非 8 对齐**，bss 仅 4 字节 →
   crt0 必须用 `sb`。这是用户指出的点，实测反汇编确认。
6. **`make elf` 会重编 crt0.o 并重链 4 个 app**，app 的地址/符号随之变化；
   判断 crt0 行为要看具体某个 app 的反汇编，不能只看 crt0.o。

## 结论

Task 3 自检全部通过：自建 createimage 按固定扇区数排布镜像并写出信息字段，
`load_task_img` 用 taskid 算扇区号读入窗口基址，crt0 为 app 建立 C 环境
（共享栈 0x50400000 + `sb` 逐字节清 BSS + `.data` 槽保存内核 sp/ra）并能在
app 返回后跳回内核，`main` 的 taskid 分发对 4 个 app 全部生效、越界被拒、
非数字被忽略、全程无跑飞。

---

# 附：验收前三个前置项复核（Task 4 之前关闭）

## 前置项 1：jalr 根因复核（结论：早期诊断是误读，特此勘误）

### 原日志的说法（**错误，已更正**）

> "GCC 会把 `((fn)entry)()` 编译成 `jalr a0`，返回地址被写进 a0 而不是 ra"
> "本工具链的 GAS 对 rd=x1(ra) 的 jalr 一律错编成 rd=a0"

### 复核实验（最小复现，/tmp/repro/jalr-repro*.S, orig.c）

```asm
f1:  jalr  ra, 0(a0)    → 0x000500e7  → 手工解码 rd=x1(ra), rs1=x10(a0)  ✓ 正确
f2:  jalr  x1, 0(a0)    → 0x000500e7  → rd=x1   ✓
f4:  jalr  s0, a0       → 0x00050467  → rd=x8(s0), rs1=a0                ✓ 正确
f5:  jalr  a0, a0       → 0x00050567  → rd=a0, rs1=a0（这才是 GCC)
                                      （的产物形态之一，但只出现在无需返回处）
```

| 解码 | rd | 说明 |
|---|---|---|
| `0x000500e7` | **x1 (ra)** | `jalr ra, a0` 的正确编码；objdump 把 rd=ra 简写成 `jalr a0`，早期据此误判成 rd=a0 |
| `0x00050467` | x8 (s0) | `jalr s0, a0`，正常 |
| `0x00050567` | x10 (a0) | `jalr a0, a0`，只有显式写成 rd=a0 才是 a0 |

C 侧最小复现（与内核原始写法相同的模式）：

```c
while (1) { if (entry == 0) continue; ((void(*)(void))(uintptr_t)entry)(); }
```

GCC -O2 生成 `c.jalr rs`（压缩，`c.jalr rs1` 的 ISA 语义就是
`jalr x1, 0(rs1)`，**链接到 ra**），或 4 字节 `jalr ra, rs`。两种都正确。

### 结论

1. **"GAS 错编 rd=x1 → rd=a0" 不成立**：`jalr ra, a0` 编码就是
   `0x000500e7`（rd=x1）。之前判定为 `jalr a0, a0` 是**把 objdump 的
   省略画风当成了指令语义**。
2. **"GCC 把返回地址写进 a0" 也不成立**：GCC 的函数指针调用（带后继代码时）
   生成的是带链接的 jalr，返回地址进 ra。原始 `((fn)entry)()` 写法本来就正确。
3. **替代假设（".bss 槽被清零"）不适用**：槽位从实现之初就在 `.data`，
   从未有 ".bss 中保存再被清零"的版本被构建或运行过；不存在运行时故障。
4. **当前代码为何保留 `la ra, 1f`**：与 GCC 隐式行为完全等价（都把返回地址
   指向 `1:` 即循环回边），是**双保险**，代码不改，仅修正注释与根因描述。

## 前置项 2：crt0 未使用 s0–s11（内核 s 寄存器存活的手工保证）

链接后 `auipc` app 的 crt0 反汇编（`objdump -d build/auipc` 的 `_start` 到 `main`）：

```
_start:   auipc t2,0; addi t2,t2,#saved_ra; sd ra,0(t2)
          auipc t3,0; addi t3,t3,#saved_sp; sd sp,0(t3)
          lui   sp,0x50400                    # 共享 app 栈
          auipc t0,0; addi t0,t0,#__bss_start
          auipc t1,0; addi t1,t1,#__BSS_END__
clear_loop: bge t0,t1,clear_done; sb zero,0(t0); addi t0,t0,1; j clear_loop
clear_done: jal main
          auipc t2,0; ...; ld sp,#saved_sp
          auipc t3,0; ...; ld ra,#saved_ra
          ret                                  # = jalr x0, ra
```

使用的寄存器白名单：**`ra, sp, t0, t1, t2, t3`，不含任何 `s0–s11`**。
这是内核 `main()` 里跨 app 存活的 s 寄存器（`-O2` 会把 `task_num`、提示串
地址等 hoist 进 s 寄存器）不被 app 破坏的**唯一手工保证**，已核验。

## 前置项 3：`init_task_num()` 的读取机制与隐藏依赖

```c
task_num = *(const uint16_t *)IMG_APP_NUM_LOC;   // IMG_APP_NUM_LOC = 0x502001f8
```

- **地址**：`0x502001f8`，落在第 0 扇区（bootblock 扇区）尾部偏移 `0x1f8`，
  由自建 `createimage` 的 `write_img_info()` 写入 app 总数。
- **机制**：内核**直接读内存**，不经过 `bios_sd_read`。
- **隐藏依赖**：`0x502001f8` 之所以在内存里，是因为 U-Boot 的
  `CONFIG_BOOTCOMMAND = "run distro_bootcmd\n virtio read 50200000 0 2\n"`
  在 autoboot 时把镜像前 **2 个扇区**读进 `0x50200000–0x502003ff`，
  `0x502001f8` 恰好落在这 2 扇区内。也就是说**内核行为借用了 U-Boot 预读的
  副作用**——若只预读 1 扇区、或信息字段挪出第 0 扇区，该读法立即失效。
  这个依赖只在 Task 3 的镜像信息区成立；**Task 4 按任务书写明要求改为
  bootblock 读 app-info 后主动传参给内核**，替代该直接读法。
