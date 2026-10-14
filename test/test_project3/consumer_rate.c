/* P3 任务5：细粒度锁对比用的消费者（任务书 §5.6）
 *   与 producer_rate.c 配对：生产/消费每次只进行 1 个单位，两个进程都
 *   持续做系统调用（互斥锁 acquire/release + 时间查询）。本进程每秒
 *   打印一次"消费速率"——这是大锁版与细粒度版在 fly 干扰下的对比指标。
 *   共享变量放绝对地址（与任务2 condition 测试同一约定），打印压低到
 *   每秒一行（§5.6 要求"减少打印频率"）。 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define SHARED      ((volatile int *)0x56000000)
#define LOCK_KEY    45          /* 与任务2 测试的 42/58 错开 */

int main(void)
{
    int lock_idx = sys_mutex_init(LOCK_KEY);
    sys_move_cursor(0, 1);

    long base = sys_get_timebase();     /* 1 秒 = base 个计数 */
    long t0 = sys_get_tick();
    int consumed = 0;

    while (1) {
        sys_mutex_acquire(lock_idx);
        if (*SHARED > 0) {
            --*SHARED;
            consumed++;
        }
        sys_mutex_release(lock_idx);

        long t = sys_get_tick();
        if (t - t0 >= base) {
            /* 实测区间换算成每秒速率（四舍五入到整数） */
            int rate = (int)((long)consumed * base / (t - t0));
            sys_move_cursor(0, 1);
            printf("[consumer] consume rate: %d /s\n", rate);
            consumed = 0;
            t0 = t;
        }
    }

    return 0;
}
