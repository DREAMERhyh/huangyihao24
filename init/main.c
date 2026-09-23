#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/time.h>
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

extern void ret_from_exception();

// bootblock 经 head.S 传来的引导参数（任务5 起为【字节】语义）：
//   a0 = AppInfo 起始字节偏移，a1 = 用户程序数目，a2 = apps 区起始字节偏移
//   定义见 task.h；赋值在 head.S 清完 BSS 后完成
uint64_t boot_appinfo_off;
uint64_t boot_appnum;
uint64_t boot_apps_base_off;

// Task info array
task_info_t tasks[TASK_MAXNUM];

// 任务3：镜像里记录的用户程序数目（createimage 写在镜像偏移 0x1f8）
int task_num = 0;

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;
    jmptab[SCREEN_REFLUSH]  = (long (*)())screen_reflush;

    // TODO: [p2-task1] (S-core) initialize system call table.

}

static void init_task_info(void)
{
    // 任务5：从 AppInfo 区读出 task_info_t 数组，填进 tasks[]。
    //   位置与数量来自 bootblock 传参（head.S 已存进 boot_appinfo_off /
    //   boot_appnum），不再直接读 0x50200xxx。
    //
    //   ★ 与任务4 的关键差别：AppInfo 在镜像里**不再按扇区对齐**（字节级紧密
    //   排列），而块设备只能整扇区读、没有偏移参数。所以必须：
    //     ① 算出 AppInfo 落在哪个扇区、扇区内偏移多少；
    //     ② 把"覆盖它的若干个整扇区"读进中转缓冲；
    //     ③ 从缓冲的 offset 处起，拷 APPINFO_BYTES 字节进 tasks[]。
    //   中转缓冲需 2 个扇区：offset ≤ 511，APPINFO_BYTES = 448，
    //   故 offset + 448 ≤ 959 < 1024。
    static uint8_t appinfo_buf[2 * SECTOR_SIZE]
        __attribute__((aligned(SECTOR_SIZE)));

    task_num = (int)boot_appnum;

    if (task_num <= 0 || task_num > TASK_MAXNUM) {
        task_num = 0;
        return;
    }

    uint32_t start  = (uint32_t)boot_appinfo_off;
    uint32_t sector = start / SECTOR_SIZE;              // 从哪块开始读
    uint32_t offset = start % SECTOR_SIZE;              // 块内偏移多少
    uint32_t need   = NBYTES2SEC(offset + APPINFO_BYTES);   // 要读几块

    if (need > sizeof(appinfo_buf) / SECTOR_SIZE) {     // 防御（不会发生）
        task_num = 0;
        return;
    }

    if (bios_sd_read((unsigned)(unsigned long)appinfo_buf, need,
                     sector) != 0) {
        task_num = 0;
        return;
    }

    // 只拷"有内容的那几行"（task_num × 28 字节），从缓冲的 offset 处起
    memcpy((void *)tasks, (const void *)(appinfo_buf + offset),
           (size_t)task_num * sizeof(task_info_t));
}

// 任务5：十进制无符号打印（内核无 itoa，除 10 取余、倒序输出）
static void put_uint(unsigned int val)
{
    char tmp[12];
    int i = 0;

    if (val == 0) {
        bios_putchar('0');
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + val % 10);
        val /= 10;
    }
    while (i > 0)
        bios_putchar(tmp[--i]);
}

// 任务5：十六进制打印（地址用 16 进制更直观）
static void put_hex(unsigned int val)
{
    static const char hex[] = "0123456789abcdef";
    char tmp[9];
    int i = 0;

    bios_putstr("0x");
    if (val == 0) {
        bios_putchar('0');
        return;
    }
    while (val > 0) {
        tmp[i++] = hex[val & 0xf];
        val >>= 4;
    }
    while (i > 0)
        bios_putchar(tmp[--i]);
}

// 任务5：显示应用信息（slide 25 可选项；name / start(相对字节) / size / entry）
static void show_task_list(void)
{
    bios_putstr("name              offset size   entry\r\n");
    for (int i = 0; i < task_num; ++i)
    {
        int pad = TASK_NAME_LEN - strlen(tasks[i].name);

        bios_putstr(tasks[i].name);
        while (pad-- > 0)
            bios_putchar(' ');
        put_uint(tasks[i].start_byte);
        bios_putstr("    ");
        put_uint(tasks[i].byte_size);
        bios_putstr("    ");
        put_hex(tasks[i].vaddr_entry);
        bios_putstr("\r\n");
    }
}

// 任务4：读入一行由 \r 或 \n 结束的程序名。
//   支持退格（BS=0x08 / DEL=0x7f）；名字截断到 TASK_NAME_LEN-1；
//   若某行超长则"整个名字"判非法，清空到行尾后返回 0。
//   \r 与 \n 都立即终止；后续的 \r 或 \n（如 \r\n 的另一半）会以空行被
//   瞬间消费掉，由调用方忽略空行，不会卡住。
static int readline(char *buf, int cap)
{
    int len = 0;

    buf[0] = '\0';
    while (1)
    {
        int ch = bios_getchar();

        if (ch == -1)
            continue;   // 无输入跳过

        if (ch == '\n' || ch == '\r') {   // 双终止符
            buf[len] = '\0';
            return len;
        }

        if (ch == '\b' || ch == 0x7f) {   // 退格：BS 或 DEL
            if (len > 0) {
                len--;
                bios_putchar('\b');        // 回显擦除
                bios_putchar(' ');
                bios_putchar('\b');
            }
            continue;
        }

        if (ch < ' ' || ch > '~')         // 不可打印字符忽略
            continue;

        if (len >= cap - 1) {              // 名字超长：本行作废
            while (1) {
                while ((ch = bios_getchar()) == -1)
                    ;
                if (ch == '\n' || ch == '\r')
                    break;
            }
            buf[0] = '\0';
            return 0;
        }

        bios_putchar(ch);                  // 回显
        buf[len++] = (char)ch;
    }
}

// P2：P1 的"按名加载单个程序并跳转过去"变成了"按名挑出一组要并发运行的程序"。
//   被选中的程序由 init_pcb 逐个装载成 PCB，之后交给 do_scheduler 轮流执行，
//   所以这里只收集名单，不再跳转。交互格式沿用 P1：一行一个名字，
//   空行表示"选完了，开始调度"，list 打印应用信息表。
static task_info_t *running_tasks[TASK_MAXNUM];
static int running_task_num = 0;

static task_info_t *lookup_task(const char *name)
{
    for (int i = 0; i < task_num; ++i) {
        if (strcmp(tasks[i].name, name) == 0)
            return &tasks[i];
    }
    return 0;
}

static void select_tasks(void)
{
    bios_putstr("Available tasks: ");
    for (int i = 0; i < task_num; ++i) {
        bios_putstr(tasks[i].name);
        if (i + 1 < task_num)
            bios_putstr(", ");
    }
    bios_putstr("\n\r");

    while (1)
    {
        char name[TASK_NAME_LEN];

        bios_putstr("Please input a task name ('list' for info, empty line to start): ");

        if (readline(name, TASK_NAME_LEN) <= 0) {   // 空行/非法行
            if (running_task_num > 0)
                return;                             // 开始调度
            bios_putstr("Error: no task selected!\n\r");
            continue;
        }

        if (strcmp(name, "list") == 0) {            // 任务5：信息显示命令
            show_task_list();
            continue;
        }

        task_info_t *task = lookup_task(name);
        if (task == 0) {
            bios_putstr("Error: unknown task!\n\r");
            continue;
        }

        // 每个 app 只有一块固定的加载窗口，同一个程序不能选两次
        int dup = 0;
        for (int i = 0; i < running_task_num; ++i) {
            if (running_tasks[i] == task)
                dup = 1;
        }
        if (dup) {
            bios_putstr("Error: task already selected!\n\r");
            continue;
        }

        running_tasks[running_task_num++] = task;
    }
}

/************************************************************/
static void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
{
     /* TODO: [p2-task3] initialization of registers on kernel stack
      * HINT: sp, ra, sepc, sstatus
      * NOTE: To run the task in user mode, you should set corresponding bits
      *     of sstatus(SPP, SPIE, etc.).
      */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));

    /* 任务3：伪造一份"刚从用户态陷入、现场已存好"的陷阱帧。
     *   switch_to 末尾 jr ra 跳到 ret_from_exception 后，RESTORE_CONTEXT
     *   会照着它把寄存器铺回 CPU，最后 sret 进用户态。 */
    memset((void *)pt_regs, 0, sizeof(regs_context_t));

    pt_regs->regs[2]  = (reg_t)user_stack;    // sp：该任务自己的用户栈顶
    pt_regs->regs[4]  = (reg_t)pcb;           // tp == current_running
    pt_regs->regs[1]  = (reg_t)entry_point;   // ra：main 万一返回也有个去处
    pt_regs->sepc     = (reg_t)entry_point;   // sret 后从程序入口开始执行
    pt_regs->sstatus  = SR_SPIE;              // SPP=0 回 User 态；SPIE=1 让 sret 后开中断


    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     *
     * 任务1：新任务没有已保存的现场，所以要手工伪造一个"刚从 switch_to 里
     *   恢复"的 switchto_context_t，按 sched.h 里 regs[14] 的顺序摆：
     *     regs[0]=ra  —— switch_to 末尾的 jr ra 会跳到这里
     *     regs[1]=sp  —— 恢复现场时用的 sp
     *     regs[2..13]=s0~s11 —— 新任务没有需要恢复的 callee-saved 变量，清零
     *   任务3 起 ra 改成 ret_from_exception（先恢复陷阱帧、再 sret 进用户态），
     *   于是 sp 要指向上面那份陷阱帧的基址。 */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));

    pt_switchto->regs[0] = (reg_t)ret_from_exception;   /* ra */
    pt_switchto->regs[1] = (reg_t)pt_regs;              /* sp → 陷阱帧 */
    for (int i = 2; i < 14; ++i)
        pt_switchto->regs[i] = 0;                       /* s0 ~ s11 */

    /* PCB 记住"下次被调度时从哪恢复"以及任务自己的栈顶 */
    pcb->kernel_sp = (reg_t)pt_switchto;
    pcb->user_sp = (reg_t)user_stack;
}

static void init_pcb(void)
{
    // 本次要跑哪些程序由这里的交互决定（名单见 running_tasks / running_task_num）
    select_tasks();

    for (int i = 0; i < running_task_num; ++i)
    {
        pcb_t *task = &pcb[i];

        // TODO: [p2-task1] load needed tasks and init their corresponding PCB
        //   内核栈与用户栈各分配一块 4K 页（地址段见任务书表 P2-1）：
        //   内核栈给"内核态现场"用，用户栈给程序自己的 C 栈用，二者分开
        ptr_t kernel_stack = allocKernelPage(1) + PAGE_SIZE;
        ptr_t user_stack   = allocUserPage(1) + PAGE_SIZE;

        // 按名字把 app 从镜像搬进它的加载窗口，返回值就是入口地址
        uint64_t entry = load_task_img(running_tasks[i]->name);
        assert(entry != 0);

        task->pid = process_id++;
        task->time_slice = TIMER_INTERVAL;   // 任务5 估出成本前先用默认时间片
        task->status = TASK_READY;
        task->cursor_x = 0;
        task->cursor_y = 0;
        task->wakeup_time = 0;

        init_pcb_stack(kernel_stack, user_stack, (ptr_t)entry, task);

        // TODO: [p2-task1] remember to initialize 'current_running'
        //   新任务一建好就排进就绪队列，等 do_scheduler 挑它上场
        list_add_tail(&task->list, &ready_queue);
    }

    // 内核自己也得有个 PCB：调度、屏幕光标等都是经由 current_running 访问的。
    //   pid0 是内核的空转任务，让它始终处于 RUNNING，于是它也会进就绪队列，
    //   在所有用户任务都被阻塞时兜底（任务3 的睡眠唤醒就靠它一直转着）。
    current_running = &pid0_pcb;
    pid0_pcb.status = TASK_RUNNING;
}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    // 把系统调用号映射到内核函数上（用户侧 invoke_syscall 用 a7 传号）
    syscall[SYSCALL_SLEEP]        = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD]        = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE]        = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR]       = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH]      = (long (*)())screen_reflush;
    syscall[SYSCALL_GET_TIMEBASE] = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK]     = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT]    = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ]     = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_SET_WORKLOAD] = (long (*)())do_set_sche_workload;
}
/************************************************************/

int main(void)
{
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information from bootblock-passed AppInfo (〃'▽'〃)
    init_task_info();

    // Init Process Control Blocks |•'-'•) ✧
    init_pcb();
    printk("> [INIT] PCB initialization succeeded.\n");

    // Read CPU frequency (｡•ᴗ-)_
    time_base = bios_read_fdt(TIMEBASE);

    // Init lock mechanism o(´^｀)o
    init_locks();
    printk("> [INIT] Lock mechanism initialization succeeded.\n");

    // Init interrupt (^_^)
    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");

    // Init system call table (0_0)
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");

    // Init screen (QAQ)
    init_screen();
    printk("> [INIT] SCREEN initialization succeeded.\n");

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's

    // ① 先给时钟装上第一次触发时间；之后每来一次中断，handle_irq_timer
    //    都会把下一次推后 TIMER_INTERVAL，所以只需要在这里武装一次。
    bios_set_timer(get_ticks() + TIMER_INTERVAL);

    // ② 内核自己也要能被中断打断（抢占模式下的"第一次调度"正是由这次时钟
    //    中断发起的：内核空转时被时钟打断 → handle_irq_timer → do_scheduler
    //    → 第一个任务被调度上场）。但陷阱入口要靠 sscratch 找内核栈顶，所以
    //    先给它一页专用的陷阱栈——直接借用 main 现在用的内核栈会和 main 的
    //    栈帧重叠。
    asm volatile("csrw sscratch, %0" : : "r"(allocKernelPage(1) + PAGE_SIZE));

    // ② 再打开 sie 的 STIE 位（允许时钟中断）。此时 sstatus.SIE 已经是 1
    //    （内核态由 setup_exception 打开，用户态由 sret 按陷阱帧的 SPIE 恢复），
    //    所以中断立刻就会来——抢占模式下的"第一次调度"就由这次时钟中断发起。
    enable_preempt();

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // 抢占模式下不再需要主动让出 CPU：时钟中断自己会触发 do_scheduler。
        // 这里是内核（pid0）的空转：wfi 睡到下一次中断，醒来后继续参与调度。
        // 注意不能用 do_scheduler() 空转——那会让 pid0 白占一个时间片。
        asm volatile("wfi");
    }

    return 0;
}
