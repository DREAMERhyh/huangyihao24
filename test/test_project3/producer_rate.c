/* P3 任务5：细粒度锁对比用的生产者（任务书 §5.6）
 *   与 consumer_rate.c 配对，用条件变量做严格的 1:1 乒乓：
 *   共享库存只允许 0/1——生产一个就唤醒消费者，消费者取走后再唤醒生产者。
 *   ★ 为什么用条件变量而不是无上限累加：
 *     无上限累加时，生产者会跑在消费者前面攒出"库存"，速率就变成由
 *     CPU 吞吐决定——开 fly 多一个吃 CPU 的任务，速率必然差不多减半；
 *     乒乓模型下每次生产/消费都要阻塞-唤醒一次，速率由"唤醒→调度"的
 *     延迟决定，fly 只占空闲 CPU，不会改变这个节奏（§5.6 要对比的正是
 *     "fly 的 console 系统调用会不会卡住 producer/consumer 的锁操作"）。 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define SHARED      ((volatile int *)0x56000000)
#define LOCK_KEY    45          /* 与 consumer_rate 同 key → 同一把锁 */
#define COND_KEY    46          /* 与 consumer_rate 同 key → 同一个条件变量 */

int main(void)
{
    int lock_idx = sys_mutex_init(LOCK_KEY);
    int cond_idx = sys_condition_init(COND_KEY);

    while (1) {
        sys_mutex_acquire(lock_idx);
        while (*SHARED != 0)                 /* 库存已满（1）：等消费者取走 */
            sys_condition_wait(cond_idx, lock_idx);
        ++*SHARED;                           /* 生产 1 个单位 */
        sys_condition_signal(cond_idx);      /* 叫醒消费者 */
        sys_mutex_release(lock_idx);
    }

    return 0;
}
