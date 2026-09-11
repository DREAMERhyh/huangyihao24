# 10 常见报错 FAQ

> **读完你能回答的 3 个问题**：① 官方 `./createimage` 为什么需要 `chmod +x`、又要在提交前
> 还原 644？② `bios_getchar` 无输入时返回什么、不处理会怎样？③ 怎样确认 QEMU 进程确实
> 被杀干净了（为什么不能直接用 `pgrep -af qemu-system-riscv64` 判空）？

> 八项都是本项目真实遇到或容易遇到的。每条给"现象 → 原因 → 修法 → 预防"。

---

## 1. `permission denied: ./createimage`

- 现象：`cd build && ../createimage --extended …` 报 `Permission denied`。
- 原因：官方 `./createimage` 在 git 里记录的是权限 644（不可执行）；我们为"不许
  删除/覆盖/改名"的只读二进制，执行前需要临时加执行位。
- 修法：`chmod +x ../createimage` 后再执行；**提交前还原** `chmod 644 createimage`
  保证 git 里它零改动。
- 预防：写进 `log/工作流程.md`；`git status` 里出现 `M createimage` 就是忘了还原。

## 2. 屏幕上全是乱码（0xFF/花屏）

- 现象：回显循环跑起来后满屏乱码。
- 原因：`bios_getchar()` 在无输入时返回 **-1**；把它当字符打印就是 0xFF。
  （根因：U-Boot `__serial_cgetc` 在无数据时 `return -1`，出处：Phase 0 §5。）
- 修法：`if (ch == -1) continue;`——-1 必须跳过。
- 预防：Task 2 验收就断言"日志中无 0xFF 字节"。

## 3. `bss check` 失败 / bss 检查没过

- 现象：`bss check: f` 或 `[bss] Error: bss check failed!`。
- 原因：BSS 没被清零。内核侧 head.S 的清 BSS 循环没生效（或没在 main 前跑）；
  app 侧 crt0 没有清，或清了但被"saved_ra 放在 .bss 后被清掉/自己覆写"。
- 修法：
  - 内核：确认清 BSS 的起止是 `__bss_start`/`__BSS_END__` 符号而不是硬编码地址；
  - app：确认 crt0 的 BSS 循环用 `sb`（app 的 bss 起点常非 8 对齐），且保存现场用的
    `.data` 槽不在 `.bss` 里。
- 预防：`riscv64-unknown-linux-gnu-nm build/main | grep __bss_start` 核对起止；
  Task 3/5 的 bss 断言持续覆盖。

## 4. `bootblock` 超过 512 字节

- 现象：`createimage` 报 bootblock 超一个扇区，或镜像第 0 扇区 print 的字节数 > 512。
- 原因：bootblock 代码 + 字符串加起来太大（它没有 C 栈，不能塞复杂逻辑）。
- 修法：把复杂事情交给内核；字符串精打细算；别在 bootblock 里做除打印外的别的事。
- 预防：Task 1/2 就盯 `readelf -lW build/bootblock` 的 PT_LOAD filesz 之和（当前 0x82=130B，
  见 `make image` 布局表首行）。

## 5. `0x1fc` 约定被破坏

- 现象：bootblock 读到的内核扇区数不是预期值（读到 0、或读到其它程序的位置）。
- 原因：①用过官方 createimage 之外的工具改镜像，或②`createimage` 的 `write_img_info`
  没写/写错位置，或③镜像分区（Task 5）后某文件开头不再是 bootblock。
- 修法：`xxd -s $((0x1fc)) -l 2 build/*.img` 看是不是 `xx xx` 正确值；核对
  `make image` 打印的 `[img info] kernel sectors`。
- 预防：0x1fc/0x1f8/0x1fe 三个偏移在 createimage 里是**集中定义的宏**，不要散写。

## 6. Task 3 之前误跑 `make image` / `make all`

- 现象：镜像做出来但 bootblock 读不到扇区数（0x1fc 没有值），内核起不来。
- 原因：铁律——任务 3 完成前自建 `tools/createimage.c` 的 `write_img_info()` 是**空的**，
  产出的镜像没有信息字段；那时必须用官方 `../createimage` 手工制镜像。
- 修法：把 build/ 清掉（`make clean`），任务 1/2 一律
  `chmod +x ../createimage && cd build && ../createimage --extended bootblock main`。
- 预防：铁律放在了 `log/工作流程.md` 最前面。

## 7. `--extended` 少写一个减号

- 现象：`createimage` 报 `invalid option` 或把参数当文件名处理。
- 原因：选项是**两个减号** `--extended`（Makefile 的 image 规则里已带；手工跑官方
  时常写成一个减号）。
- 修法：`../createimage --extended bootblock main`。
- 预防：`Makefile` 里写对即可；手工命令从 `log/工作流程.md` 复制。

## 8. QEMU 残留进程占用端口

- 现象：第二次 `make run` 起不来，或 `telnet 45454` 连接不上。
- 原因：上一次 QEMU 没被杀干净，占用了 `-monitor telnet::45454`。
- 修法：`pkill -f qemu-system-riscv64`；确认用 `ps -C qemu-system-riscv64`（按进程名），
  **不要**用 `pgrep -af qemu-system-riscv64` 判空——它可能会匹配到你自己的命令行。
- 预防：每个验证脚本先"清理残留 QEMU"再启动，收尾再清一次（`.scratch/verify.sh` 已封装）。

---

## 附加：FAQ 对应的排查路径速查

```
起不来/跑飞/乱码 ───────────────► ① 先确认镜像正确：xxd 0x1f8/0x1fc/0x1fe + make image 布局表
   │
   ├─ 内核没反应 ───────────────► ② bootblock 是否真的跳到了 0x50201000（objdump -d build/bootblock）
   │
   ├─ 进了内核但行为怪 ─────────► ③ init_jmptab() 是否被调用（bios_* 依赖跳转表）／ BSS 是否清
   │
   ├─ app 打不开/找不到 ────────► ④ AppInfo 有没有被 init_task_info 正确读出（gdb p tasks）
   │
   └─ 显示/回显乱 ──────────────► ⑤ -1 跳过；readline 的 \r/\n 双终止；颜色控制符是否被打印
```

每一层都能在 QEMU 崩溃后立即用 `make debug + make gdb` 停在现场，按上面案例的手法
（反汇编数位段、x/ 看内存、nm 核符号）继续往下排。