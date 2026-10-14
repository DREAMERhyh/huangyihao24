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

/* 是否有任何核"正在使用"这个 PCB（被切出中的任务状态可能已是 EXITED，
 *   但它的现场还在核上被 switch_to 引用，不能回收复用） */
static int on_any_cpu(pcb_t *t)
{
    for (int c = 0; c < NR_CPUS; ++c) {
        if (core_running[c] == t)
            return 1;
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
     *   给 waitpid 留出观察窗口。多核下用 core_running[] 排除全部核。 */
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].status == TASK_EXITED && list_empty(&pcb[i].wait_list)
            && !on_any_cpu(&pcb[i]))
            return &pcb[i];
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
pid_t do_exec(char *name, int argc, char **argv)
{
    if (name == 0 || argc < 0 || argc > EXEC_MAX_ARGC)
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
     *   各进程有自己的内核栈/用户栈——任务4 的测试正依赖这一点） */
    uint64_t entry = load_task_img(kname);
    if (entry == 0)
        return 0;

    /* ③ PCB 与两张栈 */
    pcb_t *task = alloc_pcb();
    if (task == 0)
        return 0;

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
        if (!pack_args(ustack_top, argc, argv, &argv_va, &user_sp_init)) {
            task_stack_free(kstack_base, 1);
            task_stack_free(ustack_base, 0);
            return 0;
        }
    }

    /* ⑤ 填 PCB */
    task->pid = process_id++;
    strcpy(task->name, kname);
    task->kernel_stack_base = kstack_base;
    task->user_stack_base = ustack_base;
    task->status = TASK_READY;
    task->cursor_x = 0;
    task->cursor_y = 0;
    task->wakeup_time = 0;
    task->wl_pos = 0;
    task->wl_cols = 0;
    task->wl_run = 0;
    task->has_wl = 0;
    list_init(&task->wait_list);

    /* ⑥ 伪造现场并入就绪队列（初始 sp = 参数区之下的对齐位置） */
    init_pcb_stack(kstack_top, user_sp_init, (ptr_t)entry, task, argc,
                   (ptr_t)argv_va);
    list_add_tail(&task->list, &ready_queue);

    return task->pid;
}
#endif /* !S_CORE */

/* =====================================================================
 * do_exit：结束当前进程
 *   进程执行完 crt0 里的 sys_exit（或显式调）后走这里：标记 EXITED、
 *   唤醒所有 waitpid 本进程者、归还内核栈/用户栈，然后让出 CPU。
 *   EXITED 的 PCB 不进任何队列，do_scheduler 不会再选它；PCB 槽位与
 *   栈空间由后续 exec 复用（见 alloc_pcb / task_stack_alloc）。
 * ===================================================================== */
void do_exit(void)
{
    pcb_t *task = current_running;

    task->status = TASK_EXITED;

    while (!list_empty(&task->wait_list))
        do_unblock(task->wait_list.next);

    task_stack_free(task->kernel_stack_base, 1);
    task_stack_free(task->user_stack_base, 0);

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
    pcb_t *task = find_pcb_by_pid(pid);

    if (task == 0 || is_idle_pcb(task) || task->status == TASK_EXITED)
        return 0;

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

    /* ④ 归还栈 */
    task_stack_free(task->kernel_stack_base, 1);
    task_stack_free(task->user_stack_base, 0);

    /* ⑤ 杀到自己头上：也走一次让出 */
    if (task == current_running)
        do_scheduler();

    return 1;
}

/* =====================================================================
 * do_waitpid：等待指定 pid 的进程退出
 *   返回被等待进程的 pid；不存在该 pid 返回 0。
 *   被等待者已经退出则立即返回（PCB 在复用前一直可查）。
 * ===================================================================== */
int do_waitpid(pid_t pid)
{
    pcb_t *task = find_pcb_by_pid(pid);

    if (task == 0)
        return 0;

    if (task->status == TASK_EXITED)
        return pid;

    /* 挂到对方的 wait_list 上睡觉；对方 exit/kill 时会被唤醒 */
    do_block(&current_running->list, &task->wait_list);

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

void do_process_show(void)
{
    printk("PID  NAME              STATUS\n");
    printk("%3d  %16s  %s\n", 0, "idle", task_status_str(pid0_pcb[0].status));
    printk("%3d  %16s  %s\n", 0, "idle", task_status_str(pid0_pcb[1].status));

    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].name[0] == '\0')
            continue;
        printk("%3d  %16s  %s\n", pcb[i].pid, pcb[i].name,
               task_status_str(pcb[i].status));
    }
}

pid_t do_getpid(void)
{
    return current_running->pid;
}

/* shell 轮询串口读一个字符；无输入时 BIOS 返回 -1 */
int do_getchar(void)
{
    return bios_getchar();
}
