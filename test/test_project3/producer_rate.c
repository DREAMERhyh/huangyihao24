/* P3 任务5：细粒度锁对比用的生产者（任务书 §5.6）
 *   每次只生产 1 个单位，无限循环、不打印（消费速率由 consumer_rate
 *   打印；§5.6 要求减少打印频率，避免打印自身与 fly 抢 console）。 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define SHARED      ((volatile int *)0x56000000)
#define LOCK_KEY    45          /* 与 consumer_rate.c 同 key → 同一把锁 */

int main(void)
{
    int lock_idx = sys_mutex_init(LOCK_KEY);

    while (1) {
        sys_mutex_acquire(lock_idx);
        ++*SHARED;
        sys_mutex_release(lock_idx);
    }

    return 0;
}
