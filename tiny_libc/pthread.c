#include <pthread.h>

/* 线程入口 trampoline：内核把 sepc 指到这里，a0=fn、a1=arg。
 * 用户函数正常 return 后必须主动 sys_exit，否则会落回函数自身。 */
static void __pthread_entry(void (*fn)(void *), void *arg)
{
    fn(arg);
    sys_exit();
}

/* P4 任务4：用户侧 pthread 封装。
 * 真正的线程 PCB / 栈 / 页表共享都在内核 sys_pthread_create 里完成；
 * 这里只负责把内核返回的线程号写回调用者给的 pthread_t。 */
void pthread_create(pthread_t *thread,
                    void (*start_routine)(void*),
                    void *arg)
{
    long tid = sys_pthread_create((void (*)(void *, void *))__pthread_entry,
                                  start_routine, arg);
    if (thread != 0)
        *thread = (pthread_t)tid;
}

int pthread_join(pthread_t thread)
{
    return sys_pthread_join(thread);
}
