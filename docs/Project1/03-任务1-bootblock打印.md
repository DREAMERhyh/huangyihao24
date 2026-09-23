# 03 任务1：bootblock 打印——第一段汇编

> **读完你能回答的 3 个问题**：① BIOS 打印一个字符串，寄存器 `a0` / `a7` 各放什么？
> ② 为什么用 `call`（jal）而不用 `j` 跳 BIOS？③ bootblock 为什么要停在死循环里？

> 本小节按 **Task 1 当时的 bootblock.S** 讲解（只有打印 + 死循环）。你看到的最新
> bootblock.S 是任务 1/2/4/5 叠加后的最终形态，小节末会说明后续变更。

## 1. 全景

```
上电 → U-Boot 把镜像前 2 扇区读到 0x50200000 → 输入 loadboot → CPU 在 0x50200000 开始执行
   │
   ▼
main:
   a0 = 字符串地址  →   a7 = 功能号 9(BIOS_PUTSTR)   →   跳 0x50150000
   │                     （0x50150000 是 ecall+ret，ecall 进 U-Boot，ret 回到下一条）
   ▼
stop:  j stop       （死循环，防止跑飞——打印完就停在这）
```

## 2. 文件骨架（Task 1 形态）

```asm
#include <asm/biosdef.h>       // 提供 BIOS_PUTSTR 等功能号

.equ os_size_loc, 0x502001fc   // 内核扇区数放在这个内存地址（Task 2 才用）
.equ kernel, 0x50201000        // 内核入口地址（Task 2 才用）
.equ bios_func_entry, 0x50150000

.text                          // 代码段
.global main                   // 导出 main，Makefile 的 -e main 靠它找入口

main:
	fence
	la    a0, msg              // a0 = 字符串的地址
	li    a7, BIOS_PUTSTR      // a7 = 9
	call  bios_func_entry      // 跳 BIOS 入口，同时自动把"返回地址"写进 ra
stop:
	j stop                     // 死循环：jump 到自身
	nop
.data                          // 数据段
msg: .string "It's Dreamerhyh's bootloader...\n\r"
```

## 3. 逐行：这行做什么 / 为什么 / 删掉会怎样

### `fence`
- 做什么：内存屏障，保证之前的内存/设备访问都完成。
- 为什么：上电后被跳进来，CPU 可能还没"看清"M 态 U-Boot 留下的状态；加一道 fence
  让后续访问有序。
- 删掉会怎样：本项目大概率也正常；但按规范这是一种防御性卫生，建议保留。

### `la a0, msg`
- 做什么：把 `msg` 这个符号的**内存地址**放进寄存器 a0。
- 为什么：BIOS 打印函数需要知道字符串在哪。`la` 是伪指令，会展开成
  `auipc`+`addi` 两条（下面反汇编能看到）。因为 bootblock 链接在 0x50200000，
  `msg` 实际在 0x5020001a。
- 删掉会怎样：a0 是脏值，BIOS 会去打印一块垃圾内存——输出乱串或直接 crash。

### `li a7, BIOS_PUTSTR`
- 做什么：把功能号 9 放进 a7。
- 为什么：BIOS 的"是什么函数"由 a7 决定（查 `asm/biosdef.h`、Verify：PUTCHAR=1、
  GETCHAR=2、PUTSTR=9、SDWRITE=10、SDREAD=11）。
- 删掉会怎样：a7 是脏值，U-Boot 的 switch 走到 default 分支只打印 "n = 0x…"，
  不会打印我们的字符串。

### `call bios_func_entry`
- 做什么：跳到 `0x50150000`，同时把**下一条指令的地址**写进 `ra`。
- 为什么：`0x50150000` 处由 U-Boot 预置了一条 `ecall` + 一条 `ret`。`ecall` 让
  CPU 陷入 U-Boot（M 态），U-Boot 看 a7=9 → `mcall_console_putstr(a0)` 打印；
  `ret` 依赖 `ra` 跳回。所以 `call`（=jal）把 ra 填好是**返回的前提**。
- 删掉会怎样（改成 `j bios_func_entry`）：ra 没有返回地址，打印完 `ret` 跳到垃圾
  地址 → 跑飞。这是本项目最大的一类坑之一。

### `stop: j stop` 与 `nop`
- 做什么：原地死循环。
- 为什么：bootblock 没有任务可做、也不允许"结束"；从 bootblock 掉出去就是跑飞。
- 删掉会怎样：执行完后面（补位）的指令直到遇到随机内容，多数是非法指令/异常。

### `.string "...\n\r"`
- 做什么：存字符串文本：`\n` 换行、`\r` 回车，末尾自动 `\0`。
- 为什么：BIOS 按 `\0` 判断哪里结束。
- 改掉会怎样：不加 `\n\r` 输出会紧跟下一段内容；忘记 `\0` 就会打印到后面内存。

## 4. 实测反汇编（Task 1，`objdump -d build/bootblock`）

```
50200000: 0ff0000f   fence
50200004: 00000517   auipc a0,0x0
50200008: 01650513   addi  a0,a0,22   # a0 = 0x5020001a = msg
5020000c: 48a5       li    a7,9
5020000e: fff50097   auipc ra,0xfff50
50200012: ff2080e7   jalr  -14(ra)    # 跳 0x50150000；ra = 0x50200016
50200016: a001       j     50200016 <stop>
50200018: 0001       nop
```

**读法**：`la a0,msg` 展开成"取当前指令地址 + 偏移"（auipc+addi）；`call` 展开成
auipc+jalr 两条来做**远距离**间接跳转；跳一圈回来 ra 正好停在 `stop`。

## 5. 验证要点

- 镜像：`cd build && ../createimage --extended bootblock main`（**任务 3 之前必须用官方
  createimage**，禁止 `make image`）。
- 运行：`make run` → 等 `=> ` → `loadboot` → 屏幕上出现标志串，且**只出现一次**
  （多出现说明打印完没停住）。
- 日志证据：`.scratch/task1-qemu.log`。引用（真实日志）：

```
=> loadboot^M
It's Dreamerhyh's bootloader...^M
```

## 6. 验收准则（本轮）

1. 出现 `It's Dreamerhyh's bootloader...`；2. 输出晚于 `loadboot`；
3. 只打印一次（`stop` 自旋）；4. `loadboot` 之前无 bootblock 输出。

## Task 1 后的变更（与 git 历史一致）

- **Task 2**：在 `call bios_func_entry` 之后插入"读内核扇区数 K + 循环 bios_sd_read
  读内核到 0x50201000 + `jal kernel`"，`stop` 变成不可达兜底。
- **Task 4**：跳内核前用 `lh` 读 app 数传 a1、复用循环结束的 t0 传 a0=1+K。
- **Task 5**：追加 `addi a2, t0, 1` 传 apps 基址。
- 因此最新 bootblock.S 的 `main` 是一整段"打印→读内核→传参→跳转"流水线（见 04/06/07）。
---

## 覆盖率自检表（03）

> 行号区间为 **bootblock.S（最终形态）** 中的对应块；Task 1 当时只有打印段，读内核/传参
> 段是后续任务加入。讲解方式：逐条=每行单独解释；归组=整块统一解释。

| 源文件块（行号区间） | 讲解方式 | 归组理由 |
|---|---|---|
| `.equ` 常量池（:4/7/10 os_size_loc、kernel、bios_func_entry） | 归组 | 常量声明非主干，一句话交代用途即可，逐条无信息增益 |
| `main:` 标签 + `fence`（:15-17） | 逐条 | 入口与第一条指令，各自一个语义要讲清 |
| `la a0, msg` / `li a7, BIOS_PUTSTR` / `call bios_func_entry`（:23-25） | 逐条 | 打印主干三行，逐行解释"放什么/为什么"，并配反汇编佐证展开形态 |
| `stop: j stop` + `nop`（:74-76） | 逐条 | 防跑飞的关键，须讲"为什么必须自己停住" |
| `msg: .string "..."`（:81） | 逐条 | 字符串内容+`\n\r`+`\0` 约定 |
| 反汇编片段（§4，0x50200000…） | 归组 | 佐证前文 la/call 的展开，不属于新知识，整段对照即可 |
