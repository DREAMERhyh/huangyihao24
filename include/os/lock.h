/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Thread Lock
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#ifndef INCLUDE_LOCK_H_
#define INCLUDE_LOCK_H_

#include <os/list.h>

#define LOCK_NUM 16

typedef enum {
    UNLOCKED,
    LOCKED,
} lock_status_t;

typedef struct spin_lock
{
    volatile lock_status_t status;
} spin_lock_t;

typedef struct mutex_lock
{
    spin_lock_t lock;
    list_head block_queue;
    int key;
    /* P3：当前持锁者（无主为 NULL）。
     * kill 一个进程时，如果它正持有某把锁，内核要替它释放并唤醒等待者，
     * 否则这把锁永远锁死；没有这个字段就无从知道"谁持有"。 */
    struct pcb *owner;
} mutex_lock_t;

void init_locks(void);

void spin_lock_init(spin_lock_t *lock);
int spin_lock_try_acquire(spin_lock_t *lock);
void spin_lock_acquire(spin_lock_t *lock);
void spin_lock_release(spin_lock_t *lock);

int do_mutex_lock_init(int key);
void do_mutex_lock_acquire(int mlock_idx);
void do_mutex_lock_release(int mlock_idx);

/* 全局互斥锁数组（kernel/locking/lock.c 定义）——kill 进程时要遍历它
 * 释放被 kill 者持有的锁、摘除它挂着的等待队列 */
extern mutex_lock_t mlocks[LOCK_NUM];

/************************************************************/
/* ================ 任务2：同步原语（barrier / condition / mailbox）================
 * 三者共用同一条内核纪律（实现见 kernel/locking/lock.c）：
 *   「判条件 → 挂等待队列 → 置 BLOCKED」必须在一段关中断窗口里一次完成，
 *   恢复中断之后才允许让出 CPU；唤醒方一律"全唤醒"，醒来者自己重查。
 * 结构里的 spin_lock_t 是为任务3 的双核互斥预留的（单核阶段靠关中断就够），
 * 字段风格与 mutex_lock_t 保持一致。 */

typedef struct barrier
{
    spin_lock_t lock;
    list_head wait_list;    /* 已到达、但还没凑齐 goal 的进程（等最后一人放行） */
    int goal;               /* 需要同时到达的进程数（init 时给定，测试是 3） */
    int arrived;            /* 本轮已到达的个数；凑满 goal 时由最后一人清零 */
    int key;                /* 空闲槽位标记：KEY_UNUSED（同 mlocks） */
} barrier_t;

#define BARRIER_NUM 16

void init_barriers(void);
int do_barrier_init(int key, int goal);
void do_barrier_wait(int bar_idx);
void do_barrier_destroy(int bar_idx);

typedef struct condition
{
    spin_lock_t lock;
    list_head wait_list;    /* 在条件上等待的进程（cond_wait 挂上，signal/broadcast 叫醒） */
    int key;                /* 空闲槽位标记：KEY_UNUSED */
} condition_t;

#define CONDITION_NUM 16

void init_conditions(void);
int do_condition_init(int key);
void do_condition_wait(int cond_idx, int mutex_idx);
void do_condition_signal(int cond_idx);
void do_condition_broadcast(int cond_idx);
void do_condition_destroy(int cond_idx);

typedef struct semaphore
{
    // TODO [P3-TASK2 semaphore]
} semaphore_t;

#define SEMAPHORE_NUM 16

void init_semaphores(void);
int do_semaphore_init(int key, int init);
void do_semaphore_up(int sema_idx);
void do_semaphore_down(int sema_idx);
void do_semaphore_destroy(int sema_idx);

#define MAX_MBOX_LENGTH (64)
#define MBOX_NAME_LEN (32)

typedef struct mailbox
{
    spin_lock_t lock;
    list_head send_wait;        /* 空间不足而阻塞的发送者 */
    list_head recv_wait;        /* 数据不足而阻塞的接收者 */
    char name[MBOX_NAME_LEN];   /* 信箱名字（按名共享）；name[0]=='\0' 表示槽位空闲 */
    char buffer[MAX_MBOX_LENGTH];   /* 环形字节缓冲（有界缓冲 + FIFO，任务书 §4.5） */
    int head;                   /* 读指针：下一个要取出的字节 */
    int tail;                   /* 写指针：下一个要写入的位置 */
    int count;                  /* 当前存了多少字节（判空间/判取走的依据） */
    int refcount;               /* open 引用计数；减到 0 才释放槽位 */
} mailbox_t;

#define MBOX_NUM 16
void init_mbox();
int do_mbox_open(char *name);
void do_mbox_close(int mbox_idx);
int do_mbox_send(int mbox_idx, void * msg, int msg_length);
int do_mbox_recv(int mbox_idx, void * msg, int msg_length);

/************************************************************/

#endif
