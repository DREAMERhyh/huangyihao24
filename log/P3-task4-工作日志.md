# P3 任务4 工作日志：taskset 动态绑核 + ps 扩展

> 分支：`Project3`。相关提交：`8012077`。
> 验证环境：QEMU `-smp 2` + `loadbootm`。

## 任务书要求（§5.5）

1. `taskset <mask> <name>`：按掩码启动任务；`taskset -p <mask> <pid>`：改运行中
   任务的掩码（这个系统调用任务书**没给号**，自定 57）；
2. `ps` 输出官方图 P3-8 格式：`[i] PID : n  STATUS : X mask: 0xN [Running on core N]`
   （`Running on core N` 只对 RUNNING 打）；
3. 掩码语义：bit(cpu) == 1 才允许在该核上跑；改掩码后要能**跨核迁移**。

## 改动清单

| 文件 | 改动 |
|---|---|
| `include/os/sched.h` | `pcb_t` 加 `cpu_mask`；`do_exec` 增第 4 参（掩码） |
| `kernel/sched/sched.c` | `pick_next(cpu)` 只挑掩码允许本核的任务；全被挡时本核 idle、任务留给别的核；idle 掩码 `CPU_MASK_ALL` |
| `kernel/sched/process.c` | `do_exec`：显式掩码非 0 用之、为 0 **继承父进程**（affinity 的 5 个子任务靠继承拿到 0x1）；`do_taskset_p`（拒绝 mask==0、不许改 idle）；`do_process_show` 重写为官图格式 |
| `init/main.c` + 两侧号表 | `SYSCALL_TASKSET` 57 挂表 |
| `test/shell.c` | `taskset <mask> <name>` / `taskset -p <mask> <pid>`；成功回显 `taskset ... .`（含句点，与官图逐字一致）；启动形式不阻塞 shell；exec 提示补全 `Info: execute %s successfully, pid = %d ...` |
| `tiny_libc/syscall.c` | `sys_exec_mask`、`sys_set_affinity` 封装 |

## 关键设计决策

1. **过滤放在 pick_next，而不是维护每核就绪队列**：全局就绪队列 + "只挑允许本核的"
   过滤，天然支持跨核迁移——核 0 上被掩码挡下的任务留在队列里，核 1 的
   pick_next 下一轮就能捞走它，不需要任何迁移代码。
2. **掩码继承**：`exec` 不给掩码时继承父进程。affinity 测试的主任务被
   `taskset 0x1` 拉起后，它 exec 的 5 个子任务自动全是 0x1——这正是任务书
   "子任务也受限"的验收点。
3. **拒绝非法掩码**：`taskset -p 0` 直接拒绝（掩码为 0 的进程谁都跑不了，
   会永远留在就绪队列里）；idle 不允许被改（它不走 pick_next，改了没有意义
   还会误导 ps）。
4. **`Running on core N` 的查法**：遍历 `core_running[]` 反查（本核的
   `current_running` 是 tp 寄存器，别的核只能查表）——这张表在任务3 建立、
   任务5 用交棒把它变成严格判据。
5. **格式细节**：STATUS 列 7 字宽（内核 printk 没有 `%-7s`，右补空格手工实现）；
   mask 用小写十六进制；**不列 idle**（官图没有；任务3 期间 ps 曾打两行 idle 查
   从核状态，本次重写后去掉）；EXITED 的进程如实列出（waitpid 还看得见）。

## 验收证据（QEMU，loadbootm `-smp 2`）

| 项 | 结果 |
|---|---|
| 掩码继承 | `taskset 0x1 affinity` 后 `ps`：5 个子任务 mask 全是 0x1 ✓ |
| 代码共享协议 | 5 个 `test_affinity` 打印的 `auipc` 完全相同（0x520c0104）——同一地址空间共享一份代码映像 ✓ |
| 动态改绑 | `taskset -p 0x2 <pid>` 后 mask 变 0x2；`--watch` 时间线显示该子任务进度斜率远超其他（快照 103 vs 72），改绑后几乎每打印帧都在推进 ✓ |
| 跨核迁移抓帧 | `taskset 0x1 fly` → `-p 0x2` 后 `ps` 连续稳定显示 `[1] PID : 2  STATUS : RUNNING mask: 0x2 Running on core 1`，同屏对照 shell 的 `Running on core 0` ✓ |
| 官图对齐 | ps 表格逐字段与 P3-8 一致（表头、PID/STATUS/mask 列宽、小写十六进制、仅 RUNNING 打核号）✓ |

## 遗留事项

1. `taskset -p` 改掩码**不触发立即迁移**：任务留在原核跑完当前时间片，
   等它自然被换下、下一个核 pick_next 时按新掩码捞走（实测抓帧可见，
   秒级收敛）。做"立即踢核"需要 IPI，课程规模不值得。
2. 掩码是"允许集合"而非"绑定集合"：mask: 0x3 意味着两核都能跑（默认），
   官方图默认值也是 0x3。
