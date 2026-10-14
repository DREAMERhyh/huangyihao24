# P3 任务1 工作日志：shell 终端与进程管理

> 分支：`Project3`。相关提交：`642cd87`（合并 upstream/Project3 的语义合并基线）、
> `48ed29b`（本任务）。
> 验证环境：QEMU + `loadbootd`（DASICS 地址隔离；U 态访存必须落在
> `[0x52000000, 0x5fffffff]`，否则 scause=0x1a）。

## 任务书要求（§3.3、§3.5）

1. shell 作为 **pid 1** 的第一个用户态进程，内核初始化后直接启动；
2. 读取/解析/回显用户命令，支持退格（BS/DEL）；任务输出与命令输入分区显示；
3. `ps`（`sys_ps`）、`clear`、未知命令报 unknown command；
4. `exec`（A/C-core：按名）、`kill`、`exit`、`waitpid` 四个系统调用；
5. 命令行参数按 argc/argv 传递（放用户栈，a0=argc、a1=argv，16 字节对齐）；
6. `exec` 默认 waitpid 等待任务结束，`&` 则后台运行。

## 改动清单

| 文件 | 改动 |
|---|---|
| `kernel/sched/process.c`（新） | `do_exec/do_exit/do_kill/do_waitpid/do_process_show/do_getpid/do_getchar`；任务栈池（exit 归还、exec 复用）；`init_pcb_stack` 从 main.c 移入并支持 argc/argv |
| `init/main.c` | 删 P2 交互选任务（readline/select_tasks/show_task_list），`init_pcb` 直接 `do_exec("shell",0,0)`；`init_syscall` 挂 7 个新号 + `SYSCALL_CLEAR`→`screen_clear` |
| `include/os/lock.h` + `kernel/locking/lock.c` | `mutex_lock_t` 加 `owner`：kill 一个持锁进程时，内核遍历 mlocks 替它释放并唤醒该锁等待者（design review 明确点） |
| `kernel/irq/irq.c` | 删时钟中断里的 Ctrl+C 轮询（P3 的 shell 是串口唯一消费者，内核再读会"偷走"用户输入）；随之删暖重启机制 |
| `drivers/screen.c` | 补 `screen_write_ch` 退格（左移+擦格）；满屏软件滚动（原实现 cursor_y 加到 50 后写字符越出 4000 字节缓冲） |
| `arch/riscv/crt0/crt0.S` | main 返回后 `call sys_exit`（原 `loop: wfi` 在 U 态是非法指令，且资源永不释放） |
| `include/os/mm.h` | 用户栈区 `0x52500000 → 0x53100000`（P3 的 16 个 app 窗口占满 0x52000000-0x53000000，原栈区与 app 窗口重叠） |
| `kernel/loader/loader.c` | 中转缓冲 8KB→16KB（P3 的 app 带完整 libc，shell 9728B + 扇区内偏移 > 8KB，原缓冲会拒绝加载） |
| `include/os/sched.h` | `pcb_t` 加 `name[TASK_NAME_LEN]`（空闲槽判据 `name[0]=='\0'`） |
| `test/shell.c` | 完整 shell（见下） |
| `tiny_libc/syscall.c` + `include/unistd.h` | exec/exit/kill/waitpid/ps/getpid/getchar/clear 的 invoke_syscall 封装 |

## 关键设计决策

1. **shell 的输入轮询**：`sys_getchar` 无输入返回 -1，shell 此时调 `sys_yield` 让出
   CPU——空转不白占时间片；`switch_to(self,self)` 是安全空操作（现场保存即恢复）。
2. **参数布局（任务书图 P3-4）**：内核在用户栈顶向下依次放 `[0 字|argv 数组|参数字符串]`，
   返回的**初始 sp 落在参数区【之下】**、16 对齐。★ 这是本任务最大的坑：
   main 的栈帧从初始 sp 向下生长，序言里会写 `-80(s0)/-68(s0)` 这类"参数溢出槽"；
   若 sp 设在参数区内部（早期实现把 sp 直接设为指针数组基址），这些写入会覆盖
   argv 元素的高位半字，用户读出 `(argc<<32)|ptr` 形态的非法指针。
3. **进程生命周期与 PCB 复用**：`do_exit` 置 EXITED、唤醒 wait_list 上所有等待者、
   归还两张栈；`alloc_pcb` 优先空槽、池满复用"已退出且无人等待"的槽位
   （刚退出的保留观察窗口，waitpid 查得到）。
4. **do_kill 的资源清理**：① 遍历 mlocks 释放其持有的锁并唤醒该锁等待者；
   ② `list_del` 摘出所在队列（侵入式链表删结点不需要队列头）；③ 唤醒其 wait_list；
   ④ 归还栈；⑤ 若杀的是自己则让出。
5. **mutex owner 字段**：kill 时刻的"谁持有"问题——没有 owner 就无从知道，
   锁会永久锁死。

## 调试战役：DASICS 加载异常（scause=0x1a）

`exec waitpid &`/`exec condition &` 在子任务读 argv 时崩（U-Boot 报
"epc 0, Instruction access fault"）。排查链：

1. `exc_table` 只有 16 项，scause=26（`CAUSE_DASICS_U_LOAD_FAULT`）查表越界
   → 取到 0 → `jalr 0` → 这才是 U-Boot 打印的 "epc 0"；
2. 读 `~/OSLab-RISC-V/qemu/target/riscv/` 的 DASICS 实现与 U-Boot `loadboot.c`：
   loadbootd 把 U 态主域边界设成 `[0xdeadbeee, 0xdeadbeef]`（永不命中），
   所以**U 态每次访存都必须落在库边界 `[0x52000000, 0x5fffffff]` 内**；
3. 崩点 stval=`(argc<<32)|正确指针`——gdb 直读物理内存见 argv 元素高半字
   恰好=argc；比对反汇编确认是 producer 的 main 序言 `sw a5,-68(s0)` 写进了
   argv[1] 元素的高半字（帧与参数区重叠，见设计决策 2）。

## 验收证据（QEMU，逐场景）

| 场景 | 结果 |
|---|---|
| 启动 | 内核直接进 shell（无交互界面），横幅 + `> root@UCAS_OS:` |
| `ps` | `PID 0 idle BLOCKED / PID 1 shell RUNNING`；exec 子任务后 7 个进程全显示、状态正确 |
| `exec waitpid &` | ready_to_exit 拿两把锁→release→退出；wait_locks 阻塞→唤醒→拿 lock1/lock2→退出；waitpid 依次等到（`Task (pid=4) has exited.`） |
| `exec waitpid`（无 &） | shell 阻塞等待；等待期注入的 `ps` 延迟执行，表格 pid2/3/4 全部 EXITED——默认等待行为的直接证据 |
| `exec condition &` | producer/3×consumer 并发跑（嵌套 exec + 5 参数）；produced 3 = consumed 1+1+1 |
| `kill 4` | 目标转 EXITED，ps 实时反映；存活进程不受影响 |
| `clear` | 清屏后 1–20 行全空，提示符回到 SHELL_BEGIN |
| 退格 | `coool`+BS×3+`lear` → 执行 `colear`（buf 与屏幕回显都对） |
| 未知命令 | `xyzzy` → `Unknown command: xyzzy` |

## 遗留事项（后续任务处理）

- ps 输出格式：官方参考图为 `[n] PID : x STATUS : RUNNING mask: 0x3 Running on core N`，
  其中 mask/运行核属于任务3/4 的字段，届时把 ps 格式对齐官方。
- semaphore：任务书只要求 barrier/condition/mailbox（三种同步原语），
  semaphore 骨架保留、不实现（与队友约定一致）。
- 任务3 起 shell/taskset 的多核与绑核语义（cpu_mask 继承等）。
