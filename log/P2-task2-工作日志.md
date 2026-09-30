# P2 任务2 工作日志：互斥锁与阻塞/唤醒

> 分支：`Project2`。提交：`0b4358f`（p2-task2）。
> 验证环境：QEMU + `loadbootd`。

## 任务书要求（§4.3，印刷页 9）

**实验要求**：实现任务的**阻塞和解除阻塞**逻辑；实现一个互斥锁——多个进程同时访问
同一把锁时，后访问的进程被挂起到**阻塞队列**；持有者释放锁后，被阻塞的进程被唤醒、
**再去获取锁**继续执行。注意自己的锁设计能否允许"多个进程同时争抢一把锁"。

**API 规定**（任务书原文要点）：
- `sys_mutex_init(int key)` → 返回 `int handle`；**同 key 同 handle**（在一个 key 的
  生命周期内）；锁刚进入生命周期时未被占有；
- `sys_mutex_acquire(int mutex_idx)`：申请 handle 对应的锁，已被占有就切换去等待；
- （对称地）`sys_mutex_release`：释放并把等待者放回就绪队列。

## 改动清单

| 文件 | 改动 |
|---|---|
| `kernel/locking/lock.c` | `do_mutex_lock_init`（key→handle 映射，handle=槽位下标）、`do_mutex_lock_acquire`（先入队再抢锁）、`do_mutex_lock_release`（置空 + 唤醒队首）、`irq_save/irq_restore` |
| `include/os/lock.h` | `spin_lock_t`、`mutex_lock_t{lock, block_queue, key}`、API 声明 |
| `kernel/sched/sched.c` | `do_block`（置 BLOCKED + 挂队列 + 让出）、`do_unblock`（摘下 + READY + 回就绪队列） |
| `tiny_libc/syscall.c` | `sys_mutex_init/acquire/release` 的用户侧包装 |
| `init/main.c` | `init_locks()`、jmptab 三个锁表项 |
| `test/test_project2/lock1.c lock2.c` | 启用（取消注释） |

## 关键设计决策

1. **`mlocks[16]` + handle = 槽位下标**：`do_mutex_lock_init` 先在表里找相同 key
   （返回已有 handle），否则取第一个空槽（`key = -1` 表示空闲）；handle 与 key 解耦，
   满足"同 key 恒同 handle"且不要求 key 落在合法 handle 值域内。
2. **"先入队、再抢锁"**（本任务的核心设计）：关中断 →
   `list_add_tail(&current_running->list, &lock->block_queue)` →
   `spin_lock_try_acquire`；抢到就 `list_del` 摘回、直接返回；抢不到就置 BLOCKED、
   恢复中断后**直接调 `do_scheduler()`**。这样"检查锁状态"与"入队"之间没有空档，
   **不存在丢唤醒窗口**（反序写法：先发现被占、再入队，中间被抢占且持有者恰好释放
   （看到队列空 → 不唤醒任何人），本进程再去睡就永远没人叫）。
3. **临界区用 `irq_save/irq_restore`**：读 `sstatus` 存原值 → 关中断 → **按原值恢复**。
   不用 `disable_interrupt/enable_interrupt` 直接配对——后者会把内核态 SIE 无条件置 1，
   违反"内核态全程不重开中断"的约定（原因见 design review C2：sscratch 语义会因此错乱）。
4. **释放 = 置空 + 唤醒队首，同一关中断区**：两件事分开做的话，等待者会在"锁已空却
   收不到唤醒"的窗口里长眠。等待者醒来后回到 `while(1)` 顶部**重抢**——也顺带容忍
   虚假唤醒。
5. **`do_block`/`do_unblock` 是通用原语**：`do_sleep`（任务3）挂 `sleep_queue` 复用
   同一套机制；PCB 的 list 结点一结点一用（入阻塞队列时从就绪队列摘除在先）。
6. **公平性**：`do_unblock` 每次只唤醒队首（不是惊群），先到先得。

## 踩坑与修复

- 早期版本"先查锁再入队"，被 `-icount` 之外的抢占窗口抓到丢唤醒风险，改成先入队再抢锁。
- `do_mutex_lock_acquire` 的判据从"锁的 status"改成"try_acquire 的返回值"后，
  自旋锁的原子性由 `spin_lock_try_acquire` 保证（`amoswap`/关中断临界区的读改写）。

## 验证证据

- lock1/lock2 双锁测试：**互斥严格成立**——两侧进入临界区的 burst 窗口零重叠；
  两侧份额均衡（167/169）；等待方绝大多数时间停在 `Applying for a lock.`。
- 换手粒度（1/4 节拍的时间片下复测）：**64% 一次一换**、83% 在 2 次以内
  （更早的 15ms 节拍下是同一方连续 1~16 次，说明换手粒度跟着调度节拍走）。
- 日志与复现见 `.scratch/*.log`（`emu.py --watch 3,4` 可看两行的交接时间线）。

## 结论

任务2 全部要求达成；"先入队再抢锁 + 单唤醒 + 关中断保护"这三个选择让锁在
"多进程争抢 + 抢占式调度"下无丢唤醒、无惊群。数据结构天然支持"一个进程持多把锁"
（每把锁独立等待队列）与"≥3 个进程抢同一把锁"，课程验收实测的是双进程单锁。
