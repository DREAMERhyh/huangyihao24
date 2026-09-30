# P2 任务1 工作日志：PCB 与轮转调度

> 分支：`Project2`。主线提交：`34c3d65`（p2-start 合并起点代码）、`fd5b7de`（p2-task1）。
> 后续修正：`fc7171e`（提示语粘连）、`c6057e4`（pid0 退出轮转，见 task4 日志）。
> 验证环境：QEMU + `loadbootd`（DASICS 地址隔离）。

## 任务书要求（§3.8，印刷页 7）

**实验要求**：
1. 设计进程相关的数据结构（如 PCB），对 PCB 进行初始化；
2. 实现任务的 `switch_to` 切换；
3. 同时运行 "print1"、"print2"、"fly" 三个测试任务，正确输出（参考图 P2-2：两个
   计数行进度接近 + 飞机自左向右移动）。

**实验步骤（概括）**：先 `git merge` 拿到 Project 2 分支的骨架（P1 的代码要保留）；
实现内核自己的链表 API；实现 `init_pcb`/`do_scheduler`/`switch_to`；用**跳转表
（jmptab）**的简易形式接通 `sys_yield` / `sys_move_cursor` / `sys_write` /
`sys_reflush`；最后把测试程序里 `sys_yield` 的注释取消，让非抢占调度转起来。

## 改动清单

| 文件 | 改动 |
|---|---|
| `include/os/list.h` | 自建链表 API：`list_init / list_add / list_add_tail / list_del / list_empty / list_entry`（+ `offset_of`），插入删除全 O(1) |
| `include/os/sched.h` | `pcb_t`（kernel_sp/user_sp 必须在前两个字段，见 `asm/regs.h`）、`regs_context_t`（32 GPR + 4 CSR = 288B）、`switchto_context_t`（14 个 callee-saved） |
| `kernel/sched/sched.c` | `do_scheduler`：RR 轮转（RUNNING→READY 挂到队尾、取队首）；`do_block/do_unblock` 骨架；pid0 空转兜底 |
| `init/main.c` | `init_pcb`（每任务一套内核栈 + 用户栈）、`init_pcb_stack`（伪造首个现场）、`init_jmptab`、`select_tasks`（交互按名选任务，P1 保留的定制） |
| `arch/riscv/kernel/entry.S` | `switch_to`：保存 14 个 callee-saved 到 PCB，恢复 next 的现场 |
| `tiny_libc/syscall.c` | 四个系统调用先走 jmptab 简易通道（任务3 换成 ecall） |
| `arch/riscv/crt0/crt0.S` | 去掉 P1 的"自设栈顶 + 保存 ra"机制（每个任务改用内核分配的独立用户栈） |
| `Makefile` | 恢复 `-std=gnu11`；app 加载窗口改为**按名字**确定性分配（修增量构建撞车） |
| `include/os/kernel.h` | jmptab 增加刷屏项（题面给的表里没有 `SCREEN_REFLUSH`） |

## 关键设计决策

1. **`tp == current_running`**：`current_running` 声明为 `register pcb_t * ... asm("tp")`，
   编译器保证对它的赋值直接写 tp 寄存器；陷阱帧里也保存/恢复 tp。全内核只此一份真相。
2. **每个任务一套栈**：内核栈给"陷入后的现场"用，用户栈给程序自己的 C 栈用；二者
   各 4K、从 `mm.c` 的分配器取（内核动态区 0x50500000 起、用户动态区 0x52500000 起）。
3. **`switch_to` 只保存 callee-saved**（ra/sp/s0-s11 共 14 个）：调用者保存的寄存器已由
   编译器在 C 调用边界处理，这是上下文切换的最小集。
4. **`init_pcb_stack` 伪造现场**：新任务没跑过，在内核栈上假造两层——
   任务1 版是"ra=程序入口、sp=用户栈顶"；任务3 改成"ra=`ret_from_exception`、
   sp=陷阱帧基址"（让新任务的第一次运行和被抢占后回来走同一条路，见 task3 日志）。
5. **pid0 内核兜底**：内核自己也有 PCB（调度、屏幕光标都要经 `current_running`）；
   所有用户任务阻塞时由它上场怠速（后来改成"不参与轮转、只在就绪队列为空时上场"，
   原因见 task4 日志的 20% 偏袒排查）。
6. **app 加载窗口按名字分配**（Makefile 的 `app_addr` 函数）：此前按"本次构建顺序"
   累计，增量编译时只重链改动的 app，会拿到已被占用的窗口——两个 app 撞进同一块
   地址，表现为"某个任务跑的是别人的代码"。

## 踩坑与修复

- **GCC 15 默认 C23**：`typedef int bool` 报错 → CFLAGS 恢复 `-std=gnu11`。
- **增量构建的 app 撞车**：见上，改成按名字确定性分配后解决。
- **提示语粘连**（`fc7171e`）：交互选任务时 readline 收到回车后没有换行，下一行提示
  会接在同一行尾 → 回车时补 `"\n\r"`。

## 验证证据

- 三任务并发（print1/print2/fly）：两行计数基本同步增长、飞机在 5~11 行自左向右移动
  并循环（同图 P2-2 的形态）。
- 计数差的量化（在 task4 修掉 pid0 偏袒后的多次复跑）：7004/7035、5143/5189，差
  0.4~0.9%（见 `P2-总结报告.md` §二）。
- 复现命令：`make run` → U-Boot 下 `loadbootd` → 一行一个任务名 → 空行开始。

## 结论

任务1 的全部要求（PCB、switch_to、三任务并发）达成；链表 API、按名分配加载窗口、
交互选任务这三点是超出任务书最低要求但被后续任务反复受益的基础设施。
