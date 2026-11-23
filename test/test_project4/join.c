#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static volatile int finished;

static void worker(void *arg)
{
    (void)arg;
    sys_sleep(2);
    finished = 1;
}

int main(void)
{
    pthread_t thread;
    pthread_create(&thread, worker, 0);
    if (thread == 0 || pthread_join(thread) != 0 || !finished) {
        printf("pthread_join FAILED\n");
        return 1;
    }
    printf("pthread_join PASSED\n");
    return 0;
}
