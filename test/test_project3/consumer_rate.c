/* P3 任务5：细粒度锁对比用的消费者（任务书 §5.6）
 *   与 producer_rate.c 配对：共享库存严格 0/1，取走一个就唤醒生产者。
 *   每秒打印一次消费速率（屏幕第 2 行），这是细粒度/大锁两版镜像的对比指标。
 *   速率口径 = 每秒完成多少轮"阻塞→唤醒→调度"的乒乓球。 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define SHARED      ((volatile int *)0x56000000)
#define LOCK_KEY    45          /* 与 producer_rate 同 key → 同一把锁 */
#define COND_KEY    46          /* 与 producer_rate 同 key → 同一个条件变量 */

int main(void)
{
    int lock_idx = sys_mutex_init(LOCK_KEY);
    int cond_idx = sys_condition_init(COND_KEY);
    sys_move_cursor(0, 1);

    long base = sys_get_timebase();     /* 1 秒 = base 个计数 */
    long t0 = sys_get_tick();
    int consumed = 0;

    while (1) {
        sys_mutex_acquire(lock_idx);
        while (*SHARED == 0)                 /* 库存空了：等生产者 */
            sys_condition_wait(cond_idx, lock_idx);
        --*SHARED;                           /* 消费 1 个单位 */
        sys_condition_signal(cond_idx);      /* 叫醒生产者 */
        sys_mutex_release(lock_idx);

        consumed++;

        long t = sys_get_tick();
        if (t - t0 >= base) {
            /* 实测区间换算成每秒速率（四舍五入到整数） */
            int rate = (int)((long)consumed * base / (t - t0));
            sys_move_cursor(0, 1);
            /* 行尾多写几个空格：新行比旧行短时把旧行残留字符盖掉 */
            printf("[consumer] consume rate: %d /s          \n", rate);
            consumed = 0;
            t0 = t;
        }
    }

    return 0;
}
