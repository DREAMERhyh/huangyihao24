# 任务2 工作日志：加载内核 + C 运行环境 + 键盘回显

## 目标

1. `bootblock.S`：用 `lh` 从镜像 `0x1fc` 读内核扇区数，循环调用 `bios_sd_read`
   把内核从 SD 卡第 1 扇区读到 `0x50201000`，然后跳过去；
2. `head.S`：清 BSS（`__bss_start` → `__BSS_END__`）、设 `sp = 0x50500000`、跳 `main`；
3. `main.c`：`bios_getchar()` 读键，返回 `-1` 跳过不输出，否则回显。

## 改动

| 文件 | 改动 |
|---|---|
| `arch/riscv/boot/bootblock.S` | 填充两处 `[p1-task2]`：扇区数读取 + 逐扇区 `bios_sd_read` 循环 + `jal kernel` |
| `arch/riscv/kernel/head.S` | 填充两处 `[p1-task2]`：8 字节步长清 BSS 循环 + 设栈指针 + `call main` |
| `init/main.c` | 把加载任务的 TODO 换成键盘回显循环（保留 task3/task4 的 TODO 标注）；**删除了因此变成不可达的死代码**（原来的 `while(1){ wfi; }`） |

`tools/createimage.c`、`kernel/loader/loader.c`、`arch/riscv/crt0/crt0.S`、`Makefile`、
`riscv.lds` 全部未动。仍用手写 `cd build && ../createimage --extended bootblock main`
（官方二进制）制作镜像，未执行 `make image`。

## 关键设计决策

### bootblock：寄存器分配

```
t3 = 0x502001fc   扇区数字段地址（临时，读一次即弃）
t2 = 剩余扇区数
t0 = 当前扇区号（从 1 开始）
t1 = 当前目标地址（从 0x50201000 开始）
a0/a1/a2 = BIOS 三参数，a7 = 功能号
```

`bios_sd_read` 经 `ecall` 进 U-Boot、返回时只写 `a0`，`t0~t3` 保持完好，
所以循环变量全部放 `t` 系列无需保存/恢复。

### 为什么按扇区循环、而不是一次读 4 个

`bios_sd_read(addr, num_blocks, block_id)` 本身支持一次读多个扇区，理论上可以一次调用读完。
但按任务书要求用**循环逐扇区读取**，理由是：① 与规格一致；② 扇区数完全由镜像里的
`0x1fc` 决定、代码不绑定具体值，改扇区数无需改代码；③ 与 Task 3 `loader.c` 按 app 读
的逻辑形态一致，便于复用。代价是 4 次 `ecall`（每段内核一次陷入 U-Boot），
在启动阶段可忽略。

### `lh` 不能直接用绝对地址做基址

第一版写的是 `lh t2, 0(os_size_loc)`，汇编器报
`Error: illegal operands 'lh t2,0(os_size_loc)'` —— `lh` 需要"基址寄存器 + 偏移"，
GAS 不会为 `lh` 自动物化绝对地址。改为先 `li t3, os_size_loc` 再 `lh t2, 0(t3)`，多一条指令、
多占一个寄存器，但正确且可读。

### 扇区数不硬编码

`lh t2, 0(t3)` 读出的是镜像里 `createimage` 写入的值（当前为 4）。
bootblock 里**没有任何地方写死 4**，Task 3 改成固定扇区数、Task 4 改成紧凑排列时，
bootblock 这段代码一个字都不用改。

### head.S：BSS 起止用符号而不是硬编码

`__bss_start` / `__BSS_END__` 由 `riscv.lds` 按内核实际段布局赋值，
`la` 取出即可。这样 Task 3/4 结构体变大、BSS 变长时自动适配。

用 8 字节 `sd` 步长清零：实测 `__bss_start = 0x502015b0` 是 8 字节对齐的，
因此不存在非对齐访问问题。

### sp = 0x50500000

`KERNEL_STACK` 宏已给好，16 字节对齐（`addi sp,sp,-80` 的栈帧分配不会踩到边界）。
该地址在内核允许区间 0x50200000–0x51FFFFFF 之内，且高于内核代码/数据
（内核只到 0x502015e8）与跳转表（0x51ffff00）。

### main.c：`-1` 必须跳过

`bios_getchar()` 无输入时返回 `-1`（Phase 0 已从 U-Boot 源码确证：
`__serial_cgetc()` 在 `getc()` 返回 `-EAGAIN` 时 `return -1`）。
若直接回显，`-1` 会被当成字符 `0xFF` 打出去造成乱码，所以 `ch == -1` 时 `continue`。

## 构建过程

```bash
make elf
chmod +x createimage && cd build && ../createimage --extended bootblock main
```

bootblock 实际命令与 Task 1 相同；内核命令：

```
riscv64-unknown-linux-gnu-gcc -O2 -std=gnu11 -fno-builtin -nostdlib -nostdinc -Wall \
  -mcmodel=medany -ggdb3 -I./arch/riscv/include -Iinclude \
  -Wl,--defsym=TEXT_START=0x50201000 -T riscv.lds -o build/main \
  ./arch/riscv/kernel/head.S ./init/main.c ./arch/riscv/bios/common.c \
  ./kernel/loader/loader.c ./libs/string.c
```

### bootblock 反汇编（`objdump -d build/bootblock`）

```
50200000: fence
50200004: auipc a0,0x0        50200008: addi a0,a0,74     # a0 = 0x5020004e <msg>
5020000c: li a7,9
5020000e: auipc ra,0xfff50    50200012: jalr -14(ra)      # -> 0x50150000, ra = 0x50200016
50200016: lui t3,0x50200      5020001a: addiw t3,t3,508   # t3 = 0x502001fc
5020001e: lh  t2,0(t3)        # t2 = 内核扇区数
50200022: li t0,1             # 起始扇区号
50200024: lui t1,0x50201      # t1 = 0x50201000

read_loop:
50200028: beqz t2,read_done
5020002c: mv a0,t1
5020002e: li a1,1
50200030: mv a2,t0
50200032: li a7,11
50200034: auipc ra,0xfff50    50200038: jalr -52(ra)      # -> 0x50150000, ra = 0x5020003c
5020003c: addi t1,t1,512
50200040: addi t0,t0,1
50200042: addi t2,t2,-1
50200044: j read_loop

read_done:
50200046: jal 50201000 <kernel>     # 单条 jal，偏移 +0xfff0

stop:
5020004a: j stop
```

### head.S 反汇编

```
50201000: csrw sie,zero
50201004: csrw sip,zero
50201008: auipc t0,0x0   5020100c: addi t0,t0,1448   # t0 = 0x502015b0 <__bss_start>
50201010: auipc t1,0x0   50201014: addi t1,t1,1496   # t1 = 0x502015e8 <__BSS_END__>
clear_loop:
50201018: bge t0,t1,clear_done
5020101c: sd  zero,0(t0)
50201020: addi t0,t0,8
50201022: j clear_loop
clear_done:
50201024: lui sp,0x50500        # sp = 0x50500000
50201028: jal 50201032 <main>
```

### 体积与扇区占用

| 产物 | PT_LOAD filesz | memsz | 说明 |
|---|---|---|---|
| bootblock | 0x70（112 字节） | 0x70 | Task 1 是 0x3c（60 字节），仍远低于 512 字节上限 |
| 内核 main | 0x5b0 | 0x5e8 | BSS = `0x502015b0 → 0x502015e8` = 56 字节 |

官方 createimage 输出（**这是 Task 3 定固定扇区数要用的实测值**）：

```
0x50201000: main
	segment 1  offset 0x1000  vaddr 0x50201000  filesz 0x05b0  memsz 0x05e8
		writing 0x05e8 bytes
		padding up to 0x0800
	segment 2  offset 0x1384  vaddr 0x50201384  filesz 0x0094  memsz 0x0094
		writing 0x0094 bytes
		padding up to 0x0a00
os_size: 4 sectors
```

→ **内核实际占 4 个扇区**，镜像共 5 扇区 2560 字节。Task 3 的
`KERNEL_FIXED_SECTORS` 取 32（题目默认）远大于 4，足够。

镜像尾部实测：`000001f0: … 0000 0400 55aa` → `os_size = 4`、签名 `55 aa` 完好。

## 自动化验证

脚本：`.scratch/verify.sh`（已 gitignore，按用户要求保留为通用骨架）。
流程复用 Task 1 那套：清理残留 QEMU → 起 QEMU 并按时序注入 → 输出落日志 →
固定串断言 + 行号比对顺序 → 收尾清进程。只替换了注入序列与断言。

注入序列：`sleep 6s` → `loadboot` → `sleep 3s` → 注入 `hello` → `sleep 6s`，整体 `timeout 25s`。

QEMU 日志关键节选：

```
=> loadboot^M
It's Dreamerhyh's bootloader...^M
^MHello OS!^M
^Mbss check: t version: 2^M
^Mhello^M
```

| # | 预期 | 实际 | 结果 |
|---|---|---|---|
| 1 | bootblock 标志串仍在（Task 1 回归） | 日志第 24 行 | PASS |
| 2 | 出现 `Hello OS!`（内核被正确加载运行） | 第 25 行 | PASS |
| 3 | 出现 `bss check: t version: 2`（清 BSS 生效） | 第 26 行 | PASS |
| 4 | 注入的 `hello` 被回显 | 第 27 行 | PASS |
| 5 | 顺序：loadboot < 标志串 < HelloOS! < bss检查 < 回显 | 23 < 24 < 25 < 26 < 27 | PASS |
| 6 | 日志中无 `0xFF` 字节（`-1` 未被当字符回显） | 无 | PASS |

**补充验证（证明回显是循环而非一次性）**：连续注入 `first`、`second` 两次，
两者都被回显：

```
26:^Mbss check: t version: 2^M
27:^Mfirst^M
28:second^M
```

QEMU 退出码 124 = `timeout` 主动杀进程（内核回显循环不会自行退出，属预期）。
收尾 `ps -C qemu-system-riscv64` 确认无残留进程，telnet 端口 45454/45455 均空闲。

## 踩坑记录

1. **`lh` 不能以绝对地址做基址**：`lh t2, 0(os_size_loc)` 报
   `Error: illegal operands`。必须先 `li t3, os_size_loc` 再 `lh t2, 0(t3)`。
2. **shell 工作目录跨命令持续**（第二次踩到）：上一条命令里的 `cd build` 会让
   后续所有相对路径都相对 `build/`，导致 `bash .scratch/verify.sh` 报
   `No such file or directory`。已再次 `cd` 到绝对路径。**建议每条命令都带绝对路径。**
3. **`pgrep -af qemu-system-riscv64` 会误报**：如果检查命令是内联写在一个长命令里，
   它的 bash 包装进程命令行本身含有该字符串，会被自己匹配到，误判成"有残留进程"。
   精确检查用 `ps -C qemu-system-riscv64`（按进程名匹配）+ `ss -ltn | grep 45454`（按端口）。
   `.scratch/verify.sh` 内部不受影响（脚本命令行是 `bash .scratch/verify.sh`，不含该串）。
4. **`tasks[16]` 被 `-O2` 优化掉了**：`nm build/main` 显示 `tasks` 与 `__BSS_END__`
   同址（0x502015e8），说明当前没人引用 `tasks`，其 16 字节没进 BSS。
   Task 4 真正使用 `tasks` 后它会自动回到 BSS、`__BSS_END__` 随之右移，
   而 `head.S` 用符号取址，无需改动。
5. 两份 RWX 段警告（bootblock 与 main）都无害。

## 结论

Task 2 自检全部通过：bootblock 用 `bios_sd_read` 完整加载了 4 扇区内核并跳转，
`head.S` 正确清 BSS 并建立 C 环境（栈指针 0x50500000），`main.c` 的键盘回显
循环稳定工作且正确跳过 `-1`。内核实际扇区数 = **4**，已记录供 Task 3 使用。
