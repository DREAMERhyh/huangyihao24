# 09 QEMU 与 gdb 调试实战

> 目标：把本课程真实踩过的三个坑当案例，讲"怎么定位、怎么断言、怎么读反汇编"。
> 三案例都可复现。

## 0. 工具怎么用

```bash
# 双终端或后台：
make run             # 跑起来手动交互（先 loadboot）
make debug           # 同上，但这回 QEMU 会停在第一行代码、监听 TCP:1234
make gdb             # gdb build/main -ex "target remote:1234"
```

gdb 常用：

```
b main                       # 内核 main 断点
b load_task_img              # 按名加载断点
c                            # 继续
x/16gx 0x502001f8            # 看信息字段 16 个 8 字节
x/16bx 0x502001fc            # 看 0x1fc 的前 16 字节
x/s 0x50200060               # 看字符串（bootblock 的 msg，当前布局；用 nm 确认）
info reg sp ra a0 a7         # 看寄存器
si                           # 单步一条指令
disas                        # 当前函数反汇编
```

> bootblock 没有 C 栈，`gdb build/bootblock` 也能连，但只能看汇编；要在 0x50200000
> 处设断点就 `b *0x50200000`。

## 案例 1：objdump 的 `jalr a0` 缩写——一次"误诊三连"

现象：内核跳转 app 的指令反汇编为 `jalr a0`，于是**想当然**认为"返回地址被写进 a0
而不是 ra，crt0 靠 ra 跳回内核会失败"。为此改了跳转写法、又引入 `\L1` 标签展开
报错等一串折腾。

定位过程（还原一个"**别只看反汇编说出来，要数位段**"的教训）：

```bash
# 1. 手写最小汇编，把各写法编出来看原始字节：
cat > /tmp/jalr_repro.S <<'EOF'
.text
f1: li a0,0x52000000; jalr ra, 0(a0)
f2: li a0,0x52000000; jalr x1, a0, 0
EOF
riscv64-unknown-linux-gnu-gcc -mcmodel=medany -c /tmp/jalr_repro.S -o /tmp/jalr_repro.o
riscv64-unknown-linux-gnu-objdump -d /tmp/jalr_repro.o
# f1/f2 都是 0x000500e7，objdump 显示为 "jalr a0"

# 2. 用 Python 按 JALR 编码数位段（rd=(enc>>7)&0x1f, rs1=(enc>>15)&0x1f）：
python3 -c "enc=0x000500e7; print('rd=%d rs1=%d' % (((enc>>7)&0x1f),(enc>>15)&0x1f))"
# 输出 rd=1(ra) rs1=10(a0)  ← 编码正确！返回地址本来就在 ra
```

结论（三条，都进了 `log/task3-工作日志.md` 前置项 1）：
1. **GAS 对 `jalr ra, x` 的编码是正确的（rd=x1）**；
2. **objdump 会把 rd=ra 的 jalr 缩写显示成 `jalr a0`**——于是 rd=ra 与 rd=a0 两种编码
   显示一样，手一抖就误读；
3. 最终内核仍保留 `la ra, 1f; jalr a0, %0; 1:` 的写法作为**双保险**，把"返回地址在哪"
   显式写进源码，也把读代码的人从 objdump 缩写里解放出来。

> 通用教训：**判断一条指令的真实语义要看原始编码（`readelf`/`xxd`），不要只看
> objdump 的助记符缩写**。本项目把这条写进了设计评审的遗留风险 #4。

## 案例 2：grep -F 里的 `\[` 不转义——四连"假失败"

现象：自动化断言里写 `check "\[auipc\] Info: testpc"`，结果 4 个 app 启动断言全 FAIL，
但日志里 app 明明跑得好好的。

原因：`check()` 用的是 `grep -aFq`（**固定字符串**）。固定串模式下 `\[` 是字面的
`\` 加 `[`，匹配不到 `[auipc]`；转义只对 `grep -E`（正则）有意义。

```bash
grep  -aFq '\[auipc\] Info'  log  && echo 匹配 || echo 不匹配   # 不匹配（坑）
grep  -aFq '[auipc] Info'    log  && echo 匹配 || echo 不匹配   # 匹配（对）
grep  -aEq '\[auipc\] Info'  log  && echo 匹配                  # 正则也行
```

结论：用 `-F`（fixed）就别加反斜杠；要转义就换 `-E`（extended regex）。本项目在
`log/task4-工作日志.md` 与 `log/task5-工作日志.md` 各记一次。

## 案例 3：512B 整扇区直读越界 —— 差点把 BSS 冲掉

现象（设计期推演，实为避免）：若 `init_task_info` 这样写：

```c
static uint8_t appinfo_buf[SECTOR_SIZE] __attribute__((aligned(SECTOR_SIZE)));
/* 直接读进 tasks[]： */
bios_sd_read((unsigned)(unsigned long)tasks, 1, 1 + kernel_sectors);   // ← 危险版
```

`tasks[TASK_MAXNUM]` = 16×28 = 448 字节，而 `bios_sd_read` 一次读进 512 字节——
**多出的 64 字节会覆写 tasks 后面的 .bss**（`task_num`、`boot_apps_base` 等变量
排布其后）。表现往往是"启动正常，一进主循环 task_num 变成 0/垃圾"。

定位思路（如果真踩到）：
```bash
# readelf 看 tasks 到底多大、手段仍然在 .bss 里：
riscv64-unknown-linux-gnu-nm build/main | grep -E 'tasks|task_num|boot_apps_base'
# gdb 停在 init_task_info 返回后：
b init_task_info
c
finish
p task_num          # 应当 == 4；若为 0/垃圾 → 典型整扇区覆写
x/24gx tasks        # 看 tasks 之后的 64 字节是否被污染
```

结论：**任何"读入大小 != 目标数组大小"的整块读都必须走中转缓冲 + memcpy 精确长度**。
本项目采用 `aligned(SECTOR_SIZE)` 缓冲（512B 对齐除了读快，也让 readelf 断言的
`%8` 之类检查稳定）。

## 调试三原则（本项目反复验证的）

1. **先看再猜**：`objdump -d` / `readelf -lW` / `nm` / `xxd` 四件套胜过任何脑补。
2. **断言要能指向根因**：注入序列 + 固定串断言，哪怕断言写歪了（grep 坑），也比
   "肉眼看到了"可复现。
3. **日志即证据**：QEMU 全部输出进 `.scratch/*.log`，验收/复查靠它横向比对；每次
   验证结束用 `ps -C qemu-system-riscv64` + 端口检查收尾（参考 `log/工作流程.md`）。