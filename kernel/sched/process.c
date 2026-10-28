#include <os/sched.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/task.h>
#include <os/loader.h>
#include <os/string.h>
#include <os/kernel.h>
#include <common.h>
#include <csr.h>
#include <printk.h>
#include <assert.h>

extern void ret_from_exception();
/* P3 任务3：新任务的首次上场入口（entry.S）——先补拿大内核锁再走公共返回路径 */
extern void new_task_entry();

/* 临时诊断（板测排查 consumer_rate 卡死用，定稿前删除）：定义在 sched.c */
extern uint64_t diag_beat[NR_CPUS];
extern uint64_t diag_wp_set;
extern uint64_t diag_wp_heal;
extern pid_t    diag_wp_heal_pid;
extern int      diag_wp_heal_cpu;

/* =====================================================================
 * P3 任务1：进程管理（exec / exit / kill / waitpid / ps / getpid）
 *
 * 全都在内核态、由系统调用进来时执行。抢占内核里定时器中断在 S 态是关的
 * （陷进来时硬件清 SIE，sret 才按 SPIE 恢复），所以这些函数对定时器是原子的；
 * 到任务3 双核时再整体用大内核锁保护（多核才是真并发）。
 * ===================================================================== */

#define EXEC_MAX_ARGC 8          /* 命令行参数个数上限（防用户乱传） */
#define ARG_AREA_LIMIT 2048      /* 参数区最多往下占用户栈一半（4KB 栈） */

/* ---------------------------------------------------------------------
 * 任务栈池：exit/kill 时把栈还进池子，exec 优先从池子取。
 * mm.c 的分配器只前进不回收，进程反复 exec 会一路吃内存；这里给
 * 内核栈/用户栈各建一个小 free list（课程规模 16 个任务够用，
 * 溢出的栈不再入池——对应"池满即弃"）。 */
#define STACK_POOL_MAX NUM_MAX_TASK
static ptr_t kstack_free[STACK_POOL_MAX];
static int   kstack_nfree;
static ptr_t ustack_free[STACK_POOL_MAX];
static int   ustack_nfree;

static ptr_t task_stack_alloc(int is_kernel)
{
    if (is_kernel) {
        if (kstack_nfree > 0)
            return kstack_free[--kstack_nfree];
        return allocKernelPage(1);
    }
    if (ustack_nfree > 0)
        return ustack_free[--ustack_nfree];
    return allocUserPage(1);
}

static void task_stack_free(ptr_t base, int is_kernel)
{
    if (base == 0)
        return;
    if (is_kernel) {
        if (kstack_nfree < STACK_POOL_MAX)
            kstack_free[kstack_nfree++] = base;
    } else {
        if (ustack_nfree < STACK_POOL_MAX)
            ustack_free[ustack_nfree++] = base;
    }
}

/* ---------------------------------------------------------------------
 * PCB 定位 / 分配 */
static int is_idle_pcb(pcb_t *t)
{
    return t >= &pid0_pcb[0] && t < &pid0_pcb[NR_CPUS];
}

static pcb_t *find_pcb_by_pid(pid_t pid)
{
    if (pid == 0)
        return &pid0_pcb[get_current_cpu_id()];   /* pid0：返回本核的 idle */
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].name[0] != '\0' && pcb[i].pid == pid)
            return &pcb[i];
    }
    return 0;
}

static pcb_t *alloc_pcb(void)
{
    /* ① 全新的空槽位 */
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].name[0] == '\0')
            return &pcb[i];
    }

    /* ② 池满：复用"已经退出、没人还在 wait 它、且不在任何核上被使用"的 PCB。
     *   数组顺序即近似 FIFO，优先复用最早退出的——刚退出的先留存一会儿，
     *   给 waitpid 留出观察窗口。多核下用 task_on_cpu 排除全部核。
     *   ★ 到这一刻才归还它的两张栈：以前在 do_exit 里就还，等于把自己的
     *     立足之地提前交给下一个 exec 去踩（做完 exit 的最后一段调度与
     *     switch_to 全踩在内核栈上）。task_on_cpu 为假 = 已完整下核——
     *     细粒度版整个 switch_to 都在 sched_lock 内（交棒），本函数也在锁内。 */
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].status == TASK_EXITED && list_empty(&pcb[i].wait_list)
            && !task_on_cpu(&pcb[i])) {
            task_stack_free(pcb[i].kernel_stack_base, 1);
            task_stack_free(pcb[i].user_stack_base, 0);
            pcb[i].kernel_stack_base = 0;
            pcb[i].user_stack_base = 0;
            return &pcb[i];
        }
    }

    return 0;   /* 都在用：exec 失败 */
}

/* ---------------------------------------------------------------------
 * 用户栈上的命令行参数打包（任务书图 P3-4 的布局）：
 *
 *   高地址 ┌──────────────┐  ← 用户栈顶（ustack_top）
 *          │ 0 字（NULL）  │   ① argv[argc] = NULL 的终止格
 *          ├──────────────┤
 *          │ argv[n-1]    │   ② 指针数组紧贴其下（argv[0] 在最低处）
 *          │     ...      │
 *          │ argv[0]      │
 *          ├──────────────┤
 *          │ 参数字符串们  │   ③ 各参数字符串（含 '\0'）再往下放
 *          ├──────────────┤
 *          │ 对齐填充      │   ④ sp 取参数区【之下】、16 对齐的位置
 *   低地址 └──────────────┘  ← 返回的 sp（即 regs[2]：任务 main 的初始 sp）
 *
 * ★ 为什么 sp 必须落在参数区【之下】（踩过的坑，务必保留此注释）：
 *   main 的栈帧从初始 sp 向下生长，但它【不会】碰 sp 之上的任何东西——
 *   所以参数区放在 sp 之上是安全的。反过来（如早期实现把 sp 直接设在
 *   指针数组基址），main 一进门 `addi sp,sp,-80` 就把帧铺在参数区上，
 *   序言里的 `sd a1,-80(s0)`、`sw a5,-68(s0)`（保存 argc/argv 的溢出槽）
 *   会覆盖 argv 元素的高位半字，拼出 (argc<<32)|ptr 这种非法指针，
 *   表现为进入 atoi(argv[i]) 时 DASICS 加载异常（scause=0x1a）。
 *
 * 返回 0 表示失败（参数太长/指针非法），调用方要负责回收。
 * argc == 0 时不调用本函数，argv_va 保持 0、sp 取栈顶。 */
static int pack_args(ptr_t ustack_top, int argc, char **argv, uint64_t *argv_va,
                     ptr_t *sp_out)
{
    ptr_t sp = ustack_top;
    uint64_t ptrs[EXEC_MAX_ARGC];

    /* ① 终止格 */
    sp -= sizeof(uint64_t);
    if (sp < ustack_top - ARG_AREA_LIMIT || sp >= ustack_top)
        return 0;
    ((uint64_t *)sp)[0] = 0;

    /* ② 预留指针数组的空间（稍后回填） */
    ptr_t arr_base = sp - (uint64_t)argc * sizeof(uint64_t);
    if (arr_base < ustack_top - ARG_AREA_LIMIT)
        return 0;
    sp = arr_base;

    /* ③ 参数字符串（用户指针直接读，无虚存） */
    for (int i = 0; i < argc; ++i) {
        char *arg = argv[i];
        if (arg == 0)
            return 0;
        uint64_t len = strlen(arg) + 1;
        sp -= len;
        if (sp < ustack_top - ARG_AREA_LIMIT || sp >= ustack_top)
            return 0;
        memcpy((uint8_t *)sp, (const uint8_t *)arg, (uint32_t)len);
        ptrs[i] = sp;
    }

    for (int i = 0; i < argc; ++i)
        ((uint64_t *)arr_base)[i] = ptrs[i];

    /* ④ 栈起点：参数区之下、16 对齐（RISC-V psABI 要求） */
    sp &= ~0xFULL;
    if (sp < ustack_top - ARG_AREA_LIMIT)
        return 0;

    *argv_va = arr_base;
    *sp_out = sp;
    return 1;
}

/* ---------------------------------------------------------------------
 * 伪造新进程的陷阱帧 + switch_to 帧（任务1 起从 init/main.c 移到这里，
 * 并支持把 argc/argv 塞进 a0/a1）。
 *   细节见原注释：陷阱帧让 ret_from_exception 直接 sret 进用户态；
 *   switchto 帧让 switch_to 的 jr ra 跳到 ret_from_exception。 */
static void init_pcb_stack(ptr_t kernel_stack, ptr_t user_stack,
                           ptr_t entry_point, pcb_t *pcb,
                           int argc, ptr_t argv_va)
{
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));

    memset((void *)pt_regs, 0, sizeof(regs_context_t));

    pt_regs->regs[2]  = (reg_t)user_stack;    /* sp：该任务自己的用户栈顶 */
    pt_regs->regs[4]  = (reg_t)pcb;           /* tp == current_running */
    pt_regs->regs[1]  = (reg_t)entry_point;   /* ra：main 万一返回也有个去处 */
    /* 命令行参数：RISC-V 调用约定里 main(argc, argv) 就是 a0/a1。
     * crt0 清 BSS 只用 t0/t1，不会碰它们，call main 时原样传给 main。 */
    pt_regs->regs[10] = (reg_t)argc;          /* a0 = argc */
    pt_regs->regs[11] = (reg_t)argv_va;       /* a1 = argv */
    pt_regs->sepc     = (reg_t)entry_point;
    pt_regs->sstatus  = SR_SPIE;              /* SPP=0 回 User 态；SPIE=1 开中断 */

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));

    /* P3 任务3：新任务首次上场不走 switch_to 的返回点（那里有"重拿大锁"），
     *   所以伪造的 ra 指向 new_task_entry：先补 lock 再走公共返回路径。 */
    pt_switchto->regs[0] = (reg_t)new_task_entry;       /* ra */
    pt_switchto->regs[1] = (reg_t)pt_regs;              /* sp → 陷阱帧 */
    for (int i = 2; i < 14; ++i)
        pt_switchto->regs[i] = 0;

    pcb->kernel_sp = (reg_t)pt_switchto;
    pcb->user_sp = (reg_t)user_stack;
}

/* =====================================================================
 * do_exec：按名字启动一个新进程
 *   成功返回新进程 pid（≥1），失败返回 0（题目约定）。
 * ===================================================================== */
#ifndef S_CORE
pid_t do_exec(char *name, int argc, char **argv, uint64_t mask)
{
    if (name == 0 || argc < 0 || argc > EXEC_MAX_ARGC)
        return 0;

    /* P3 评审补丁：显式掩码里若含本系统之外的核位（如 0x4），该任务在
     *   任何核上都选不中、滞留就绪队列静默饿死——直接拒绝启动。
     *   （mask==0 仍表示"继承父进程"，不是非法值。） */
    if (mask != 0 && (mask & ~CPU_MASK_ALL) != 0)
        return 0;

    /* ① 名称从用户空间拷进内核（长度封顶 TASK_NAME_LEN-1） */
    char kname[TASK_NAME_LEN];
    int i = 0;
    while (i < TASK_NAME_LEN - 1 && name[i] != '\0') {
        kname[i] = name[i];
        ++i;
    }
    kname[i] = '\0';
    if (i == 0)
        return 0;

    /* ② 按名加载代码（同一个程序加载多次＝共享同一份代码窗口，
     *   各进程有自己的内核栈/用户栈——任务4 的测试正依赖这一点）。
     *   任务5：细粒度版用 loader_lock 包住加载（staging 缓冲与地址表
     *   全局共享，两核同时 exec 会踩）；加载完就放锁，与 sched_lock
     *   不嵌套——两把锁分属不同域，无需遵守锁序。 */
    FINE_LOCK(&loader_lock);
    uint64_t entry = load_task_img(kname);
    FINE_UNLOCK(&loader_lock);
    if (entry == 0)
        return 0;

    /* ③ PCB 与两张栈（从这里到入队是一个 sched_lock 事务：
     *   pcb 池与栈池的空闲链表都归 sched_lock 保护，见 smp.h 纪律） */
    FINE_LOCK(&sched_lock);

    pcb_t *task = alloc_pcb();
    if (task == 0)
        goto fail;

    ptr_t kstack_base = task_stack_alloc(1);
    ptr_t ustack_base = task_stack_alloc(0);
    ptr_t kstack_top = kstack_base + PAGE_SIZE;
    ptr_t ustack_top = ustack_base + PAGE_SIZE;

    /* ④ 参数打包进新进程的用户栈。
     *   注意 sp 不由 ustack_top 直接充当：pack_args 会把初始 sp 落在
     *   参数区【之下】（见其注释），main 的栈帧向下生长才不会踩参数。 */
    uint64_t argv_va = 0;
    ptr_t user_sp_init = ustack_top;
    if (argc > 0 && argv != 0) {
        if (!pack_args(ustack_top, argc, argv, &argv_va, &user_sp_init))
            goto fail_free;
    }

    /* ⑤ 填 PCB */
    task->pid = process_id++;
    strcpy(task->name, kname);
    task->kernel_stack_base = kstack_base;
    task->user_stack_base = ustack_base;
    task->status = TASK_READY;
    /* P3 任务4：核亲和掩码。taskset 启动时显式给（mask != 0）；
     *   否则继承父进程——affinity 的 5 个 test_affinity 子任务
     *   正是靠继承拿到 taskset 指定的掩码。 */
    task->cpu_mask = (mask != 0) ? mask : current_running->cpu_mask;
    task->cursor_x = 0;
    task->cursor_y = 0;
    task->wakeup_time = 0;
    task->wl_pos = 0;
    task->wl_cols = 0;
    task->wl_run = 0;
    task->has_wl = 0;
    /* 任务5：PCB 复用时必须清掉上一世的唤醒挂起标记——否则新进程
     *   一起步就带着 wake_pending=1，而它的 do_scheduler 要等被调度到
     *   才清；可 pick_next 又因这个标记永不选它 → 自锁死（板上
     *   consumer_rate 卡死的候选根因之一）。 */
    task->wake_pending = 0;
    list_init(&task->wait_list);

    /* ⑥ 伪造现场并入就绪队列（初始 sp = 参数区之下的对齐位置） */
    init_pcb_stack(kstack_top, user_sp_init, (ptr_t)entry, task, argc,
                   (ptr_t)argv_va);
    list_add_tail(&task->list, &ready_queue);

    FINE_UNLOCK(&sched_lock);

    return task->pid;

fail_free:
    task_stack_free(kstack_base, 1);
    task_stack_free(ustack_base, 0);
fail:
    FINE_UNLOCK(&sched_lock);
    return 0;
}
#endif /* !S_CORE */

/* =====================================================================
 * do_exit：结束当前进程
 *   进程执行完 crt0 里的 sys_exit（或显式调）后走这里：标记 EXITED、
 *   唤醒所有 waitpid 本进程者，然后让出 CPU；两张栈留到 PCB 被真正
 *   复用（alloc_pcb）时才归还。
 *   EXITED 的 PCB 不进任何队列，do_scheduler 不会再选它；PCB 槽位与
 *   栈空间由后续 exec 复用（见 alloc_pcb / task_stack_alloc）。
 * ===================================================================== */
void do_exit(void)
{
    pcb_t *task = current_running;

    /* 任务5：状态迁移/唤醒等待者在一个 sched_lock 临界段里完成；
     *   放锁之后才让出。 */
    FINE_LOCK(&sched_lock);

    task->status = TASK_EXITED;

    while (!list_empty(&task->wait_list))
        do_unblock(task->wait_list.next);

    /* ★ 两张栈不在这里归还（以前在这里还，是重磅 bug）：本任务此刻还
     *   踩着自己的内核栈执行——下面还有调度决策、M 态定时器和 switch_to。
     *   把栈放进空闲池，别的核一次 exec 就可能把这块内存装成新任务，
     *   两边现场当场互相踩烂（实测症状：活任务的 kernel_sp 指向被
     *   call_bios 代码覆写的内存）。改在 alloc_pcb 真正复用时归还。 */

    FINE_UNLOCK(&sched_lock);

    do_scheduler();     /* 切走；此后本 PCB 不会被再选中 */
    while (1)           /* 不可达 */
        ;
}

/* =====================================================================
 * do_kill：杀死指定 pid 的进程
 *   成功返回 1，找不到返回 0（题目约定）。
 *   被 kill 者可能正处于任何状态，要把它从"所有还咬着它的资源"里摘干净：
 *     · 它在某把锁的等待队列里等着 → 摘掉（不然唤醒者会唤醒一具尸体）
 *     · 它自己正持有某把锁         → 替它释放并唤醒该锁的等待者
 *                                    （design review 明确要求处理的情形）
 *     · 它在就绪/睡眠/任意 block 队列上 → 摘掉
 *       （侵入式链表删结点不需要知道队列头，直接 list_del 即可）
 *     · 有人在 waitpid 它           → 醒；它们应自行按 pid 判断结果
 * ===================================================================== */
int do_kill(pid_t pid)
{
    int ret = 0;
    pcb_t *task;

    /* 任务5：mlocks 的 owner 属 sync 域、队列/状态属 sched 域——按锁序
     *   sync→sched 双锁齐拿，把"释放锁 + 摘队 + 置终态 + 唤醒"做成一个
     *   跨核原子事务；do_scheduler 挪到放锁之后（自己杀自己时用）。 */
    FINE_LOCK(&sync_lock);
    FINE_LOCK(&sched_lock);

    task = find_pcb_by_pid(pid);

    if (task == 0 || is_idle_pcb(task) || task->status == TASK_EXITED)
        goto out;

    /* ① 替它释放持有的互斥锁 */
    for (int i = 0; i < LOCK_NUM; ++i) {
        if (mlocks[i].owner == task) {
            mlocks[i].owner = 0;
            spin_lock_release(&mlocks[i].lock);
            if (!list_empty(&mlocks[i].block_queue))
                do_unblock(mlocks[i].block_queue.next);
        }
    }

    /* ② 从当前所在队列里摘除 */
    if (task->status == TASK_READY || task->status == TASK_BLOCKED)
        list_del(&task->list);

    /* ③ 终态 + 唤醒所有等待者 */
    task->status = TASK_EXITED;
    while (!list_empty(&task->wait_list))
        do_unblock(task->wait_list.next);

    /* ④ 归还栈：只有它已完整下核才能动。还在核上的（正在跑、切换中、
     *   或处在 block 与 do_scheduler 之间的几步里）留给 alloc_pcb 复用时
     *   归还——那里同样以 task_on_cpu 为闸。 */
    if (!task_on_cpu(task)) {
        task_stack_free(task->kernel_stack_base, 1);
        task_stack_free(task->user_stack_base, 0);
        task->kernel_stack_base = 0;
        task->user_stack_base = 0;
    }

    ret = 1;

out:
    FINE_UNLOCK(&sched_lock);
    FINE_UNLOCK(&sync_lock);

    /* ⑤ 杀到自己头上：放锁后也走一次让出 */
    if (ret && task == current_running)
        do_scheduler();

    return ret;
}

/* =====================================================================
 * do_waitpid：等待指定 pid 的进程退出
 *   返回被等待进程的 pid；不存在该 pid 返回 0。
 *   被等待者已经退出则立即返回（PCB 在复用前一直可查）。
 * ===================================================================== */
int do_waitpid(pid_t pid)
{
    /* 任务5：细粒度版把"判子进程状态"和"挂到它的 wait_list"放进同一个
     *   sched_lock 临界段——否则子进程在核 1 上退出（改状态+唤醒等待者）
     *   可能插在我们的两次操作之间，我们随后照睡不误，永久卡死。
     *   大锁版宏为空（BKL 本来就在 trap 入口持有，天然互斥），等价原逻辑。 */
    FINE_LOCK(&sched_lock);

    pcb_t *task = find_pcb_by_pid(pid);

    /* P3 评审补丁：pid==0 会解析到本核 idle、pid==自己 会解析到自身——
     *   两者的 wait_list 都永远等不到"退出唤醒"，直接按"无可等待对象"
     *   返回 0（与 pid 不存在的语义一致），避免调用者永睡。 */
    if (task == 0 || is_idle_pcb(task) || task == current_running) {
        FINE_UNLOCK(&sched_lock);
        return 0;
    }

    if (task->status == TASK_EXITED) {
        FINE_UNLOCK(&sched_lock);
        return pid;
    }

    /* 挂到对方的 wait_list 上睡觉；对方 exit/kill 时会被唤醒 */
    current_running->status = TASK_BLOCKED;
    list_add_tail(&current_running->list, &task->wait_list);

    FINE_UNLOCK(&sched_lock);

    do_scheduler();

    return pid;
}

/* =====================================================================
 * do_process_show：ps 命令的内核实现
 *   列出系统里所有进程（含 pid0 内核空转与 shell）。打印走 printk，
 *   光标在调用者（shell）的显示区，所以内容自然落在 shell 区。
 *   TODO(任务3/4)：补充 cpu_mask 与"正在哪个核上运行"两列。
 * ===================================================================== */
static const char *task_status_str(task_status_t st)
{
    switch (st) {
    case TASK_BLOCKED: return "BLOCKED";
    case TASK_RUNNING: return "RUNNING";
    case TASK_READY:   return "READY";
    case TASK_EXITED:  return "EXITED";
    default:           return "?";
    }
}

/* 临时诊断：任务当前是否真的挂在就绪队列上（ps 里 q: 字段）。
 *   调用者已持 sched_lock。READY 却 q:0 = 结点已从队列里丢失。 */
static int in_ready_queue(pcb_t *t)
{
    for (list_node_t *n = ready_queue.next; n != &ready_queue; n = n->next) {
        if (list_entry(n, pcb_t, list) == t)
            return 1;
    }
    return 0;
}

void do_process_show(void)
{
    /* 输出格式对齐官方参考图（任务书图 P3-8）：
     *   [Process Table]:
     *   [i] PID : n  STATUS : RUNNING mask: 0x3 Running on core 1
     * 细节：
     *   · [i] 从 0 起的行号；不列 idle（官图没有）；EXITED 的进程如实列出；
     *   · STATUS 域定宽 7（RUNNING/BLOCKED 满宽，READY 补 3 空格——与官图
     *     "READY   mask" 的对齐一致）；
     *   · mask 小写十六进制（0x3 / 0x1）；
     *   · "Running on core N" 只对 RUNNING 打，N 从 core_running[] 反查
     *     （RUNNING 的任务必然只被它所在核登记）。 */
    /* 任务5：整表在 sched_lock 下读（两核可能同时在 exec/exit 改 pcb）；
     *   期间会 printk（嵌套拿 console_lock，锁序 sched→console 合法）。 */
    FINE_LOCK(&sched_lock);

    printk("[Process Table]:\n");

    int row = 0;
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].name[0] == '\0')
            continue;

        int core = -1;
        if (pcb[i].status == TASK_RUNNING) {
            for (int c = 0; c < NR_CPUS; ++c) {
                if (core_running[c] == &pcb[i]) {
                    core = c;
                    break;
                }
            }
        }

        /* 状态列定宽 7（READY 右补 3 空格与官图对齐）——内核 printk
         *   只认 %s/%d/%x 等基本转换、没有 %-7s 宽度修饰，手工补空格。 */
        char st[8];
        const char *ss = task_status_str(pcb[i].status);
        int n = 0;
        while (ss[n] != '\0' && n < 7) {
            st[n] = ss[n];
            ++n;
        }
        while (n < 7)
            st[n++] = ' ';
        st[n] = '\0';

        printk("[%d] PID : %d  STATUS : %s mask: 0x%lx",
               row, pcb[i].pid, st, pcb[i].cpu_mask);
        if (core >= 0)
            printk(" Running on core %d", core);
        /* 临时诊断字段：wp = wake_pending；q = 是否真在就绪队列上（非 READY 打 -1） */
        printk(" wp:%d q:%d", pcb[i].wake_pending,
               pcb[i].status == TASK_READY ? in_ready_queue(&pcb[i]) : -1);
        printk("\n");

        ++row;
    }

    /* 临时诊断汇总：两核调度心跳 + 唤醒标记的设置/自愈计数 */
    printk("[diag] beat0=%ld beat1=%ld wp_set=%ld wp_heal=%ld(pid=%d cpu=%d)\n",
           (long)diag_beat[0], (long)diag_beat[1],
           (long)diag_wp_set, (long)diag_wp_heal,
           diag_wp_heal_pid, diag_wp_heal_cpu);
    printk("[diag] core0=%d core1=%d\n",
           core_running[0] ? core_running[0]->pid : -1,
           core_running[1] ? core_running[1]->pid : -1);

    FINE_UNLOCK(&sched_lock);
}

pid_t do_getpid(void)
{
    return current_running->pid;
}

/* =====================================================================
 * 任务4：taskset -p <mask> <pid> 的内核实现
 *   改已存在进程的亲和掩码。改完不需要立刻搬人：若目标正跑在
 *   "不合掩码"的核上，当前时间片照跑；下次调度它被放回【全局】就绪
 *   队列后，只有掩码允许的核会从队列里挑走它——跨核迁移自然发生。
 *   拒绝 mask==0：没有核能跑的进程等于永久挂死（也不接受 0x0 输入）。
 * ===================================================================== */
int do_taskset_p(pid_t pid, uint64_t mask)
{
    /* P3 评审补丁：除 0 之外，高位掩码（没有核能跑）同样拒绝——
     *   否则任务滞留就绪队列、静默饿死。 */
    if (mask == 0 || (mask & ~CPU_MASK_ALL) != 0)
        return 0;

    int ret = 0;

    FINE_LOCK(&sched_lock);     /* cpu_mask 被别的核的 pick_next 读 */

    pcb_t *task = find_pcb_by_pid(pid);
    if (task != 0 && !is_idle_pcb(task)) {
        task->cpu_mask = mask;
        ret = 1;
    }                           /* 找不到进程，或想改 idle（不许） */

    FINE_UNLOCK(&sched_lock);
    return ret;
}

/* shell 轮询串口读一个字符；无输入时 BIOS 返回 -1 */
int do_getchar(void)
{
    /* 任务5：UART 读写同属 console 域（与屏幕缓冲一起，防 fly 刷屏
     *   期间把键盘轮询与光标操作交错）。大锁版宏为空。 */
    int c;

    FINE_LOCK(&console_lock);
    c = bios_getchar();
    FINE_UNLOCK(&console_lock);

    return c;
}
