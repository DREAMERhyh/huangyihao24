# Phase 0 代码勘察报告

> 勘察方式：全量阅读源码 + `readelf/nm/objdump` 反查编译产物 + 阅读 `~/OSLab-RISC-V/u-boot`
> 源码 + 实测 QEMU 启动一次。勘察阶段**未修改任何代码**，未执行 `make image` / `make all`。
>
> 本报告为后续教学文档的底稿，结论均已实测或源码印证。勘误见文末。

---

## 0. 工作区现状（与铁律相关的异常）

```
$ git status --short
 M createimage      ← 只有文件权限变了（100644 -> 100755）
?? build/           ← 未被 .gitignore 覆盖
```

- 已校验 `git show HEAD:createimage | md5sum` 与工作区文件 **md5 完全一致**
  （`ab364c4b0b69e29eea2ec48f33597509`），**官方二进制内容零改动**，只是多了 `+x` 执行位。
  而 `cd build && ../createimage` 这个 Task 1/2 的必经命令**必须有 +x 才能执行**。
- 仓库根目录**没有** `.gitignore`（`Project1` 分支上 `git cat-file -e` 确认不存在）。
- 处置：新增 `.gitignore`（`build/`、`.scratch/`、`*.txt`），并在 commit 前把
  `createimage` 权限还原为 644，保证官方二进制在 git 眼里完全没变。

---

## 1. 目录树与文件职责

git 跟踪 26 个文件，无多余源码文件。

```
./
├── Makefile                        构建/运行总入口（167 行）
├── riscv.lds                       内核/bootblock/app 共用的链接脚本（185 行，不可改）
├── createimage                     ★官方 createimage 二进制（23208B，只读，不许动）
├── README.md                       仅一句 TODO 占位，按用户裁决不动
├── test.S                          无关的 P0 C 练习骨架，不被 Makefile 构建
├── C_baseline/                     空目录，git 不跟踪，与 P1 无关
├── build/                          未跟踪构建产物：bootblock main 2048 auipc bss data crt0.o image
│
├── arch/riscv/
│   ├── bios/common.c               C 侧 BIOS 封装：port_write/port_read_ch/sd_read/sd_write（只读参考）
│   ├── boot/bootblock.S            ★Task 1/2/4 主战场
│   ├── crt0/crt0.S                 ★Task 3 主战场：用户程序 C 环境
│   ├── kernel/head.S               ★Task 2 主战场：清 bss + 设 sp + 跳 main
│   └── include/
│       ├── asm/biosdef.h           ★BIOS 功能号定义（不可改）
│       ├── asm.h                   汇编宏 ENTRY/END/FEXPORT
│       ├── common.h                port_*/sd_* 声明 + 颜色宏
│       └── csr.h                   CSR 地址与中断号常量
│
├── include/
│   ├── type.h                      基础类型（uint8_t…uint64_t、size_t、NULL）
│   └── os/
│       ├── kernel.h                ★内核侧 bios_* 封装（经跳转表）+ KERNEL_JMPTAB_BASE
│       ├── task.h                  ★TASK_MEM_BASE/TASK_MAXNUM/TASK_SIZE/SECTOR_SIZE + task_info_t(TODO)
│       ├── loader.h                load_task_img(int) 声明
│       ├── string.h                memcpy/memset/bzero/str* 声明
│       └── sched.h                 P3 调度器声明（P1 用不到，引用了不存在的 os/list.h）
│
├── init/main.c                     ★内核主循环（89 行）
├── kernel/loader/loader.c          ★Task 3/4 主战场：load_task_img（14 行，全 TODO）
├── libs/string.c                   字符串函数实现（已完整可用）
├── tiny_libc/include/kernel.h      ★用户程序侧 bios_* 封装（app 里写 #include <kernel.h>）
├── test/test_project1/             4 个用户程序：2048.c auipc.c bss.c data.c
└── tools/createimage.c             ★Task 3/4 主战场：自建 createimage 源码（232 行）
```

---

## 2. Makefile target 清单

| target | 作用 |
|---|---|
| `all` | `dirs elf image asm`（`# floppy` 被注释掉） |
| `dirs` | `mkdir -p ./build` |
| `clean` | `rm -rf ./build` |
| `elf` | 编 bootblock + 内核 + 4 个 app（**不**编 createimage） |
| `image` | 编 `tools/createimage.c` 到 `build/createimage`，再 `cd build && ./createimage --extended …` |
| `asm` | 对 bootblock/main/各 app 做 `objdump -d` |
| `floppy` | `sudo dd if=build/image of=/dev/sdb3`（真硬件写盘） |
| `run` / `debug` | 起 QEMU（debug 额外 `-s -S`） |
| `gdb` | `riscv64-unknown-linux-gnu-gdb build/main -ex "target remote:1234"` |
| `minicom` | `sudo minicom -D /dev/ttyUSB1`（真硬件串口） |

关键命令行：

- bootblock：`riscv64-unknown-linux-gnu-gcc -O2 -std=gnu11 -fno-builtin -nostdlib -nostdinc -Wall -mcmodel=medany -ggdb3 -I./arch/riscv/include -Wl,--defsym=TEXT_START=0x50200000 -T riscv.lds -o build/bootblock arch/riscv/boot/bootblock.S -e main`
- 内核：同上，`-Iinclude` 追加、`TEXT_START=0x50201000`、输出 `build/main`，源文件为
  `arch/riscv/kernel/head.S init/main.c arch/riscv/bios/common.c kernel/loader/loader.c libs/string.c`
- app：`-I./tiny_libc/include -o build/<app> build/crt0.o test/test_project1/<app>.c -Wl,--defsym=TEXT_START=0x52000000 -T riscv.lds`
- `make image` 展开为：`cd build && ./createimage --extended bootblock main 2048 auipc bss data`
- `make run`：
  ```
  ~/OSLab-RISC-V/qemu/riscv64-softmmu/qemu-system-riscv64 -nographic -machine virt -m 256M \
    -kernel ~/OSLab-RISC-V/u-boot/u-boot -bios none \
    -drive if=none,format=raw,id=image,file=build/image \
    -device virtio-blk-device,drive=image \
    -monitor telnet::45454,server,nowait -serial mon:stdio
  ```

⚠️ `asm` target 会把 `bootblock.txt main.txt 2048.txt auipc.txt bss.txt data.txt` 写到
**仓库根目录**（`> $(notdir $$elffile).txt`），不是 build/。已加入 `.gitignore`。

---

## 3. QEMU 存储挂载方式（★含重要实测结论）

- 镜像文件：`build/image`，**唯一块设备** → virtio 设备号 0。
- `-kernel` 加载 `~/OSLab-RISC-V/u-boot/u-boot`（`U-Boot 2024.07UCAS_OS DASICS v3.1.0`），
  `-bios none`，QEMU 8.1.50，256M 内存。
- 实测启动日志：
  ```
  U-Boot 2024.07UCAS_OS DASICS v3.1.0 …
  DRAM:  256 MiB   /   In: Out: Err: serial@10000000
  Net:   No ethernet found.
  Hit any key to stop autoboot:  0
  Device 0: QEMU VirtIO Block Device
              Type: Hard Disk
              Capacity: 0.0 MB = 0.0 GB (5 x 512)
  ... is now current device
  ** Invalid partition 1 **
  No ethernet found.
  No ethernet found.
  virtio read: device 0 block # 0, count 2 ... 2 blocks read: OK
  => loadboot
  ```

**结论 1：需要手动输入 `loadboot`**（提示符 `=> `）。autoboot **不需要按键**（日志里倒计时已
是 `0`）。实测等待 **6 秒**后注入 `loadboot` 成功。

**结论 2：bootblock 不是靠 `loadboot` 加载的**，而是 u-boot 自举命令提前读的。根因在
`~/OSLab-RISC-V/u-boot/.config:122`：
```
CONFIG_BOOTCOMMAND="run distro_bootcmd\n virtio read 50200000 0 2\n"
```
即 autoboot 末尾执行 `virtio read 50200000 0 2`：**从镜像第 0、1 扇区读 1024 字节到
0x50200000**。所以 bootblock（扇区 0）与内核扇区 1（落到 0x50200200）在 `loadboot` 之前
就已在内存里。`loadboot` 本身只做 `mret` 到 0x50200000（`cmd/loadboot.c:152`，进 S 态、
PMP 全开 RWX）。

**结论 3（对 Task 5 决定性）：BIOS 的 `sd_read` 硬编码只读 virtio 设备 0。**
`arch/riscv/lib/interrupts.c:212`：
```c
blk_read_devnum(if_type_virtio, 0, block_id, num_of_blocks, (unsigned long *)addr);
```
→ 就算 Task 5 在 Makefile 挂第二个 `-drive`，内核运行时也读不到第二块盘。
因此 **Task 5 必须采用"单个 build/image 内划分内核镜像区 + 应用镜像区"** 的等价方案。

**结论 4**：boot 阶段只读 2 扇区，应用数据只要排在扇区 2 之后，天然满足
"启动阶段绝不读取应用内容"。

---

## 4. riscv.lds 关键地址

- `ENTRY(_start)`；`. = TEXT_START`（由 `-Wl,--defsym=TEXT_START=…` 传入）。
  bootblock 用 `-e main` 强制入口为 `main`。
- 段序：`.text`(含 `.entry_function`) → `.rodata` → `.sdata2/.sbss2` → `.eh_frame*`
  → 各种 array → `.data` → `.data1/.got` → `.sdata` → **`.sbss` → `.bss`** → 对齐 8 → `__BSS_END__`。
- **bss 边界符号**：`__bss_start`（无条件赋值）、`__BSS_END__`（无条件赋值）、
  `_end`（与 `__BSS_END__` 同址）。注意 `__bss_start` 是小写 bss、`__BSS_END__` 是大写。
- 实测符号（`build/main`）：`_start`=`_ftext`=0x50201000、`main`=0x5020100e、
  `__bss_start`=0x50201558、`__BSS_END__`=`_end`=0x50201590。
- 链接脚本中**没有**内核栈/跳转表符号：`KERNEL_STACK=0x50500000` 定义在 `head.S`，
  `KERNEL_JMPTAB_BASE=0x51ffff00` 定义在 `include/os/kernel.h`。
- 三份产物（bootblock / 内核 / app）**共用同一份** `riscv.lds`，且铁律不许改它，
  因此 app 的段布局必须接受现状（见 §8.4）。

---

## 5. biosdef.h 功能号表

| 宏 | 值 | 接口 | 参数 | 返回值 |
|---|---|---|---|---|
| `BIOS_PUTCHAR` | 1 | 打印单字符 | `a0=ch` | 0 |
| `BIOS_GETCHAR` | 2 | 读一个字符 | 无 | **无输入返回 -1** |
| `BIOS_PUTSTR` | 9 | 打印字符串 | `a0=str`（`\0` 结束） | 0 |
| `BIOS_SDWRITE` | 10 | 写 SD | `a0=addr, a1=块数, a2=块号` | 成功 0 / 失败 1 |
| `BIOS_SDREAD` | 11 | 读 SD | `a0=addr, a1=块数, a2=块号` | 成功 0 / 失败 1 |

已与 u-boot 侧 `arch/riscv/include/asm/sbi.h` 交叉验证完全一致
（`SBI_CONSOLE_PUTCHAR 1 / GETCHAR 2 / PUTSTR 9 / SD_WRITE 10 / SD_READ 11`），
分发点在 `interrupts.c` 的 `mcall_trap()`：取 `regs->a7` 作功能号，结果写回 `regs->a0`。

调用约定实证：`common.c` 的 `call_bios()` 把 `which` 放 **a7**，`arg0..arg4` 放 **a0..a4**
（源码写法 `bios_func(arg0,…,arg4, IGNORE, IGNORE, which)`，即 a5/a6 忽略）。
`0x50150000` 处由 u-boot 写入一条 `ecall` + `ret`（`cmd/loadboot.c:write_oslab_instrs()`）。

`bios_getchar()` 返回 -1 亦已确证：`cgetc() → serial_cgetc() → __serial_cgetc()`，
其中 `ops->getc()` 返回 `-EAGAIN` 时 `return -1`（`drivers/serial/serial-uclass.c:213`）。

---

## 6. createimage 现状

### 官方 `./createimage`（二进制，23208B）

用法：`cd build && ../createimage --extended bootblock main`。实测输出：

```
0x50200000: bootblock
	segment 1  offset 0x1000  vaddr 0x50200000  filesz 0x001f  memsz 0x001f
		writing 0x001f bytes
		padding up to 0x0200
0x50201000: main
	segment 1  offset 0x1000  vaddr 0x50201000  filesz 0x0558  memsz 0x0590
		writing 0x0590 bytes
		padding up to 0x0800
	segment 2  offset 0x1334  vaddr 0x50201334  filesz 0x0094  memsz 0x0094
		writing 0x0094 bytes
		padding up to 0x0a00
os_size: 4 sectors
```

行为要点（与 `tools/createimage.c` **不一致**）：
1. 每个段写的是 **`p_memsz`** 而非 `p_filesz`；
2. 会写**所有**段（连 `GNU_EH_FRAME` 也写进镜像），源码版会跳过非 PT_LOAD；
3. 每个段 padding 到 **0x400 对齐**；
4. 最后写 `os_size`（小端 2 字节）到镜像 **0x1fc**，并在 **0x1fe/0x1ff 写签名 `55 aa`**
   （`BOOT_LOADER_SIG_OFFSET 0x1fe`，`OS_SIZE_LOC = 0x1fe - 2`）。
5. 实测镜像 5 扇区 2560 字节：扇区 0 = bootblock，扇区 1–4 = 内核（4 扇区）。

### `tools/createimage.c`（自建版）

- 输出固定写 `./image`（必须在 `build/` 下运行）。
- 已实现：`main()`、`create_image()`、`read_ehdr()`、`read_phdr()`、`get_entrypoint/get_filesz/get_memsz`、
  `write_segment()`、`write_padding()`。
- TODO：`task_info_t` 为空；`create_image()` 里未给 `taskinfo[]` 赋值；段后 padding 策略；
  **`write_img_info()` 整体为空**——现在跑 `make image` 出的镜像 0x1fc 处没有内核扇区数，
  bootblock 会读到 0 扇区，这正是铁律禁止 Task 1/2 跑 `make image` 的原因。
- 细节：`tasknum = nfiles - 2`；`taskidx = fidx - 2`；`nbytes_kernel` 累加**文件名等于
  `"main"`** 的 PT_LOAD 段 `p_filesz`；`write_segment` 按 `p_filesz` 逐字节写。

---

## 7. 全部 TODO 标记清单（22 处）

| 位置 | 行 | 归属 |
|---|---|---|
| `arch/riscv/boot/bootblock.S` | 19 | **task1**：调 BIOS 打印字符串 |
| `arch/riscv/boot/bootblock.S` | 22 | task2：调 BIOS 从 SD 读内核 |
| `arch/riscv/boot/bootblock.S` | 28 | task2：跳到内核 |
| `arch/riscv/boot/bootblock.S` | 25 | task4：加载任务参数并传给内核 |
| `arch/riscv/kernel/head.S` | 17 | **task2**：清 BSS |
| `arch/riscv/kernel/head.S` | 20 | task2：建 C 环境（设 sp、跳 main） |
| `init/main.c` | 79 | **task3** 按 taskid / task4 按名字加载并执行 |
| `init/main.c` | 41 | task4：读 app-info 扇区初始化 `tasks[]` |
| `arch/riscv/crt0/crt0.S` | 6 | **task3**：为用户程序建 C 运行环境 |
| `arch/riscv/crt0/crt0.S` | 9 | task3：进入 app main |
| `arch/riscv/crt0/crt0.S` | 12 | task3：任务结束返回内核 |
| `kernel/loader/loader.c` | 10 | **task3**：按 taskid 加载，返回入口地址 |
| `kernel/loader/loader.c` | 11 | task4：改为按 `char *taskname` 加载 |
| `include/os/task.h` | 14 | **task4**：实现 `task_info_t` |
| `tools/createimage.c` | 20 | task4：设计 `task_info_t` |
| `tools/createimage.c` | 79 | task4：赋值 `task_info_t` |
| `tools/createimage.c` | 126 | task3：padding 使内核与每个 app 占相同扇区 |
| `tools/createimage.c` | 127 | task4：只允许 padding bootblock |
| `tools/createimage.c` | 216 | task3 & task4：写镜像信息 |
| `test.S` | 5 | `[p0-task1]`，与 P1 无关 |

---

## 8. 用户程序的编译链接机制

### 8.1 入口地址算法

```makefile
$(DIR_BUILD)/%: $(DIR_TEST_PROJ)/%.c $(OBJ_CRT0) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds
	$(eval USER_ENTRYPOINT := $(shell python3 -c "print(hex(int('$(USER_ENTRYPOINT)',16)+int('0x10000',16)))"))
```

`USER_ENTRYPOINT` 初值 `0x52000000`，每编完一个 app 自增 `0x10000`，顺序由 wildcard 字母序决定。

| app | ELF 入口 | `__bss_start` | `__BSS_END__` | PT_LOAD filesz/memsz | flags |
|---|---|---|---|---|---|
| 2048 | 0x52000000 | 0x52001228 | 0x52001228 | 0x1228 / 0x1228 | **R E**（无 W、无 bss） |
| auipc | 0x52010000 | 0x5201021c | 0x52010220 | 0x21c / 0x220 | R E，bss 4B |
| bss | 0x52020000 | 0x52020108 | 0x52020140 | 0x108 / 0x140 | RWE，bss 56B |
| data | 0x52030000 | 0x52030150 | 0x52031150 | 0x1150 / 0x1150 | RWE，无 bss |

与 `task.h` 的 `TASK_MEM_BASE 0x52000000`、`TASK_SIZE 0x10000`、`TASK_MAXNUM 16` 吻合。

### 8.2 crt0.S 如何链进去

- `$(OBJ_CRT0): $(SRC_CRT0)` → 编译**一次**，4 个 app **共用同一个 `build/crt0.o`**。
- 每个 app 链接时把 `crt0.o` 放在源文件**前面**：`-o build/2048 build/crt0.o …/2048.c`。
- 链接脚本 `ENTRY(_start)`，`crt0.S` 的 `ENTRY(_start)` 把 `_start` 放 `.entry_function` 段，
  该段被排在所有 `.text` 之前 → **每个 app 的入口就是 crt0 的 `_start`**。

### 8.3 app 的 bss 边界从哪来

**结论：`__bss_start` / `__BSS_END__` 是每个 app 自己 ELF 里的绝对符号，由共用的
`riscv.lds` 在该 app 自身布局中赋值；`crt0.S` 直接引用这两个符号名，链接器就会解析成
"当前这个 app"的 bss 边界。** 不需要 bootblock 传参，也不需要额外信息扇区。

依据：两个符号在 lds 中是**无条件赋值**（非 `PROVIDE`）；4 个 app 的 `nm` 输出证明边界各不相同；
同一个 `crt0.o` 链接出 4 组不同值，只有"链接期绝对符号"能解释。

对 2048 / data 这两个无 bss 的程序，清零循环是空转，天然安全。

### 8.4 app 栈方案（★经用户裁决）

app 的 64KB 窗口内代码+数据紧贴窗口底部（2048 占 0x1228、data 占 0x1150），窗口顶部是空的。

**最终方案（用户裁决）**：所有 app 共享栈，栈顶 `0x50400000`，向下生长。
- 内核栈独享 `0x50400000–0x50500000`（约 1MB），`KERNEL_STACK=0x50500000` 向下生长时不会碰到 app 栈；
- app 栈向下到 0x50200000 仍有约 2MB 空间，远大于 4 个 app 的需求；
- `crt0.S` 在调 app `main` 前把内核 sp 存进 `a0`，app 返回后 `mv sp, a0` 再跳回内核 `main`
  （`init_jmptab()` 被链接器内联掉、不产生可引用符号，`main` 是唯一可链接的恢复点）。

> 原提案（app 栈顶 `0x504fffff`）被驳回：与 `KERNEL_STACK=0x50500000` 仅差 1 字节，
> 两个向下生长的栈顶部必然交叠，会踩掉内核现场。已作废。

---

## 9. 与"关键事实"不一致或存疑之处

1. **bootblock 由 u-boot 的 `CONFIG_BOOTCOMMAND` 提前读取，`loadboot` 只做 `mret`**；且读的是
   **2 个扇区**（`virtio read 50200000 0 2`），不是 1 个。扇区 1 会顺带落到 0x50200200，
   对只占用扇区 0 的 bootblock 无影响。
2. **`-O2` 已经在 Makefile 里了**：`CFLAGS = -O2 …` 且被三份 CFLAGS 全部继承。
   Task 4 的"加 -O2"在现状下已是事实，届时以"确认已生效 + 实测体积"诚实处理，不伪造对比。
3. **`make image` 用的是自建 `tools/createimage.c`**（`build/createimage`），**不是**根目录那个
   官方二进制；官方二进制只能手动 `cd build && ../createimage` 使用。
4. **镜像信息布局已定稿**：内核扇区数 2B @`0x1fc`（bootblock 用 `lh` 读），
   **app 数目 2B @`0x1f8`**，官方签名 `55 aa` @`0x1fe/0x1ff` 保持不动。
   （提示词建议的 @0x1fe 会破坏官方签名，故调整。）
5. `make image` 里 `--extended` 已带；Task 1/2 手动执行时仍需自己带上。
6. **app 侧 `kernel.h` 在 `tiny_libc/include/kernel.h`**（`USER_INCLUDE`），与内核侧
   `include/os/kernel.h` 是两份：后者多 `SD_READ/SD_WRITE` 两个跳转表槽位（枚举值 4/5），
   前者只有 3 个（0/1/2）。两者 `KERNEL_JMPTAB_BASE` 都是 0x51ffff00，app 可复用内核填好的表。
7. **`sd_read` 硬编码 virtio 设备 0** → Task 5 不能挂两块盘，必须单镜像分区。
8. **`include/os/sched.h` 引用不存在的 `include/os/list.h`**，但无人 include `sched.h`，
   不影响构建。P3 遗留，P1 不碰。
9. `init/main.c:15` 的 `tasks[TASK_MAXNUM]` 目前仅 16 字节（`task_info_t` 为空结构体，
   C 里 sizeof=1）。Task 4 改结构体后内核体积增大，但不影响 0x1fc 字段与 512B bootblock 限制。
10. `asm` target 产物写到仓库根目录（`*.txt`），已加入 `.gitignore`。
11. 官方 createimage 写 `memsz` + 写所有段 + 0x400 对齐；自建版写 `filesz` + 只写 PT_LOAD。
    当前内核：官方算 4 扇区，自建按 `NBYTES2SEC(0x558)=3` 算 3 扇区。bootblock 读扇区数
    取自镜像 0x1fc，两侧一致即可。

---

## 10. 勘误（用户裁决后修正）

1. **`KERNEL_STACK=0x50500000` 的位置**：勘察初稿曾写"0x50500000 在内核允许区间
   0x50200000–0x51FFFFFF 之外"，**这是错误的**。`0x50500000` 满足
   `0x50200000 ≤ 0x50500000 ≤ 0x51FFFFFF`，**在内核区间之内**
   （初稿把 0x50500000 与 app 起始地址 0x52000000 搞混了）。按提示词保留 0x50500000，无需改动。
2. **app 栈位置**：初稿提案"共享栈顶 0x504fffff"被用户驳回——该地址与
   `KERNEL_STACK=0x50500000` 仅差 1 字节，两个向下生长的栈顶部必然交叠。
   已改为**共享栈顶 0x50400000**，见 §8.4。
3. **Task 3 审批语"bootblock 零改动"（用户自己纠正为错误表述，我未及时点破冲突）**：
   - Task 3 验收通过消息里的原话是"bootblock.S：读内核扇区数的逻辑保持从 0x1fc 读取
     （与 Task 2 一致，天然兼容）"，这句容易被读成"bootblock 零改动"；
   - 但 **`bootblock.S:25` 本身就是 `[p1-task4] load task-related arguments and pass
     them to kernel` 的 TODO**（本报告 §7 已列出），Task 4 必须填它（读 AppInfo、传参），
     任务书原文"bootblock.S：读内核扇区数的逻辑保持从 0x1fc 读取"只是约束**那一处逻辑**
     不变，并不表示 bootblock 完全不动；
   - 按 Task 2 立下的规矩："指令与源码/任务书冲突时必须显式点破再行动"，我应当在
     Task 3 汇报里指出"该承诺与 bootblock.S:25 的 [p1-task4] TODO 冲突"，当时没提，
     **这是继 head.S 之后第二次漏点破**。此处正式勘误，并在 Task 4 冲突表里登记。
4. **任务书原文引用一律以 `docs/taskbook-extracts-p1.md` 入库文本为准**：
   本报告及历次日志中出现的、无权威出处的转述条目（例如"app 数目 @0x1fe"、
   "启动日志证明只加载内核镜像"等），凡与入库文本冲突或不在库内者，一律
   **不得再当作任务书原文引用**；确属我们自定的验收标准标注为"验收方要求"。

---

## 11. 用户裁决汇总（开工依据）

| 项 | 裁决 |
|---|---|
| Task 1 字符串 | `It's Dreamerhyh's bootloader...\n\r` |
| 镜像信息布局 | 内核扇区数 2B @0x1fc（`lh`），app 数目 2B @0x1f8，签名 @0x1fe 不动 |
| `.gitignore` / 官方二进制权限 | 新增 `.gitignore`；commit 前把 `createimage` 还原为 644 |
| app 栈 | 共享栈顶 `0x50400000`；内核栈独享 0x50400000–0x50500000 |
| `README.md` | 不动 |
| Task 4 的 `-O2` | 现状已生效，诚实处理，不伪造"对比缩小" |
| Task 5 存储 | 单个 `build/image` 内划分内核镜像区 + 应用镜像区（偏移文档化） |
