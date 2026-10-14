#include <syscall.h>
#include <stdint.h>
#include <kernel.h>
#include <unistd.h>

static const long IGNORE = 0L;

/* 任务3：真系统调用的发起方式。
 *   RISC-V 的约定：调用号放 a7，参数依次放 a0~a4，然后 ecall 陷入内核；
 *   内核把返回值写回 a0。这里用 GCC 的"寄存器变量"把 C 变量钉在指定寄存器上，
 *   ecall 之后再从 a0 读回结果。a0 是输出，所以用 "+r"。
 *   注意 a0~a7/t0~t6 都是 caller-saved，调用者本来就不指望它们保值；
 *   而 a0 被声明成输出，编译器不会假设它没变。 */
static long invoke_syscall(long sysno, long arg0, long arg1, long arg2,
                           long arg3, long arg4)
{
    /* TODO: [p2-task3] implement invoke_syscall via inline assembly */
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a3 asm("a3") = arg3;
    register long a4 asm("a4") = arg4;
    register long a7 asm("a7") = sysno;

    asm volatile("ecall"
                 : "+r"(a0)
                 : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7)
                 : "memory");

    return a0;
}

/* 任务1/2 用的跳转表版本已随任务3 退场：现在用户程序跑在 User 态，
 * 与内核不再同特权级，只能通过 ecall 陷入。 */

void sys_yield(void)
{
    invoke_syscall(SYSCALL_YIELD, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor(int x, int y)
{
    invoke_syscall(SYSCALL_CURSOR, (long)x, (long)y, IGNORE, IGNORE, IGNORE);
}

void sys_write(char *buff)
{
    invoke_syscall(SYSCALL_WRITE, (long)buff, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_reflush(void)
{
    invoke_syscall(SYSCALL_REFLUSH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mutex_init(int key)
{
    return (int)invoke_syscall(SYSCALL_LOCK_INIT, (long)key, IGNORE, IGNORE,
                               IGNORE, IGNORE);
}

void sys_mutex_acquire(int mutex_idx)
{
    invoke_syscall(SYSCALL_LOCK_ACQ, (long)mutex_idx, IGNORE, IGNORE, IGNORE,
                   IGNORE);
}

void sys_mutex_release(int mutex_idx)
{
    invoke_syscall(SYSCALL_LOCK_RELEASE, (long)mutex_idx, IGNORE, IGNORE,
                   IGNORE, IGNORE);
}

long sys_get_timebase(void)
{
    return invoke_syscall(SYSCALL_GET_TIMEBASE, IGNORE, IGNORE, IGNORE, IGNORE,
                          IGNORE);
}

long sys_get_tick(void)
{
    return invoke_syscall(SYSCALL_GET_TICK, IGNORE, IGNORE, IGNORE, IGNORE,
                          IGNORE);
}

void sys_sleep(uint32_t time)
{
    invoke_syscall(SYSCALL_SLEEP, (long)time, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_set_sche_workload(int workload)
{
    /* TODO: [p2-task5] call invoke_syscall to implement sys_set_sche_workload */
    invoke_syscall(SYSCALL_SET_WORKLOAD, (long)workload, IGNORE, IGNORE, IGNORE,
                   IGNORE);
}

/************************************************************/
/* P3 任务1：进程管理与终端。全部走 invoke_syscall（a7 传号、a0~a4 传参） */
#ifdef S_CORE
pid_t  sys_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    return (pid_t)invoke_syscall(SYSCALL_EXEC, (long)id, (long)argc, (long)arg0,
                                 (long)arg1, (long)arg2);
}
#else
pid_t  sys_exec(char *name, int argc, char **argv)
{
    return (pid_t)invoke_syscall(SYSCALL_EXEC, (long)name, (long)argc,
                                 (long)argv, IGNORE, IGNORE);
}

/* 任务4：以指定核亲和掩码启动（taskset <mask> <name> 走这条；
 *   普通 exec 的封装把第 4 参传 0 = 内核侧继承父进程掩码） */
pid_t  sys_exec_mask(char *name, int argc, char **argv, uint64_t mask)
{
    return (pid_t)invoke_syscall(SYSCALL_EXEC, (long)name, (long)argc,
                                 (long)argv, (long)mask, IGNORE);
}

/* 任务4：修改已存在进程的核亲和掩码（taskset -p <mask> <pid>） */
int  sys_set_affinity(pid_t pid, uint64_t mask)
{
    return (int)invoke_syscall(SYSCALL_TASKSET, (long)pid, (long)mask,
                               IGNORE, IGNORE, IGNORE);
}
#endif

void sys_exit(void)
{
    invoke_syscall(SYSCALL_EXIT, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
    /* 正常情况下内核已让出 CPU 不会回到这里；万一返回了，死循环兜底，
     * 不能继续往下跑（现场的栈/PCB 都已经被回收了） */
    while (1)
        ;
}

int  sys_kill(pid_t pid)
{
    return (int)invoke_syscall(SYSCALL_KILL, (long)pid, IGNORE, IGNORE, IGNORE,
                               IGNORE);
}

int  sys_waitpid(pid_t pid)
{
    return (int)invoke_syscall(SYSCALL_WAITPID, (long)pid, IGNORE, IGNORE,
                               IGNORE, IGNORE);
}


void sys_ps(void)
{
    invoke_syscall(SYSCALL_PS, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

pid_t sys_getpid()
{
    return (pid_t)invoke_syscall(SYSCALL_GETPID, IGNORE, IGNORE, IGNORE, IGNORE,
                                 IGNORE);
}

int  sys_getchar(void)
{
    return (int)invoke_syscall(SYSCALL_READCH, IGNORE, IGNORE, IGNORE, IGNORE,
                               IGNORE);
}

/* P3 任务1：clear 命令——清屏幕缓冲（内核 screen_clear） */
void sys_clear(void)
{
    invoke_syscall(SYSCALL_CLEAR, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_barrier_init(int key, int goal)
{
    /* 任务2：屏障。返回值 = handle（内核里的槽位下标），失败 -1 */
    return (int)invoke_syscall(SYSCALL_BARR_INIT, (long)key, (long)goal,
                               IGNORE, IGNORE, IGNORE);
}

void sys_barrier_wait(int bar_idx)
{
    invoke_syscall(SYSCALL_BARR_WAIT, (long)bar_idx, IGNORE, IGNORE, IGNORE,
                   IGNORE);
}

void sys_barrier_destroy(int bar_idx)
{
    invoke_syscall(SYSCALL_BARR_DESTROY, (long)bar_idx, IGNORE, IGNORE, IGNORE,
                   IGNORE);
}

int sys_condition_init(int key)
{
    /* 任务2：条件变量。返回值 = handle */
    return (int)invoke_syscall(SYSCALL_COND_INIT, (long)key, IGNORE, IGNORE,
                               IGNORE, IGNORE);
}

void sys_condition_wait(int cond_idx, int mutex_idx)
{
    /* 内核里会：放开互斥锁 → 睡在条件上 → 被唤醒后重新拿锁，才返回 */
    invoke_syscall(SYSCALL_COND_WAIT, (long)cond_idx, (long)mutex_idx, IGNORE,
                   IGNORE, IGNORE);
}

void sys_condition_signal(int cond_idx)
{
    invoke_syscall(SYSCALL_COND_SIGNAL, (long)cond_idx, IGNORE, IGNORE, IGNORE,
                   IGNORE);
}

void sys_condition_broadcast(int cond_idx)
{
    invoke_syscall(SYSCALL_COND_BROADCAST, (long)cond_idx, IGNORE, IGNORE,
                   IGNORE, IGNORE);
}

void sys_condition_destroy(int cond_idx)
{
    invoke_syscall(SYSCALL_COND_DESTROY, (long)cond_idx, IGNORE, IGNORE,
                   IGNORE, IGNORE);
}

int sys_semaphore_init(int key, int init)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_init */
}

void sys_semaphore_up(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_up */
}

void sys_semaphore_down(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_down */
}

void sys_semaphore_destroy(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_destroy */
}

int sys_mbox_open(char * name)
{
    /* 任务2：按名字开信箱（同名共享，引用计数在内核维护）。返回 handle */
    return (int)invoke_syscall(SYSCALL_MBOX_OPEN, (long)name, IGNORE, IGNORE,
                               IGNORE, IGNORE);
}

void sys_mbox_close(int mbox_id)
{
    invoke_syscall(SYSCALL_MBOX_CLOSE, (long)mbox_id, IGNORE, IGNORE, IGNORE,
                   IGNORE);
}

int sys_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    /* 返回值 = 本次发送是否发生过阻塞（0/1），测试里累加成 blocked 计数 */
    return (int)invoke_syscall(SYSCALL_MBOX_SEND, (long)mbox_idx, (long)msg,
                               (long)msg_length, IGNORE, IGNORE);
}

int sys_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    /* 返回值 = 本次接收是否发生过阻塞（0/1） */
    return (int)invoke_syscall(SYSCALL_MBOX_RECV, (long)mbox_idx, (long)msg,
                               (long)msg_length, IGNORE, IGNORE);
}
/************************************************************/
