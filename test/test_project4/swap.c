#include <stdio.h>
#include <unistd.h>

/* P4 任务3 自测：访问 256 个虚拟页（1MB），超过内核给每个进程的
 * 128 个常驻页上限，触发 FIFO 换页；全部写完后逐个读回校验。 */
#define N_PAGES 256
#define BASE    0x20000000lu
#define STRIDE  (4096 / sizeof(long))

int main(void)
{
    volatile long *base = (volatile long *)BASE;
    int i;

    sys_move_cursor(1, 1);
    printf("swap test: writing %d pages...\n", N_PAGES);

    for (i = 0; i < N_PAGES; ++i)
        base[(unsigned long)i * STRIDE] = 0x1234567800000000lu + i;

    printf("swap test: reading back...\n");
    for (i = 0; i < N_PAGES; ++i) {
        long v = base[(unsigned long)i * STRIDE];
        if (v != (long)(0x1234567800000000lu + i)) {
            printf("swap test FAILED at page %d: %lx\n", i, v);
            return -1;
        }
    }

    printf("swap test PASSED (%d pages)\n", N_PAGES);
    return 0;
}
