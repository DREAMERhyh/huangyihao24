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

    /* ★ 关抢占把"判断锁状态"和"挂进阻塞队列"锁成一段原子操作。
     *   否则存在丢唤醒的窗口：本进程刚发现锁被占、还没入队时被打断，
     *   持有者恰好释放（看到阻塞队列空 → 不唤醒任何人），本进程再去睡就没人叫了。
     *   do_block 会在入队之后立刻恢复抢占（见 sched.c 的说明）。 */
    disable_preempt();

    while (!spin_lock_try_acquire(&lock->lock)) {
        do_block(&current_running->list, &lock->block_queue);
        /* 被唤醒后回到循环顶部重新抢：释放者只是把锁置为 UNLOCKED 并
         * 叫醒队首，真正的占有还是在这里完成的（也顺带容忍虚假唤醒）。 */
    }

    enable_preempt();
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    mutex_lock_t *lock = &mlocks[mlock_idx];

    disable_preempt();

    spin_lock_release(&lock->lock);

    // 叫醒一个等待者；它醒来后会重新尝试抢占这把锁
    if (!list_empty(&lock->block_queue))
        do_unblock(lock->block_queue.next);

    enable_preempt();
}
