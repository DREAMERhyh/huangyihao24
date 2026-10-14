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
#ifdef S_CORE
pid_t  sys_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec for S_CORE */
}    
#else
pid_t  sys_exec(char *name, int argc, char **argv)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec */
}
#endif

void sys_exit(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exit */
}

int  sys_kill(pid_t pid)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_kill */
}

int  sys_waitpid(pid_t pid)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_waitpid */
}


void sys_ps(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_ps */
}

pid_t sys_getpid()
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_getpid */
}

int  sys_getchar(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_getchar */
}

int  sys_barrier_init(int key, int goal)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrier_init */
}

void sys_barrier_wait(int bar_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrie_wait */
}

void sys_barrier_destroy(int bar_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrie_destory */
}

int sys_condition_init(int key)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_init */
}

void sys_condition_wait(int cond_idx, int mutex_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_wait */
}

void sys_condition_signal(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_signal */
}

void sys_condition_broadcast(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_broadcast */
}

void sys_condition_destroy(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_destroy */
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
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_open */
}

void sys_mbox_close(int mbox_id)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_close */
}

int sys_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_send */
}

int sys_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_recv */
}
/************************************************************/
