# 05 任务3：自建 createimage、按 taskid 运行多程序

> **读完你能回答的 3 个问题**：① "分配值 32/15" 和 "实际值 4" 各指什么？② loader 怎么用
> taskid 算出要读的 SD 扇区号？③ crt0 为什么把内核的 sp/ra 存进 `.data` 槽而不是寄存器？

> 本小节按 **Task 3 形态**讲述：①createimage 固定扇区布局 + 信息字段；②loader 按
> taskid 读取；③crt0 给用户程序建 C 环境；④main 的 taskid 分发。此后任务把这条
> 链路不断改造。

## 1. 全景

```
Createimage（宿主机工具，Task 3 起用自建版）
   │ 固定扇区布局：bootblock(1) + 内核(KERNEL_FIXED_SECTORS) + 每 app APP_FIXED_SECTORS
   │ 写信息字段：0x1f8=app数 / 0x1fc=内核实际扇区数 / 0x1fe=55aa
   │ 打印每个程序"实际/分配"扇区对照表（实际>分配 → error 退出）
   ▼ build/image
bootblock（不变：0x1fc 读 K 读内核）
   ▼ 内核 main 打印提示，读入一个数字 taskid（0..3）
   │ limit: taskid < task_num（越界 → 报错回到输入）
   ▼ load_task_img(taskid)
   │ block = 1 + 32 + taskid*15；entry = 0x52000000 + taskid*0x10000
   │ bios_sd_read(entry, 15, block)   ← 固定扇区，读满 15 扇区（padding 全是 0）
   ▼ 跳 entry
crt0._start：保存 ra/sp → app 共享栈 → sb 清 BSS → call main → 恢复 → jr ra
   ▼ app main 运行
   （app 返回后跳回内核 main 的循环）
```

## 2. createimage：固定扇区布局 + 信息字段（Task 3 形态）

```c
#define KERNEL_FIXED_SECTORS 32   /* 分配值：内核区固定给 32 个扇区 */
#define APP_FIXED_SECTORS    15   /* 分配值：每个 app 固定给 15 个扇区 */
#define OS_SIZE_LOC          0x1fc
#define IMG_APP_NUM_LOC      (OS_SIZE_LOC - 4)   /* 0x1f8 */
```

**"分配值" vs "实际值"**：内核当时真正只占 **4 扇区**（`make image` 布局表实测
`actual 4 sector(s) / allocated 32 sector(s)`），但 createimage 把内核区**pad 满
32 扇区**、每个 app pad 满 15 扇区。打印对照表（Task 3 真实输出节选）：

```
0x50201000: main        actual  4 sector(s) / allocated 32 sector(s)
0x52000000: 2048        actual 10 sector(s) / allocated 15 sector(s)
...
[img info] kernel sectors : 4 @ 0x1fc
[img info] app number     : 4 @ 0x1f8
[img info] boot signature : 0x55 0xaa @ 0x1fe
```

`actual > allocated` 时 `error()` 直接退出，绝不产出一个塞不下的镜像。

**信息字段为什么是这三处**：
| 偏移 | 内容 | 读取方 |
|---|---|---|
| `0x1f8` | app 数目（2 字节小端） | Task 3 内核直接读内存；**Task 4 后改为 bootblock 传参** |
| `0x1fc` | 内核扇区数（2 字节小端，**实际值**） | bootblock `lh` 读，官方推荐约定 |
| `0x1fe` | 签名 `55 aa` | 官方约定的引导签名，不许覆盖 |

> 早期的教学预设"app 数目放 0x1fe"已被推翻：0x1fe 是官方签名 `55 aa` 的位置，
> 两者冲突，故 app 数定在 **0x1f8**（用户裁决，见 Phase0 报告）。

## 3. loader.c：按 taskid 读（Task 3 形态）

```c
uint64_t load_task_img(int taskid)
{
    uint64_t entry = TASK_MEM_BASE + (uint64_t)taskid * TASK_SIZE;   // 0x52000000+i*0x10000
    uint32_t block = 1 + KERNEL_FIXED_SECTORS + taskid * APP_FIXED_SECTORS;
    if (bios_sd_read((unsigned)entry, APP_FIXED_SECTORS, block) != 0)
        return 0;                 // 失败返回 0（哨兵：合法入口恒 ≥ 0x52000000）
    return entry;
}
```

- **入口为什么 = 窗口基址**：`riscv.lds` 把 `*(.entry_function)`（crt0 的 `_start`）
  排在最前，Makefile 又按 `TEXT_START=0x52000000+i*0x10000` 给每个 app 链接，
  所以每个 app 的 `_start` 恰好落在窗口基址上（实测四个 app 的 e_entry 全等于窗口基址）。
- **那个 `+1` 是给 bootblock 扇区让位**：扇区 0 是 bootblock，内核从扇区 1 开始。
- 读满 15 扇区虽然浪费，但 padding 全 0，不会引入脏数据，且按 taskid 算扇区号极简。

## 4. crt0.S：给用户程序建 C 环境（Task 3 形态，后续不变）

```asm
/* 保存内核现场：放进 .data 槽，而不是寄存器 */
la    t2, saved_ra;  sd ra, 0(t2)
la    t3, saved_sp;  sd sp, 0(t3)
li    sp, APP_STACK_TOP               /* 0x50400000，所有 app 共享 */
la    t0, __bss_start; la t1, __BSS_END__
clear_loop: bge t0,t1,clear_done
    sb zero, 0(t0); addi t0,t0,1; j clear_loop   /* 逐字节清：app bss 常非 8 对齐 */
clear_done: call main
la  t2, saved_sp;  ld sp, 0(t2)
la  t3, saved_ra;  ld ra, 0(t3)
jr  ra
.data
saved_ra: .zero 8
saved_sp: .zero 8
```

| 决策 | 为什么 |
|---|---|
| `.data` 槽放 sp/ra | a0 会被 app main 冲掉；s 寄存器违反被调用者 ABI（内核 main 的 s 寄存器跨 app 存活）；槽放 `.bss` 会被清 BSS 抹掉 |
| 共享栈顶 0x50400000 | 内核栈 = 0x50500000 向下，独享 0x50400000–0x50500000；app 栈向下还有 ~2MB；app 串行执行，共享安全。（任务书建议"每 app 窗口顶"，我们改共享并说明理由，见 design review 声明偏差） |
| `sb` 而非 `sd` | app 的 `__bss_start` 可能非 8 对齐（实测 auipc 在 0x5201027c、仅 4 字节 bss），`sd` 会越界写坏后面内存 |
| 全程只碰 ra/sp/t0/t1/t2/t3 | 不碰 s0–s11 是"内核的 s 寄存器跨 app 存活"的唯一手工保证（四 app 反汇编核验均为 0 处） |

## 5. main.c：taskid 分发（Task 3 形态）

```c
while (1)
{
    bios_putstr("Please input a task id: ");
    int id = read_taskid();            // 读数字并回显；-1 继续；非数字只回显
    if (id >= task_num) {              // task_num = 镜像 0x1f8 读出
        bios_putstr("Error: task id out of range!\n\r");
        continue;
    }
    uint64_t entry = load_task_img(id);
    if (entry == 0) { bios_putstr("Error: load task img failed!\n\r"); continue; }
    ((void(*)(void))(uintptr_t)entry)();   // （Task 3 曾用此写法，见 09 的踩坑实录）
}
```

边界：`taskid ∈ [0, task_num)`，越界打印错误并 `continue`，绝不跳转——这就是验收
"输入越界 id 被拒绝，全程无跑飞"。

## 6. 验证与验收（Task 3）

注入：`1`(auipc) → `2`(bss) → `3`(data) → `0`(2048) → 回车 → `q` 退出 → `4`(越界) → `x`(非数字)。
日志（`.scratch/task3-qemu.log` 节选）：

```
Please input a task id: 1[auipc] Info: testpc = 0x52010064
Please input a task id: 2[bss] Info: passed bss check!
Please input a task id: 3[data] Info: checking data!
[data] Info: checking data --- [OK]!
...
[2048]: Exit 2048 game ...
Please input a task id: 4Error: task id out of range!
```

- 4 个 app 全部可跑、越界拒绝、非数字忽略、2048 q 退出后成功回到内核（crt0 返回）。
- Task 3 实测内核扇区数 = 4（供 Task 4 紧凑版对比）。

## 7. Task 3 后变更速记

- **Task 4**：createimage 取消固定扇区 → 紧凑排列 + AppInfo；loader 不再按 taskid
  公式算扇区，改从 `tasks[i].sector_num/start_sector` 读；main 改 `readline` 按名匹配。
- **Task 5**：双镜像，`start_sector` 变为 apps.img 相对扇区，loader 加 `boot_apps_base`；
  main 增加 `list` 命令。
- `KERNEL_FIXED_SECTORS / APP_FIXED_SECTORS` 两个宏在 Task 4 后**退役删除**。
---

## 覆盖率自检表（05）

> 行号区间：**当时（Task 3）形态**的 createimage.c / loader.c / crt0.S / main.c。
> Task 3 后 createimage 与 main.c 已被大幅改造，逐任务对照请结合 `log/task3-工作日志.md`。

| 源文件块（当时行号区间） | 讲解方式 | 归组理由 |
|---|---|---|
| createimage：`KERNEL_FIXED_SECTORS/APP_FIXED_SECTORS` 宏 | 逐条 | "分配值"是本任务概念核心（32/15 vs 实际 4） |
| createimage：`write_img_info`（写 0x1f8/0x1fc/0x1fe）| 逐条 | 信息字段三处偏移语义，必须逐条讲清 |
| createimage：padding/对照表（actual/allocated + `actual>allocated→error`）| 归组 | 一段流程而非单行语义，整段讲"固定槽位+越限即错" |
| loader：`block = 1+32+taskid*15` 与 `entry = 0x52000000+i*0x10000`（两行）| 逐条 | 扇区公式是 loader 唯一正确性来源（含 bootblock 扇区让位的 +1 解释） |
| loader：`bios_sd_read(...)` 与 `return 0/entry` | 归组 | 读+返回值是公式的落点，配合上一行 |
| crt0：`sd ra→saved_ra`/`sd sp→saved_sp`（两行）| 逐条 | 现场保存策略是本任务最关键设计（为什么 .data 而不 .bss/不借寄存器） |
| crt0：`li sp,APP_STACK_TOP`（hex）| 逐条 | 共享栈决策要单独论证（0x50400000 vs 0x504fffff） |
| crt0：BSS 清零（`sb` 循环）| 逐条 | sb vs sd 的越界风险需要逐句展开 |
| crt0：`call main` + 恢复 + `jr ra` | 归组 | 返回链路与保存是对称结构，合并讲清往返 |
| main.c：`read_taskid` + 越界判断 + `load_task_img(id)` 调用 | 逐条 | 越界保护（id<task_num）是验收点，必须逐条 |
