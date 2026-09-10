# 任务1 工作日志：第一个引导块

## 目标

填写 `arch/riscv/boot/bootblock.S` 的 `[p1-task1]`，用纯汇编调用 BIOS 在终端打印：

```
It's Dreamerhyh's bootloader...
```

bootblock 无 C 栈、无 C 运行环境，只能写汇编；总代码必须 ≤ 512 字节；结尾保留死循环防跑飞。

## 改动

| 文件 | 改动 |
|---|---|
| `arch/riscv/boot/bootblock.S` | 填充 `[p1-task1]`：`la a0, msg` / `li a7, BIOS_PUTSTR` / `call bios_func_entry`；字符串由 `"It's a bootloader...\n\r"` 改为 `"It's Dreamerhyh's bootloader...\n\r"` |
| `.gitignore` | 新增（`build/`、`.scratch/`、`*.txt`） |
| `log/` | 新增勘察报告、工作流程、本日志 |

其余 3 处 TODO（task2 读内核、task2 跳内核、task4 传任务参数）**保持不动**，留到后续任务。

## 设计要点

### BIOS 调用怎么写

调用约定（见 Phase 0 §5，已与 u-boot 侧 `sbi.h` / `mcall_trap()` 交叉验证）：
功能号入 `a7`，参数依次入 `a0, a1…`，然后跳转 `0x50150000`。

`0x50150000` 处由 u-boot 写入的是 **一条 `ecall` + 一条 `ret`**
（`cmd/loadboot.c:write_oslab_instrs()`）。所以这是"函数调用"语义——
**返回地址 `ra` 必须提前填好**，否则 `ret` 会跳到垃圾地址跑飞。

写法用 `call`（`jal` 的同义词），让汇编器自动填 `ra`：

```asm
la    a0, msg                # a0 = 字符串首地址
li    a7, BIOS_PUTSTR        # a7 = 9
call  bios_func_entry        # 跳 BIOS，汇编器自动把下一条指令地址写进 ra
```

### 实际反汇编（`objdump -d build/bootblock`）

```
0000000050200000 <main>:
    50200000:	0ff0000f          	fence
    50200004:	00000517          	auipc	a0,0x0
    50200008:	01650513          	addi	a0,a0,22 # 5020001a <msg>
    5020000c:	48a5                	li	a7,9
    5020000e:	fff50097          	auipc	ra,0xfff50
    50200012:	ff2080e7          	jalr	-14(ra) # 50150000 <bios_func_entry>

0000000050200016 <stop>:
    50200016:	a001                	j	50200016 <stop>
    50200018:	0001                	nop
```

- `auipc a0,0 + addi a0,22` → `a0 = 0x5020001a`，正是 `msg`；
- `li a7,9` → `BIOS_PUTSTR`；
- `call` 被展开成 `auipc ra + jalr` 两条（因为 `-mcmodel=medany` 下汇编器不走单条
  `jal` 的 ±1MB 分支）。`auipc ra,0xfff50` 得 `ra = 0x5015000e`，再 `jalr -14(ra)`
  跳 `0x50150000`，同时把 `pc+4 = 0x50200016` 写回 `ra`；
- `0x50200016` 正好是 `stop` —— 打印完 `ret` 会落回死循环，**不会跑飞**。
  这一点是逐条验过的，不是想当然。

### 体积

- `PT_LOAD filesz = memsz = 0x3c`（60 字节），`msg` 落在 `0x5020001a`、结束于 `0x5020003c`；
- 镜像信息字段在 `0x502001fc`（`os_size`）与 `0x502001fe`（`55 aa` 签名），
  **bootblock 代码与之无重叠**；
- 距离 512 字节上限还余 452 字节，Task 2 读内核的循环代码放得下。

## 构建过程

```bash
make dirs
make elf
chmod +x ../createimage && cd build && ../createimage --extended bootblock main
```

`make elf` 实际命令：

```
riscv64-unknown-linux-gnu-gcc -O2 -std=gnu11 -fno-builtin -nostdlib -nostdinc -Wall \
  -mcmodel=medany -ggdb3 -I./arch/riscv/include \
  -Wl,--defsym=TEXT_START=0x50200000 -T riscv.lds \
  -o build/bootblock ./arch/riscv/boot/bootblock.S -e main
```

（链接器警告 `warning: build/bootblock has a LOAD segment with RWX permissions` 无害，
bootblock 本就是纯汇编、无只读/只写区分。）

官方 createimage 输出尾部：

```
os_size: 4 sectors
```

镜像：`build/image` = 2560 字节 = 5 扇区（扇区 0 = bootblock，扇区 1–4 = 内核）。
尾部字节实测：

```
000001f0: 0000 0000 0000 0000 0000 0000 0400 55aa
                ^os_size=4 (小端2字节)  ^签名 55 aa
```

首扇区前 0x20 字节实测：

```
00000000: 0f00 f00f 1705 0000 1305 6501 a548 9700
00000010: f5ff e780 20ff 01a0 0100 4974 2773 2044   ← "It's D"
```

## 自动化验证

脚本：`.scratch/task1-verify.sh`（已 gitignore，验收通过后删除）

思路：清理残留 QEMU → 起 QEMU 并向 stdin 按时序注入 `loadboot` →
全部输出重定向到 `.scratch/task1-qemu.log` → 用固定串断言逐条比对 → 收尾清理进程。

注入序列：`sleep 6s`（等 U-Boot autoboot 走完，末尾会执行
`virtio read 50200000 0 2` 把镜像第 0/1 扇区读入内存）→ `printf 'loadboot\n'` →
`sleep 12s` 收集输出；整体 `timeout 25s`。

QEMU 日志关键节选（`cat -v`，`^M` 为回车）：

```
22: virtio read: device 0 block # 0, count 2 ... 2 blocks read: OK^M
23: => loadboot^M
24: It's Dreamerhyh's bootloader...^M
```

验收标准比对：

| # | 预期 | 实际 | 结果 |
|---|---|---|---|
| 1 | 屏幕上出现 `It's Dreamerhyh's bootloader...` | 日志第 24 行命中固定串 | PASS |
| 2 | 输出晚于 `loadboot`（证明是跳转后才执行） | 串在第 24 行，`loadboot` 在第 23 行 | PASS |
| 3 | 只打印 1 次（`stop` 自旋、未跑飞） | 出现次数 = 1 | PASS |
| 4 | `loadboot` 之前无任何 bootblock 输出 | 成立 | PASS |

> QEMU 退出码 124 = `timeout` 主动杀进程，属预期（bootblock 会永久自旋，不会自己退出）。
> 收尾检查残留进程数 = 0。

## 踩坑记录

1. **shell 工作目录会跨命令持续保留**：早前执行过 `cd build && ../createimage …`，
   之后 `.scratch/task1-verify.sh` 报 `No such file or directory`（脚本明明写成功了，
   是相对路径解析到了 `build/.scratch/`）。改为每条命令先 `cd` 到仓库根目录或直接用
   绝对路径。**已写进 `log/工作流程.md`。**
2. **`pgrep` 不带 `-f` 会失效**：`pgrep qemu-system-riscv64` 因进程名超过 15 字符直接报
   "pattern that searches for process name longer than 15 characters will result in zero
   matches"，会误判成"无残留进程"。必须用 `pgrep -af`。
3. **`call` 到 `0x50150000` 被展开成两条指令**（`auipc ra` + `jalr`），不是单条 `jal`。
   原因是 bootblock 用 `-mcmodel=medany` 编译。已逐条核对 `ra` 最终指向 `stop`。
   如果改用手写 `auipc ra,0 / addi ra,ra,12 / jalr`，偏移量容易算错成 `+8`（那样会跳回
   `jalr` 自己，形成无限重入 BIOS 的循环）——`call` 更不易错，所以保留 `call`。
4. `make elf` 会重新编译全部 app 的入口地址自增逻辑，重跑不影响既有产物；
   本任务中 `make elf` 对 main/各 app 显示 `Nothing to be done`，只有 bootblock 重编。

## 结论

Task 1 自检全部通过，可以提交。bootblock 打印功能、`stop` 自旋保护、512 字节体积
约束、镜像信息字段不冲突这四点均已实测确认。
