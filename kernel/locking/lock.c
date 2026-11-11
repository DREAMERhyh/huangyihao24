#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <os/irq.h>
#include <os/string.h>
#include <csr.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];

/* 临界区用的"是否被打断"开关。
 *   不能用 disable_interrupt()/enable_interrupt() 直接配一对：后者会把
 *   sstatus.SIE 无条件置 1。内核里绝大多数临界区都跑在陷阱处理中，那时
 *   SIE 本该是 0（陷入时硬件清掉的）；一旦被置 1，内核就可能被时钟中断
 *   再次打断并触发嵌套调度。
 *   所以这里"存原值-关-按原值恢复"。
 *   （P4 的 entry.S 已让同步缺页可以从 S 态安全压帧，但定时器抢占仍要
 *   按原 SIE 恢复，不能无条件开。） */
static inline uint64_t irq_save(void)
{
    uint64_t sstatus;

    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    disable_interrupt();
    return sstatus;
}

static inline void irq_restore(uint64_t sstatus)
{
    if (sstatus & SR_SIE)
        enable_interrupt();
    else
        disable_interrupt();
}

/* 空闲槽位的标记：mlocks[i].key == KEY_UNUSED 表示这把锁还没被任何 key 占用。
 * 用户程序用到的 key 必须是别的值（测试用的是 42）。 */
#define KEY_UNUSED (-1)

/* 任务5：释放互斥锁的核心逻辑（定义在文件后部，do_mutex_lock_release 与
 *   do_condition_wait 都要用；后者在临界段里复用它避免重复拿 sync_lock） */
static void mutex_release_locked(mutex_lock_t *lock);

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for (int i = 0; i < LOCK_NUM; ++i) {
        mlocks[i].key = KEY_UNUSED;
        spin_lock_init(&mlocks[i].lock);
        list_init(&mlocks[i].block_queue);
        mlocks[i].owner = 0;
    }

    /* 任务2 的三种原语槽位也在这里初始化——main.c 已经在调 init_locks()，
     * 不必往那份文件里再加三行（避免和主会话的改动交错）。重复初始化无害。 */
    init_barriers();
    init_conditions();
    init_mbox();
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
     *   没有才占一个空闲槽位。handle 就是 mlocks 的下标，范围 0..LOCK_NUM-1。
     * P4：key/owner 属 sync 域；双核同时 init 同一个 key 可能各自选中
     *   不同槽位，必须和 barrier/condition 一样用 sync_lock 串行化。 */
    uint64_t sstatus = irq_save();
    int ret = -1;

    FINE_LOCK(&sync_lock);

    for (int i = 0; i < LOCK_NUM; ++i) {
        if (mlocks[i].key == key) {
            ret = i;
            goto out;
        }
    }

    for (int i = 0; i < LOCK_NUM; ++i) {
        if (mlocks[i].key == KEY_UNUSED) {
            spin_lock_init(&mlocks[i].lock);
            list_init(&mlocks[i].block_queue);
            mlocks[i].owner = 0;
            mlocks[i].key = key;
            ret = i;
            break;
        }
    }

out:
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
    return ret;   // -1 = 锁用光了
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    /* P3 评审补丁：handle 来自用户态，先验界再索引（越界会读写内核内存） */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM)
        return;
    mutex_lock_t *lock = &mlocks[mlock_idx];

    /* ★ 先入队、再抢锁：这样"入队"与"检查锁状态"之间没有空档，不会丢唤醒。
     *   如果反过来（先发现被占、再入队），中间被打断而持有者恰好释放
     *   （它看到队列是空的 → 不唤醒任何人），本进程再去睡就永远没人叫了。
     *   这段必须关中断：定时器抢占插在中间同样会破坏原子性。
     *   用 sstatus.SIE 而不是 sie —— sret 会用陷阱帧里的 SPIE 把它恢复，
     *   所以掩码只作用于这一小段，不会泄漏给别的任务或用户态。
     *   任务5：细粒度版再叠两把域锁——sync_lock 护 owner、sched_lock 护
     *   阻塞队列/状态（锁序 sync→sched，见 smp.h）；放锁恢复中断后才调度。 */
    while (1) {
        int acquired;
        uint64_t sstatus = irq_save();

        FINE_LOCK(&sync_lock);
        FINE_LOCK(&sched_lock);

        list_add_tail(&current_running->list, &lock->block_queue);
        acquired = spin_lock_try_acquire(&lock->lock);
        if (acquired) {
            list_del(&current_running->list);   // 抢到了，从等待队列里摘出来
            lock->owner = current_running;      // P3：记下持锁者（kill 时要用）
        }
        current_running->status = acquired ? TASK_RUNNING : TASK_BLOCKED;

        FINE_UNLOCK(&sched_lock);
        FINE_UNLOCK(&sync_lock);

        irq_restore(sstatus);

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
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM)
        return;
    mutex_lock_t *lock = &mlocks[mlock_idx];

    /* "置空"和"唤醒队首"必须是一个原子动作：中间被抢占的话，等待者在
     * 锁已空的情况下却收不到唤醒，就会一直睡下去。
     * 任务5：核心逻辑拆到 mutex_release_locked（condition_wait 要复用），
     *   这里负责按纪律拿/放两把域锁。 */
    uint64_t sstatus = irq_save();

    FINE_LOCK(&sync_lock);
    FINE_LOCK(&sched_lock);

    mutex_release_locked(lock);

    FINE_UNLOCK(&sched_lock);
    FINE_UNLOCK(&sync_lock);

    irq_restore(sstatus);
}

/* =====================================================================
 * 任务2：同步原语实现（barrier / condition / mailbox）
 *
 * 三种原语共用同一条"阻塞-唤醒"骨架，纪律展开成三句：
 *   ① 「判条件、改状态、（必要时）挂进等待队列」必须在**同一段关中断
 *      窗口**里完成——定时器抢占不能插在中间。否则或者丢唤醒（挂队
 *      之前发生 wakeup，没人记得我们），或者出现"同挂在两个队列上"的
 *      双重身份（pcb 只有一个 list 结点，链表会被串坏）；
 *   ② 恢复中断之后才允许让出（do_scheduler）——关着中断切换任务会把
 *      SIE=0 带进下一个任务的执行流；
 *   ③ 被唤醒后回到循环开头重新判条件（容忍虚假唤醒）。唤醒方一律
 *      "全唤醒"，不做精确点名：测试规模下最省心，也绝不丢唤醒。
 * ===================================================================== */

/* 把自己挂上 q 并置 BLOCKED，恢复中断后让出 CPU。
 *   调用者必须已 irq_save()，并把那次保存的 sstatus 传进来——"挂队 +
 *   置状态"必须和调用处的"判条件"处在同一段关中断窗口里，中间不留空档。
 *   任务5：细粒度版要求调用者同时持有 sync_lock 与 sched_lock（判条件
 *   与挂队同处一个跨核临界段）；本函数按拿锁相反的顺序放开两锁、
 *   恢复中断，之后才允许让出（自旋锁绝不能带进 do_scheduler）。 */
static void block_on_queue(list_head *q, uint64_t sstatus)
{
    current_running->status = TASK_BLOCKED;
    list_add_tail(&current_running->list, q);

    FINE_UNLOCK(&sched_lock);
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
    do_scheduler();
}

/* 任务5：释放互斥锁的核心逻辑，调用者必须已持 sync_lock + sched_lock。
 *   单拆一个函数是为 do_condition_wait：它要在自己的临界段里放互斥锁，
 *   直接调完整版会重复拿 sync_lock——自旋锁不可重入，会死锁。 */
static void mutex_release_locked(mutex_lock_t *lock)
{
    lock->owner = 0;                    // P3：持锁者即将易主（或变空）
    spin_lock_release(&lock->lock);

    // 叫醒一个等待者；它醒来后会重新尝试抢占这把锁
    if (!list_empty(&lock->block_queue))
        do_unblock(lock->block_queue.next);
}

/* ------------------------------- barrier ------------------------------- */

barrier_t barriers[BARRIER_NUM];

void init_barriers(void)
{
    for (int i = 0; i < BARRIER_NUM; ++i) {
        spin_lock_init(&barriers[i].lock);
        list_init(&barriers[i].wait_list);
        barriers[i].goal = 0;
        barriers[i].arrived = 0;
        barriers[i].key = KEY_UNUSED;
    }
}

int do_barrier_init(int key, int goal)
{
    /* 与 do_mutex_lock_init 同款：同一个 key 复用同一个 handle（= 下标）
     * 任务5：key/goal 属 sync 域（细粒度版）；改完统一从尾部返回。 */
    /* P3 评审补丁：goal<=0 时 wait 的判据 arrived==goal 永远凑不齐，
     *   全体参与者永久挂起——直接按参数错误拒绝。 */
    if (goal <= 0)
        return -1;
    uint64_t sstatus = irq_save();
    int ret = -1;

    FINE_LOCK(&sync_lock);

    for (int i = 0; i < BARRIER_NUM; ++i) {
        if (barriers[i].key == key) {
            ret = i;
            goto out;
        }
    }

    for (int i = 0; i < BARRIER_NUM; ++i) {
        if (barriers[i].key == KEY_UNUSED) {
            spin_lock_init(&barriers[i].lock);
            list_init(&barriers[i].wait_list);
            barriers[i].goal = goal;
            barriers[i].arrived = 0;
            barriers[i].key = key;
            ret = i;   // 屏障槽位分配成功
            goto out;
        }
    }
    // 槽位用光：ret 保持 -1

out:
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
    return ret;
}

void do_barrier_wait(int bar_idx)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM)
        return;
    barrier_t *b = &barriers[bar_idx];

    uint64_t sstatus = irq_save();

    /* 任务5：细粒度版两把域锁同时圈住"判 arrived（sync 数据）+ 挂队
     *   （sched 数据）"——两件事之间不能松锁，否则最后一人可能在我们
     *   还没挂上时数满人数，把"还没睡的人"当"到齐"放行（丢唤醒）。 */
    FINE_LOCK(&sync_lock);
    FINE_LOCK(&sched_lock);

    /* 到达计数 +1；凑满 goal 的"最后一人"不睡，由它放行所有人：
     *   · 清零 arrived，为下一轮做准备（测试是 10 轮循环）；
     *   · 把等待队列上的进程全部唤醒。
     * 于是三个 test_barrier 打印的循环计数必然是"一起增长"的。 */
    b->arrived++;
    if (b->arrived == b->goal) {
        b->arrived = 0;
        while (!list_empty(&b->wait_list))
            do_unblock(b->wait_list.next);
        FINE_UNLOCK(&sched_lock);
        FINE_UNLOCK(&sync_lock);
        irq_restore(sstatus);
        return;
    }

    /* 还没凑齐：挂到等待队列上睡，被最后一人唤醒后直接返回
     * （计数已被清好，本进程下一轮重新计数）。
     * block_on_queue 内部按序放掉两把锁、恢复中断后才让出。 */
    block_on_queue(&b->wait_list, sstatus);
}

void do_barrier_destroy(int bar_idx)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM)
        return;
    barrier_t *b = &barriers[bar_idx];

    /* 测试在所有参与者退出后才 destroy（barrier.c 先 waitpid 三个孩子），
     * 正常不会有等待者；万一有也一并唤醒——总比睡死强。 */
    uint64_t sstatus = irq_save();

    FINE_LOCK(&sync_lock);
    FINE_LOCK(&sched_lock);

    while (!list_empty(&b->wait_list))
        do_unblock(b->wait_list.next);
    b->arrived = 0;
    b->goal = 0;
    b->key = KEY_UNUSED;

    FINE_UNLOCK(&sched_lock);
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
}

/* ------------------------------ condition ------------------------------ */

condition_t conditions[CONDITION_NUM];

void init_conditions(void)
{
    for (int i = 0; i < CONDITION_NUM; ++i) {
        spin_lock_init(&conditions[i].lock);
        list_init(&conditions[i].wait_list);
        conditions[i].key = KEY_UNUSED;
    }
}

int do_condition_init(int key)
{
    uint64_t sstatus = irq_save();
    int ret = -1;

    FINE_LOCK(&sync_lock);

    for (int i = 0; i < CONDITION_NUM; ++i) {
        if (conditions[i].key == key) {
            ret = i;
            goto out;
        }
    }

    for (int i = 0; i < CONDITION_NUM; ++i) {
        if (conditions[i].key == KEY_UNUSED) {
            spin_lock_init(&conditions[i].lock);
            list_init(&conditions[i].wait_list);
            conditions[i].key = key;
            ret = i;   // 条件变量槽位分配成功
            goto out;
        }
    }
    // 槽位用光：ret 保持 -1

out:
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
    return ret;
}

void do_condition_wait(int cond_idx, int mutex_idx)
{
    /* P3 评审补丁：两个 handle 都来自用户态，任一越界直接返回
     *   （不解锁不阻塞——调用者按条件变量语义重查条件即可）。 */
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM ||
        mutex_idx < 0 || mutex_idx >= LOCK_NUM)
        return;
    condition_t *c = &conditions[cond_idx];

    /* ★ 全过程在一段关中断窗口里：挂上条件队列 → 放开互斥锁 → 置 BLOCKED。
     *   任何两步之间留空档都会出事：
     *   · "先放锁、后挂队"：广播可能在空档里发生——我们还不在队列上，
     *     叫不到我们，而我们随后照睡不误，永久卡死（丢唤醒）；
     *   · "挂好队、还没置 BLOCKED"：若空档里被调度走，状态还是 RUNNING，
     *     do_scheduler 会把我们当就绪任务再排进就绪队列——pcb 只有一个
     *     list 结点，同时在两个队列上链表就串坏了。
     *   窗口里同时把锁放了：consumer 是持锁进来等的，不放锁生产者进不来。
     *   丢唤醒的另一半证明：生产者要改共享条件必须先拿到这把锁，而锁
     *   在我们手里直到这一步——"检查条件为假 → 挂上队"之间条件不可能变。
     *   任务5：细粒度版三件事（挂条件队列/放互斥锁/置 BLOCKED）同处一段
     *   sync+sched 双锁临界区；放互斥锁必须用 _locked 变体（本段已持
     *   sync_lock，调完整版会重复拿锁死锁）。放锁恢复中断后才调度。 */
    uint64_t sstatus = irq_save();

    FINE_LOCK(&sync_lock);
    FINE_LOCK(&sched_lock);

    list_add_tail(&current_running->list, &c->wait_list);
    mutex_release_locked(&mlocks[mutex_idx]);
    current_running->status = TASK_BLOCKED;

    FINE_UNLOCK(&sched_lock);
    FINE_UNLOCK(&sync_lock);

    irq_restore(sstatus);
    do_scheduler();

    /* 被唤醒后按条件变量的语义重新持有互斥锁再返回（测试里 consumer
     * 回到 while 循环、持锁重新检查计数）。此刻不持任何域锁，走完整版。 */
    do_mutex_lock_acquire(mutex_idx);
}

void do_condition_signal(int cond_idx)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM)
        return;
    condition_t *c = &conditions[cond_idx];

    uint64_t sstatus = irq_save();
    FINE_LOCK(&sched_lock);
    if (!list_empty(&c->wait_list))
        do_unblock(c->wait_list.next);      /* 叫醒一个；它醒来自己去抢锁 */
    FINE_UNLOCK(&sched_lock);
    irq_restore(sstatus);
}

void do_condition_broadcast(int cond_idx)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM)
        return;
    condition_t *c = &conditions[cond_idx];

    uint64_t sstatus = irq_save();
    FINE_LOCK(&sched_lock);
    while (!list_empty(&c->wait_list))
        do_unblock(c->wait_list.next);      /* 全体叫醒；醒来各自重查条件 */
    FINE_UNLOCK(&sched_lock);
    irq_restore(sstatus);
}

void do_condition_destroy(int cond_idx)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM)
        return;
    condition_t *c = &conditions[cond_idx];

    uint64_t sstatus = irq_save();
    FINE_LOCK(&sync_lock);
    FINE_LOCK(&sched_lock);
    while (!list_empty(&c->wait_list))
        do_unblock(c->wait_list.next);
    c->key = KEY_UNUSED;
    FINE_UNLOCK(&sched_lock);
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
}

/* ------------------------------- mailbox ------------------------------- */

mailbox_t mboxes[MBOX_NUM];

void init_mbox(void)
{
    for (int i = 0; i < MBOX_NUM; ++i) {
        spin_lock_init(&mboxes[i].lock);
        list_init(&mboxes[i].send_wait);
        list_init(&mboxes[i].recv_wait);
        mboxes[i].name[0] = '\0';
        mboxes[i].head = mboxes[i].tail = mboxes[i].count = 0;
        mboxes[i].refcount = 0;
    }
}

int do_mbox_open(char *name)
{
    char key[MBOX_NAME_LEN];

    /* 名字来自用户态指针：先拷进内核缓冲、限长、补 '\0'，再拿去比对
     *   （strncpy 读用户内存，放在锁外做，别占着域锁） */
    strncpy(key, name, MBOX_NAME_LEN - 1);
    key[MBOX_NAME_LEN - 1] = '\0';

    uint64_t sstatus = irq_save();
    int ret = -1;

    FINE_LOCK(&sync_lock);      /* 名字表/引用计数属 sync 域 */

    for (int i = 0; i < MBOX_NUM; ++i) {
        if (mboxes[i].name[0] != '\0' && strcmp(mboxes[i].name, key) == 0) {
            mboxes[i].refcount++;       /* 同名共享：引用计数 +1，返回同一个 */
            ret = i;
            goto out;
        }
    }

    for (int i = 0; i < MBOX_NUM; ++i) {
        if (mboxes[i].name[0] == '\0') {
            spin_lock_init(&mboxes[i].lock);
            list_init(&mboxes[i].send_wait);
            list_init(&mboxes[i].recv_wait);
            strncpy(mboxes[i].name, key, MBOX_NAME_LEN - 1);
            mboxes[i].name[MBOX_NAME_LEN - 1] = '\0';
            mboxes[i].head = mboxes[i].tail = mboxes[i].count = 0;
            mboxes[i].refcount = 1;
            ret = i;    // 信箱槽位分配成功
            goto out;
        }
    }
    // 槽位用光：ret 保持 -1

out:
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
    return ret;
}

void do_mbox_close(int mbox_idx)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM)
        return;
    mailbox_t *m = &mboxes[mbox_idx];

    uint64_t sstatus = irq_save();
    FINE_LOCK(&sync_lock);
    if (m->name[0] != '\0' && m->refcount > 0 && --m->refcount == 0) {
        /* 引用计数归零才真正释放。能走到这里说明没有其他持有者，
         * 也就不可能有阻塞者——阻塞者必然还持有该信箱的引用。 */
        m->name[0] = '\0';
        m->head = m->tail = m->count = 0;
    }
    FINE_UNLOCK(&sync_lock);
    irq_restore(sstatus);
}

int do_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM)
        return -1;

    mailbox_t *m = &mboxes[mbox_idx];
    int blocked = 0;

    /* 单条比整个缓冲还大：剩余空间永远凑不够，直接拒绝（测试不会出现）。
     *   评审补丁：负长度会绕过判据并反向腐蚀 m->count（send 减），一并拒绝。 */
    if (msg_length <= 0 || msg_length > MAX_MBOX_LENGTH)
        return -1;

    while (1) {
        uint64_t sstatus = irq_save();

        /* 任务5：缓冲（sync 域）与收发等待队列（sched 域）双锁齐拿——
         *   判空间、写环、唤醒接收方的两件事必须在同一临界段。 */
        FINE_LOCK(&sync_lock);
        FINE_LOCK(&sched_lock);

        /* 阻塞判据（任务书 §4.5 原文）：msg_length > 剩余空间。
         * 注意是严格大于——恰好放得下不算阻塞。 */
        if (msg_length <= MAX_MBOX_LENGTH - m->count) {
            for (int i = 0; i < msg_length; ++i) {
                m->buffer[m->tail] = ((char *)msg)[i];
                m->tail = (m->tail + 1) % MAX_MBOX_LENGTH;
            }
            m->count += msg_length;

            /* 有新数据：把接收方全部叫醒（它们醒来重查"够不够整段取走"） */
            while (!list_empty(&m->recv_wait))
                do_unblock(m->recv_wait.next);

            FINE_UNLOCK(&sched_lock);
            FINE_UNLOCK(&sync_lock);
            irq_restore(sstatus);
            return blocked;     /* 返回值 = 本次是否阻塞过（0/1，测试累加成计数） */
        }

        /* 空间不足：记下 blocked、挂到发送等待队列，醒来回循环重查
         *   （block_on_queue 内部放两锁、恢复中断后才让出） */
        blocked = 1;
        block_on_queue(&m->send_wait, sstatus);
    }
}

int do_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    /* P3 评审补丁：handle 来自用户态，先验界再索引 */
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM)
        return -1;

    mailbox_t *m = &mboxes[mbox_idx];
    int blocked = 0;

    /* 评审补丁：负长度会绕过判据并反向腐蚀 m->count（recv 增），一并拒绝 */
    if (msg_length <= 0 || msg_length > MAX_MBOX_LENGTH)
        return -1;

    while (1) {
        uint64_t sstatus = irq_save();

        FINE_LOCK(&sync_lock);
        FINE_LOCK(&sched_lock);

        /* 字节流语义：凑够 msg_length 字节才整段取走。
         * server 对一次 send 分两次 recv（先 12B 的 MsgHeader_t、再正文），
         * 靠的就是"不到量就继续等"——先到的字节先出（FIFO）。 */
        if (m->count >= msg_length) {
            for (int i = 0; i < msg_length; ++i) {
                ((char *)msg)[i] = m->buffer[m->head];
                m->head = (m->head + 1) % MAX_MBOX_LENGTH;
            }
            m->count -= msg_length;

            /* 腾出空间：把发送方全部叫醒 */
            while (!list_empty(&m->send_wait))
                do_unblock(m->send_wait.next);

            FINE_UNLOCK(&sched_lock);
            FINE_UNLOCK(&sync_lock);
            irq_restore(sstatus);
            return blocked;
        }

        blocked = 1;
        block_on_queue(&m->recv_wait, sstatus);
    }
}
