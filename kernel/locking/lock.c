#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <os/irq.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];

/* 空闲槽位的标记：mlocks[i].key == KEY_UNUSED 表示这把锁还没被任何 key 占用。
 * 用户程序用到的 key 必须是别的值（测试用的是 42）。 */
#define KEY_UNUSED (-1)

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for (int i = 0; i < LOCK_NUM; ++i) {
        mlocks[i].key = KEY_UNUSED;
        spin_lock_init(&mlocks[i].lock);
        list_init(&mlocks[i].block_queue);
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    /* 一趟原子的"比较并交换"：只有原来就是 UNLOCKED 才能换成 LOCKED。
     * 返回非 0 表示抢到了。 */
    return atomic_cmpxchg(UNLOCKED, LOCKED, (ptr_t)&lock->status) == UNLOCKED;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    while (!spin_lock_try_acquire(lock))
        ;
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    atomic_swap(UNLOCKED, (ptr_t)&lock->status);
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    /* 题目要求：同一个 key 在整个生命周期内映射到同一个 handle。
     *   所以先找有没有 key 相同的锁，有就直接复用它的下标当 handle；
     *   没有才占一个空闲槽位。handle 就是 mlocks 的下标，范围 0..LOCK_NUM-1。 */
    for (int i = 0; i < LOCK_NUM; ++i) {
        if (mlocks[i].key == key)
            return i;
    }

    for (int i = 0; i < LOCK_NUM; ++i) {
        if (mlocks[i].key == KEY_UNUSED) {
            spin_lock_init(&mlocks[i].lock);
            list_init(&mlocks[i].block_queue);
            mlocks[i].key = key;
            return i;
        }
    }

    return -1;   // 锁用光了
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    mutex_lock_t *lock = &mlocks[mlock_idx];

    /* ★ 先入队、再抢锁：这样"入队"与"检查锁状态"之间没有空档，不会丢唤醒。
     *   如果反过来（先发现被占、再入队），中间被打断而持有者恰好释放
     *   （它看到队列是空的 → 不唤醒任何人），本进程再去睡就永远没人叫了。
     *   这段必须关中断：定时器抢占插在中间同样会破坏原子性。
     *   用 sstatus.SIE 而不是 sie —— sret 会用陷阱帧里的 SPIE 把它恢复，
     *   所以掩码只作用于这一小段，不会泄漏给别的任务或用户态。 */
    while (1) {
        int acquired;

        disable_interrupt();

        list_add_tail(&current_running->list, &lock->block_queue);
        acquired = spin_lock_try_acquire(&lock->lock);
        if (acquired)
            list_del(&current_running->list);   // 抢到了，从等待队列里摘出来
        current_running->status = acquired ? TASK_RUNNING : TASK_BLOCKED;

        enable_interrupt();

        if (acquired)
            return;

        /* 没抢到：已经挂在阻塞队列上、状态也是 BLOCKED，让出 CPU 等唤醒。
         * 醒来后回到循环顶部重抢（也顺带容忍虚假唤醒）。 */
        do_scheduler();
    }
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    mutex_lock_t *lock = &mlocks[mlock_idx];

    /* "置空"和"唤醒队首"必须是一个原子动作：中间被抢占的话，等待者在
     * 锁已空的情况下却收不到唤醒，就会一直睡下去。 */
    disable_interrupt();

    spin_lock_release(&lock->lock);

    // 叫醒一个等待者；它醒来后会重新尝试抢占这把锁
    if (!list_empty(&lock->block_queue))
        do_unblock(lock->block_queue.next);

    enable_interrupt();
}
