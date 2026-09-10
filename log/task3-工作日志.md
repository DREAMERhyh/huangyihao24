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

### 6. ⚠️ 跳转 app 必须让返回地址进 `ra`（本任务最大的坑）

GCC 把 `((void(*)(void))(uintptr_t)entry)()` 编译成 **`jalr a0`**——
返回地址写进了 **a0** 而不是 ra，crt0 靠 ra 跳回内核就会回不来。

而写成 `jalr ra, %0` 也不行：本工具链的 GAS 对 `rd=x1(ra)` 的 `jalr`
**一律错编成 rd=a0**（`rd=x2/x3/x10/s0/t0` 都是对的，逐一实测过）。

最终解法——用局部标签让汇编器算好返回地址，不写魔数：

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
502011c6: jalr  a0,a5     # 跳 app 入口
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

1. **`jalr ra, x` 在本工具链下被错编成 rd=a0**（见 §6）。用
   `la ra, 1f` + `jalr a0, %0` + `1:` 绕过，返回地址仍正确落进 ra。
2. **`((fn)entry)()` 生成 `jalr a0`**，返回地址进 a0 而非 ra，配合坑 1 差点让整个
   "app 返回内核"链路失败。逐条反汇编才发现，不是猜的。
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
