# P3 任务2 工作日志：barrier / condition / mailbox 同步原语

> 分支：`Project3`。相关提交：`64181ca`（内核原语 + 用户侧封装）、
> `d8f31fd`（系统调用表挂载 12 条）。
> 验证环境：QEMU + `loadbootd`（单核场景）与 `loadbootm`（后并入双核回归）。

## 任务书要求（§4.3、§4.5）

1. 在 mutex（任务1 已有）之外，实现 **barrier**（3 个）、**condition**（5 个）、
   **mailbox**（4 个）共 12 个系统调用；semaphore 按任务书属选做范围，不实现；
2. 原语的阻塞/唤醒必须与调度器协作：条件不满足时"挂队 + 置 BLOCKED + 让出"，
   唤醒后**重新判条件**（容忍虚假唤醒），绝不丢唤醒；
3. mailbox 是**字节流**语义：接收方分多次 `recv`、凑满一段 `msg_length` 才整段取走；
4. `recv/send` 的返回值按任务书 §4.5 =**本次是否发生过阻塞**（0/1），
   测试程序把返回值累加成 `blocked` 计数。

## 改动清单

| 文件 | 改动 |
|---|---|
| `include/os/lock.h` | `barrier_t`（goal/arrived + wait_list）、`condition_t`（wait_list）、`mailbox_t`（name / 环形字节缓冲 head+tail+count / refcount / 收发两条等待队列）；semaphore 骨架保留 |
| `kernel/locking/lock.c` | `block_on_queue()` 公共阻塞骨架；barrier/condition/mailbox 全套实现 + `init_barriers/init_conditions/init_mbox`（`init_locks` 里顺带调用） |
| `init/main.c` | `init_syscall()`：BARR 44-46、COND 47-51、MBOX 52-55 共 12 条挂表 |
| `tiny_libc/syscall.c` | 12 个 `invoke_syscall` 用户侧封装（号表与 `include/syscall.h`、`kernel/unistd.h` 三处一致） |

## 关键设计决策

1. **一条公共骨架、三句纪律**（lock.c 顶部长注释）：①「判条件、改状态、（必要时）
   挂进等待队列」必须在**同一段关中断窗口**里完成——定时器抢占插在中间要么丢唤醒、
   要么让一个 PCB 同时挂在两个队列上（pcb 只有一个 `list` 结点，链表会被串坏）；
   ② 恢复中断之后才允许 `do_scheduler`（关着中断切换会把 SIE=0 带进下一个任务）；
   ③ 被唤醒后回循环开头重判条件。
2. **唤醒策略一律"全唤醒 + 醒来重查"**：不做精确点名。规模小（个位数等待者），
   但换来"绝不丢唤醒、绝不点错人"——对条件变量尤其重要（broadcast 语义）。
   barrier 例外：只有"最后一名到达者"不睡，它负责清 arrived（供下一轮）+ 全唤醒。
3. **condition_wait 的原子性**：在单个关中断窗口里完成「挂条件队列 → 释放互斥锁 →
   置 BLOCKED」——先放锁再挂队会让 producer 在缝里 broadcast，则该 consumer 永久睡死。
   被唤醒后重新 acquire 互斥锁再返回（把"重新抢锁"留给用户态语义）。
   `mutex_release_locked()` 拆成单独函数正是为此：condition_wait 在自己的临界段里
   放锁，直接调完整版会重复拿 `sync_lock`（自旋锁不可重入，会死锁）。
4. **mailbox 按名 open + refcount 共享**：同名的收发双方拿到同一个 handle；
   close 递减、归零才真正销毁并全唤醒。
5. **阻塞判据严格按任务书**：send 阻塞 ⟺ `msg_length > 剩余空间`（不是"放不下"）；
   recv 阻塞 ⟺ 缓冲内不足 `msg_length` 字节。**字节流语义**：server 一次 send
   被 client 分两次 recv（先 12B 头、再正文）依赖它——不能按"消息"为单位交付。

## 调试战役

1. **"名字反着"的坑**：任务书的 mbox 场景里 `mbox_server` 实际是**收方**、
   `mbox_client` 是**发方**（与直觉相反）；同类地，barrier 侧
   `worker/launcher` 的命名与课件图也有出入。写日志/验收脚本前先以
   `test/test_project3/` 源码为准逐行核对，避免按名字臆断方向。
2. **condition 测试没有终点**：producer 10 轮后 EXITED，3 个 consumer 是死循环。
   验收脚本必须自带收场（ps 看状态 + kill），不能等"程序自己结束"。
3. **系统调用号两侧同步**：内核 `init_syscall` 与用户侧封装必须同一次提交里改，
   否则会出现"编过、跑飞"（号错位后调用到别的原语上）。号表三处
   （`init/main.c`、`include/syscall.h`、`tiny_libc` 侧）已互相对齐并复核。

## 验收证据（QEMU，loadbootd 单核）

| 场景 | 注入 | 结果 |
|---|---|---|
| barrier | `exec barrier &`，45s，`--watch 1,2,3` 时间线 | 三个 `test_barrier` 的 `(i)` 计数**严格并排增长**，每轮全部凑齐才一起推进，10 轮后三行同时收尾——屏障语义成立 |
| condition | `exec condition &`，30s + `ps` | `produced 30 = consumed(10+11+9)` 守恒、无负值；producer 10 轮后 EXITED，consumers 如实 BLOCKED 在条件上 |
| mbox | `mbox_server` + 2×`mbox_client`，40s + `ps` | server `errorBytes` 恒 **0**、`correctBytes=706` 持续增长；两 client `send bytes` 合计≈server 收量、`blocked=5` 随邮箱积压增长；ps 中 server/client 状态如实 |

## 遗留事项

1. **semaphore 未实现**（任务书选做范围，骨架/int 号保留）；
2. condition/mbox 测试常驻不退出，验收统一以 `ps` + `kill` 收场（kill 一个正阻塞
   在 wait/send 的进程由任务1 的 owner 回收 + 任务2 的队列纪律共同保证摘干净）；
3. 细粒度锁版本的域划分（sync_lock/sched_lock）在任务5 完成，本节所有原语
   已在任务5 复核过锁序（sync→sched→console）。
