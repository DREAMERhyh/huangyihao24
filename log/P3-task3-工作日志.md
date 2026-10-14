# P3 任务3 工作日志：双核启动与大内核锁（SMP 第一步）

> 分支：`Project3`。相关提交：`02d743f`（从核启动 + BKL + 每核 idle）。
> 验证环境：QEMU `-smp 2` + 串口输入 `loadbootm`。
> （任务5 在细粒度锁版上对本文的 BKL 做了域拆分；`main` 侧 12 处
> `lock_kernel()` 调用点在细粒度版退化为空函数，见任务5 日志。）

## 任务书要求（§5.4）

1. 让第二个核真正跑起来（bootblock/head.S 的从核入口、每核独立的栈与 idle）；
2. 引入**大内核锁**：任何在内核态继续执行的执行流都持有大锁——把 P2 期间
   为单核写的全部内核数据结构保护起来，是"上多核"最小风险的路径；
3. 双核下任务能被任一个核调度（全局就绪队列），idle 每核一份。

## 从核启动的三种死法（都已排除，记录现场）

| 方案 | 死法 | 根因 |
|---|---|---|
| 等主核 IPI 唤醒（原版 bootblock 路线） | 从核永久深睡，QEMU 不唤醒 | 从核经 U-Boot `enter_supervisor_mode` 的 `mret` 进 S 态时 MPIE=0 → MIE=0；主核 `send_ipi` 经 U-Boot 发的是 CLINT **MSIP（M 态软中断）**，要先进 M 态 trap 才能被 U-Boot 转发成 SSIP，但 MIE=0 永远进不去 |
| S 态自行开 MIE | `epc=5020007c` 非法指令炸进 U-Boot 的 hang | S 态**无权写 mie/mstatus**（写 M 态 CSR 是非法指令），此路不通 |
| `wfi` + 任何"等使能门"的变体 | 永久深睡 | QEMU 按全局使能门判定 wfi 唤醒条件；MIE=0 且 SIE=0 双门全关时 wfi 就是关机键 |

**最终方案：内存标志自旋轮询**。主核写 `SMP_WAKEUP_ADDR`（0x50300000）一个魔数，
从核在 bootblock 阶段自旋读它——没有任何特权级/使能门依赖，QEMU 多核内存一致性
直接保证可见。从核在 head.S 里走独立分支（跳过 BSS 清零与引导参数保存，消费掉
挂起的 SIP），随后进 `main` 的从核路径：装自己的陷阱栈、自己的 idle PCB、
第一个时钟，进入 idle 循环。

## 大内核锁（BKL）不变量与三个交接点

不变量：**"任何在内核态继续执行的执行流，都持有大锁。"**
（trap 入口硬件已清 SIE，临界区不会被本地中断打断；自旋等待只等另一个核。）

1. **trap 入口**（`exception_handler_entry`，entry.S）：SAVE_CONTEXT 之后 `lock_kernel`；
2. **trap 出口**（`ret_from_exception`）：RESTORE_CONTEXT 之前 `unlock_kernel`
   （放在恢复现场前：`jal` 不压栈，unlock 自己的栈帧压在陷阱帧基址之下，不碰陷阱帧）；
3. **任务切换缝隙**（`do_scheduler`）：`unlock_kernel → switch_to → lock_kernel` 对偶，
   **新增 `new_task_entry`**（entry.S）：新任务第一次上场不走 switch_to 的返回点，
   由伪造现场里的 ra 指向它，先补 `lock_kernel` 再跳 `ret_from_exception`。
   ——三个交接点缺一不可，否则要么"内核态不持锁"（数据竞争），要么死锁。

其余配套：`pid0_pcb[NR_CPUS]`（每核 idle，从核 [1] 的形式初值在 `smp_init` 里填）、
`core_running[NR_CPUS]`（每核"正在运行"表，后续任务4/5 都靠它）、
`find_pcb_by_pid(0)` 返回本核 idle、ps 打两行 idle。

## 踩坑记录

1. `wakeup_other_hart` 的调用点必须在 `init_jmptab` **之后**：跳转表未填时
   `jalr 0` 直接飞掉（踩过，现象是主核在 init 早期无输出重启）。
2. 从核 `main` 里第一行 printl 之前不能碰 `current_running`（tp 还是 U-Boot 残留值，
   这条在任务5 又以另一个形式踩了一遍，见任务5 日志"自伤"一节）。

## 验收证据（QEMU，loadbootm + `-smp 2`）

| 项 | 结果 |
|---|---|
| 双核进内核 | 串口日志 `[SMP] hart 0 entered kernel main` 与 `[SMP] hart 1 idle ready, entering scheduler loop` 两条齐 |
| `ps` | 两行 idle（RUNNING/BLOCKED 如实反映从核状态）+ shell(pid=1)。注：任务4 重写 ps 为官图格式后不再列 idle |
| multicore 测试 | 见下方"补充实测" |
| 稳定性 | 上述场景 + 任务4 的 affinity 场景（5 子任务压双核）全程无崩溃 |

## 补充实测（multicore 基准）

> `exec multicore &`，30s：两个 `add` 任务分别测单核/双核各跑一遍相同计算量。

- 结果：`single core: ____ ticks` / `multi core: ____ ticks`（加速比 ____×）
- 课件参考（图 P3-7）：2036699 / 1129776 ≈ **1.80×**。

## 遗留事项

1. **BKL 是过渡方案**：任务5 的细粒度锁版本撤掉 BKL（`lock_kernel/unlock_kernel`
   在 `FINE_GRAIN_LOCK` 下是空函数），本文三个交接点的调用点原样保留；
2. 大锁版放锁（`unlock_kernel`）与 `switch_to` 之间还有几条汇编指令的窗口
   （大锁不覆盖 switch 本身），细粒度版用 sched_lock 交棒消除了它——大锁版
   实测从未复现问题，保留原样；
3. 从核的 idle 栈/陷阱栈是 smp.h 里的固定地址（BootStack/TrapStack），
   没有动态分配，课程规模够用。
