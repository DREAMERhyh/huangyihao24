#include <os/mm.h>
#include <os/sched.h>
#include <os/irq.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/string.h>
#include <printk.h>
#include <csr.h>

/* =====================================================================
 * P4 内存管理（C-core）
 *
 * 物理页：0x52000000 起按 4KB 向上分配；freePage 回收进小 free list。
 * 用户页表：三级 Sv39，根页表里 256..511 项从内核页表复制（内核空间），
 *   0..255 项给用户空间。
 * 换页：每个进程最多 USER_PAGE_LIMIT 个常驻可换出页；超过后按 FIFO
 *   选本进程自己的一个页写 SD 卡，PTE 清 V、置 SOFT、PFN 存 swap 槽号。
 * 共享内存：0x100000000 起找空页，多个进程映射到同一物理页。
 * ===================================================================== */

#define MAX_FREE_PAGES 8192
/* 每个进程最多 USER_PAGE_LIMIT 个常驻可换出页；所有进程的常驻页都要在
 * 这张表里登记，所以上限必须 >= NUM_MAX_TASK * USER_PAGE_LIMIT。 */
#define MAX_SWAP_ENTRIES (NUM_MAX_TASK * USER_PAGE_LIMIT)

typedef struct {
    PTE *pte;
    uintptr_t pgdir;
    uintptr_t va;
    uint64_t seq;       /* 加入顺序：换出时选 seq 最小的页（严格 FIFO） */
    int used;
} swap_entry_t;

static spin_lock_t mem_lock;
static spin_lock_t paging_lock;
static uintptr_t free_top = FREEMEM_KERNEL;
static ptr_t free_pages[MAX_FREE_PAGES];
static int free_n;

static swap_entry_t swap_entries[MAX_SWAP_ENTRIES];
static uint64_t swap_seq;
static uint8_t swap_slot_used[SWAP_MAX_PAGES / 8];

typedef struct {
    int key;
    uintptr_t kva;
    int refcount;
    int used;
} shm_entry_t;

static shm_entry_t shm_entries[32];

void paging_init(void)
{
    spin_lock_init(&mem_lock);
    spin_lock_init(&paging_lock);
    free_top = FREEMEM_KERNEL;
    free_n = 0;
    swap_seq = 0;
    memset(swap_entries, 0, sizeof(swap_entries));
    memset(swap_slot_used, 0, sizeof(swap_slot_used));
    memset(shm_entries, 0, sizeof(shm_entries));
}

/* ------------------------------------------------------------------ */
/* 物理页分配                                                          */
/* ------------------------------------------------------------------ */
ptr_t allocPage(int numPage)
{
    ptr_t ret;

    spin_lock_acquire(&mem_lock);
    if (numPage <= 0) {
        spin_lock_release(&mem_lock);
        return 0;
    }

    if (numPage == 1 && free_n > 0) {
        ret = free_pages[--free_n];
    } else {
        ret = ROUND(free_top, PAGE_SIZE);
        if (ret + (uintptr_t)numPage * PAGE_SIZE > ALLMEM_KERNEL) {
            spin_lock_release(&mem_lock);
            /* 物理内存用尽：继续跑只会把页表映到未映射的 KVA 上，
             * 直接明确停机，比随机缺页好定位。 */
            printk("kernel out of physical memory!\n");
            while (1)
                ;
        }
        free_top = ret + (uintptr_t)numPage * PAGE_SIZE;
    }
    spin_lock_release(&mem_lock);

    return ret;
}

void freePage(ptr_t baseAddr)
{
    if (baseAddr == 0)
        return;

    spin_lock_acquire(&mem_lock);
    if (free_n < MAX_FREE_PAGES)
        free_pages[free_n++] = baseAddr;
    spin_lock_release(&mem_lock);
}

/* ------------------------------------------------------------------ */
/* 页表                                                                */
/* ------------------------------------------------------------------ */
PTE *walk_page_table(uintptr_t va, uintptr_t pgdir, int create)
{
    uint64_t vpn2, vpn1, vpn0;
    PTE *l2 = (PTE *)pgdir;
    PTE *l1, *l0;

    get_vpn(va, &vpn2, &vpn1, &vpn0);

    if (!(l2[vpn2] & _PAGE_PRESENT)) {
        if (!create)
            return 0;
        ptr_t page = allocPage(1);
        clear_pgdir(page);
        set_pfn(&l2[vpn2], kva2pa(page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&l2[vpn2], _PAGE_PRESENT);
    }
    /* 2MB 叶子（内核直接映射）不能再往下一级走，否则会把物理页当页表。 */
    if (l2[vpn2] & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC))
        return 0;
    l1 = (PTE *)pa2kva(get_pa(l2[vpn2]));

    if (!(l1[vpn1] & _PAGE_PRESENT)) {
        if (!create)
            return 0;
        ptr_t page = allocPage(1);
        clear_pgdir(page);
        set_pfn(&l1[vpn1], kva2pa(page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&l1[vpn1], _PAGE_PRESENT);
    }
    if (l1[vpn1] & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC))
        return 0;
    l0 = (PTE *)pa2kva(get_pa(l1[vpn1]));

    return &l0[vpn0];
}

/* 把内核页表的上半部分（内核空间）复制到用户根页表。
 * 只复制 256..511，避免把启动期的 0x50000000 恒等映射带进用户地址空间。 */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    PTE *dest = (PTE *)dest_pgdir;
    PTE *src = (PTE *)src_pgdir;

    for (int i = (int)(NUM_PTE_ENTRY / 2); i < (int)NUM_PTE_ENTRY; ++i)
        dest[i] = src[i];
}

/* ------------------------------------------------------------------ */
/* 换页                                                                */
/* ------------------------------------------------------------------ */
static int swap_slot_alloc(void)
{
    for (uint32_t i = 0; i < SWAP_MAX_PAGES; ++i) {
        if (!(swap_slot_used[i / 8] & (1u << (i % 8)))) {
            swap_slot_used[i / 8] |= (1u << (i % 8));
            return (int)i;
        }
    }
    return -1;
}

static void swap_slot_free(int slot)
{
    if (slot >= 0 && slot < (int)SWAP_MAX_PAGES)
        swap_slot_used[slot / 8] &= ~(1u << (slot % 8));
}

static int swap_entries_for(uintptr_t pgdir)
{
    int n = 0;
    for (int i = 0; i < MAX_SWAP_ENTRIES; ++i)
        if (swap_entries[i].used && swap_entries[i].pgdir == pgdir)
            ++n;
    return n;
}

static void swap_entry_add(PTE *pte, uintptr_t pgdir, uintptr_t va)
{
    for (int i = 0; i < MAX_SWAP_ENTRIES; ++i) {
        if (!swap_entries[i].used) {
            swap_entries[i].pte = pte;
            swap_entries[i].pgdir = pgdir;
            swap_entries[i].va = va;
            swap_entries[i].seq = ++swap_seq;
            swap_entries[i].used = 1;
            return;
        }
    }
}

static void swap_entry_remove(PTE *pte)
{
    for (int i = 0; i < MAX_SWAP_ENTRIES; ++i) {
        if (swap_entries[i].used && swap_entries[i].pte == pte) {
            swap_entries[i].used = 0;
            return;
        }
    }
}

/* 从本进程里挑一页写回 SD，返回腾出来的物理页 KVA（供新页复用）。
 * FIFO：选本进程里 seq 最小（最早加入）且仍在内存的页。
 * 只挑本进程自己的页：同一进程的线程被钉在同一核上，别的核不会同时
 * 跑它，所以换出后不需要给别的核发 TLB shootdown。 */
static ptr_t swap_out_one(uintptr_t pgdir)
{
    swap_entry_t *victim = 0;

    for (int i = 0; i < MAX_SWAP_ENTRIES; ++i) {
        if (!swap_entries[i].used || swap_entries[i].pgdir != pgdir)
            continue;
        if (!(*swap_entries[i].pte & _PAGE_PRESENT))
            continue;
        if (victim == 0 || swap_entries[i].seq < victim->seq)
            victim = &swap_entries[i];
    }
    if (victim == 0)
        return 0;

    int slot = swap_slot_alloc();
    if (slot < 0)
        return 0;

    PTE *pte = victim->pte;
    ptr_t page = (ptr_t)pa2kva(get_pa(*pte));
    if (bios_sd_write((unsigned)kva2pa(page), SWAP_SECTORS_PER_PAGE,
                      SWAP_START_SECTOR +
                          (unsigned)slot * SWAP_SECTORS_PER_PAGE) != 0) {
        /* 写失败就别换出：保持 PTE 原样，宁可暂时多分配一页，也不能
         * 把没落盘的数据标成"已在磁盘上"。 */
        swap_slot_free(slot);
        return 0;
    }

    /* PTE 变成"在磁盘上"：V=0，SOFT=1，PFN 存槽号 */
    set_pfn(pte, (uint64_t)slot);
    *pte &= ~_PAGE_PRESENT;
    *pte |= _PAGE_SOFT;
    local_flush_tlb_page(victim->va);
    swap_entry_remove(pte);
    return page;
}

static void swap_in_one(PTE *pte, uintptr_t va, uintptr_t pgdir)
{
    int slot = (int)get_pfn(*pte);
    ptr_t page;

    /* 换入也要守住每进程常驻页上限：否则"换入一页 + 新分配一页"会让
     * 常驻页无限增长，swap 测试的读回阶段会突破 128 页限制。 */
    if (swap_entries_for(pgdir) >= USER_PAGE_LIMIT)
        page = swap_out_one(pgdir);
    else
        page = allocPage(1);
    if (page == 0)
        page = allocPage(1);

    if (bios_sd_read((unsigned)kva2pa(page), SWAP_SECTORS_PER_PAGE,
                     SWAP_START_SECTOR +
                         (unsigned)slot * SWAP_SECTORS_PER_PAGE) != 0) {
        /* 读不回来就明确停机；继续把垃圾页标成 present 会静默毁数据。
         * 此时仍持 paging_lock/BKL，表现为"打了这一行后不再动"——正常，
         * 不是调度卡死。 */
        printk("swap in failed: slot %d\n", slot);
        while (1)
            ;
    }

    set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
    *pte &= ~_PAGE_SOFT;
    set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                       _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
    swap_slot_free(slot);
    swap_entry_add(pte, pgdir, va);
}

/* 为一个用户虚页分配物理页；超过本进程上限时先换出一页。 */
static ptr_t alloc_user_page(uintptr_t pgdir, uintptr_t va)
{
    ptr_t page = 0;

    if (swap_entries_for(pgdir) >= USER_PAGE_LIMIT)
        page = swap_out_one(pgdir);       /* 腾出旧页给新页复用 */
    if (page == 0)
        page = allocPage(1);

    memset((void *)page, 0, PAGE_SIZE);
    return page;
}

static uintptr_t alloc_page_helper_locked(uintptr_t va, uintptr_t pgdir)
{
    PTE *pte = walk_page_table(va, pgdir, 1);
    ptr_t page;

    if (pte == 0)
        return 0;

    if (*pte & _PAGE_PRESENT) {
        page = (ptr_t)pa2kva(get_pa(*pte));
        return (uintptr_t)page + (va & OFFSET_MASK_4K);
    }
    if (*pte & _PAGE_SOFT) {
        swap_in_one(pte, va & ~OFFSET_MASK_4K, pgdir);
        page = (ptr_t)pa2kva(get_pa(*pte));
        return (uintptr_t)page + (va & OFFSET_MASK_4K);
    }

    page = alloc_user_page(pgdir, va & ~OFFSET_MASK_4K);
    set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
    set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                       _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
    swap_entry_add(pte, pgdir, va & ~OFFSET_MASK_4K);
    return (uintptr_t)page + (va & OFFSET_MASK_4K);
}

uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    uintptr_t ret;
    spin_lock_acquire(&paging_lock);
    ret = alloc_page_helper_locked(va, pgdir);
    spin_lock_release(&paging_lock);
    return ret;
}

uintptr_t map_user_page(uintptr_t va, uintptr_t pgdir, uint64_t perm)
{
    PTE *pte;
    ptr_t page;

    spin_lock_acquire(&paging_lock);
    pte = walk_page_table(va, pgdir, 1);
    if (pte == 0) {
        spin_lock_release(&paging_lock);
        return 0;
    }
    if (*pte & _PAGE_PRESENT) {
        page = (ptr_t)pa2kva(get_pa(*pte));
    } else {
        /* 新映射覆盖一个已换出的旧页时，先归还它的 swap 槽。 */
        if (*pte & _PAGE_SOFT) {
            swap_entry_remove(pte);
            swap_slot_free((int)get_pfn(*pte));
        }
        page = alloc_user_page(pgdir, va & ~OFFSET_MASK_4K);
        set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
        swap_entry_add(pte, pgdir, va & ~OFFSET_MASK_4K);
    }
    set_attribute(pte, _PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED |
                       _PAGE_DIRTY | (perm & (_PAGE_READ | _PAGE_WRITE |
                                                _PAGE_EXEC)));
    spin_lock_release(&paging_lock);
    return (uintptr_t)page + (va & OFFSET_MASK_4K);
}

/* ------------------------------------------------------------------ */
/* 缺页处理                                                            */
/* ------------------------------------------------------------------ */
/* 非法地址/无法修复的缺页：U 态来自用户程序，终止它即可；S 态说明
 * 内核自己踩了坏指针，保留 handle_other 的现场断言便于定位。 */
static void handle_bad_fault(regs_context_t *regs, uint64_t stval,
                             uint64_t scause)
{
    if (!(regs->sstatus & SR_SPP)) {
        do_exit();
        return;
    }
    handle_other(regs, stval, scause);
}

/* 注意：本函数只做"分配/换入 + 建映射"，不得调用 do_scheduler/do_block
 * 等会切换任务的函数——大锁版的 BKL 重入深度依赖"最外层 trap 才放锁"，
 * 在嵌套缺页里调度会让接棒方只减一层深度。 */
void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    uintptr_t va = (uintptr_t)stval;
    pcb_t *task = current_running;
    uintptr_t pgdir;
    PTE *pte;

    /* 内核空间的缺页不该发生；非规范地址（Sv39 要求 bit63..39 全等于
     * bit38）也修不了，否则会反复缺页死循环。都走统一的报错路径。 */
    if (((va >> 38) & 1) || (va >> 39) != 0) {
        handle_bad_fault(regs, stval, scause);
        return;
    }

    spin_lock_acquire(&paging_lock);
    pgdir = task->pgdir;
    pte = walk_page_table(va, pgdir, 1);
    if (pte == 0) {
        spin_lock_release(&paging_lock);
        handle_bad_fault(regs, stval, scause);
        return;
    }

    if (*pte & _PAGE_PRESENT) {
        /* 板上 A/D 位为 0 会触发缺页；只补位，不能覆盖原有权限。
         * 但权限不足（写只读页/取指不可执行页）不是 A/D 问题：补位后
         * 重试还会缺页，会变成持锁的永久 trap 风暴，按坏访问处理。 */
        uint64_t need = 0;
        if (scause == EXCC_STORE_PAGE_FAULT)
            need = _PAGE_WRITE;
        else if (scause == EXCC_INST_PAGE_FAULT)
            need = _PAGE_EXEC;
        else if (scause == EXCC_LOAD_PAGE_FAULT)
            need = _PAGE_READ;
        if ((*pte & need) != need) {
            spin_lock_release(&paging_lock);
            handle_bad_fault(regs, stval, scause);
            return;
        }
        *pte |= _PAGE_ACCESSED | _PAGE_DIRTY;
    } else if (*pte & _PAGE_SOFT) {
        swap_in_one(pte, va & ~OFFSET_MASK_4K, pgdir);
    } else {
        ptr_t page = alloc_user_page(pgdir, va & ~OFFSET_MASK_4K);
        set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
        set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                           _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED |
                           _PAGE_DIRTY);
        swap_entry_add(pte, pgdir, va & ~OFFSET_MASK_4K);
    }

    local_flush_tlb_page(va);
    local_flush_icache_all();
    spin_lock_release(&paging_lock);
}

/* ------------------------------------------------------------------ */
/* 共享内存                                                            */
/* ------------------------------------------------------------------ */
static shm_entry_t *shm_find_key(int key)
{
    for (int i = 0; i < (int)(sizeof(shm_entries) / sizeof(shm_entries[0])); ++i)
        if (shm_entries[i].used && shm_entries[i].key == key)
            return &shm_entries[i];
    return 0;
}

static shm_entry_t *shm_find_kva(uintptr_t kva)
{
    for (int i = 0; i < (int)(sizeof(shm_entries) / sizeof(shm_entries[0])); ++i)
        if (shm_entries[i].used && shm_entries[i].kva == kva)
            return &shm_entries[i];
    return 0;
}

static int shm_va_free(uintptr_t va, uintptr_t pgdir)
{
    PTE *pte = walk_page_table(va, pgdir, 0);
    /* SOFT = 该虚拟页已换出到磁盘，仍然属于本进程，不能拿来当共享内存。 */
    return (pte == 0) || !(*pte & (_PAGE_PRESENT | _PAGE_SOFT));
}

static uintptr_t shm_page_get_locked(int key)
{
    pcb_t *task = current_running;
    uintptr_t pgdir = task->pgdir;
    shm_entry_t *se;
    uintptr_t va;

    /* 先找空虚拟页；找不到就直接失败，别先分配物理页/条目造成泄漏。 */
    for (va = SHM_VA_BASE; va < SHM_VA_END; va += PAGE_SIZE) {
        if (shm_va_free(va, pgdir))
            break;
    }
    if (va >= SHM_VA_END)
        return 0;

    se = shm_find_key(key);
    if (se == 0) {
        for (int i = 0; i < (int)(sizeof(shm_entries) / sizeof(shm_entries[0])); ++i) {
            if (!shm_entries[i].used) {
                ptr_t page = allocPage(1);
                memset((void *)page, 0, PAGE_SIZE);
                shm_entries[i].used = 1;
                shm_entries[i].key = key;
                shm_entries[i].kva = page;
                shm_entries[i].refcount = 0;
                se = &shm_entries[i];
                break;
            }
        }
        if (se == 0)
            return 0;
    }

    PTE *pte = walk_page_table(va, pgdir, 1);
    if (pte == 0)
        return 0;
    set_pfn(pte, kva2pa(se->kva) >> NORMAL_PAGE_SHIFT);
    set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                       _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
    se->refcount++;
    local_flush_tlb_page(va);
    return va;
}

uintptr_t shm_page_get(int key)
{
    uintptr_t va;
    spin_lock_acquire(&paging_lock);
    va = shm_page_get_locked(key);
    spin_lock_release(&paging_lock);
    return va;
}

/* 解除一个共享页映射（进程退出时也用它回收残留映射） */
static int shm_unmap_va(uintptr_t va, uintptr_t pgdir)
{
    PTE *pte = walk_page_table(va, pgdir, 0);
    shm_entry_t *se;

    if (pte == 0 || !(*pte & _PAGE_PRESENT))
        return 0;

    se = shm_find_kva((uintptr_t)pa2kva(get_pa(*pte)));
    if (se == 0)
        return 0;

    *pte = 0;
    local_flush_tlb_page(va);
    se->refcount--;
    if (se->refcount <= 0) {
        freePage((ptr_t)se->kva);
        se->used = 0;
        se->kva = 0;
        se->key = 0;
        se->refcount = 0;
    }
    return 1;
}

void shm_page_dt(uintptr_t addr)
{
    /* 只接受用户半区的地址；内核半区没有可解除的共享映射。 */
    if ((addr >> 38) != 0)
        return;

    spin_lock_acquire(&paging_lock);
    shm_unmap_va(addr & ~OFFSET_MASK_4K, current_running->pgdir);
    spin_lock_release(&paging_lock);
}

/* 释放单个用户虚拟页（线程退出时归还它独占的用户栈页）。
 * 共享内存页走引用计数，不会直接 freePage。 */
void free_user_page(uintptr_t va, uintptr_t pgdir)
{
    spin_lock_acquire(&paging_lock);
    PTE *pte = walk_page_table(va, pgdir, 0);

    if (pte != 0) {
        if (*pte & _PAGE_PRESENT) {
            ptr_t page = (ptr_t)pa2kva(get_pa(*pte));
            if (va >= SHM_VA_BASE && va < SHM_VA_END &&
                shm_unmap_va(va, pgdir)) {
                /* 共享页由 shm_unmap_va 按引用计数回收 */
            } else {
                swap_entry_remove(pte);
                freePage(page);
            }
        } else if (*pte & _PAGE_SOFT) {
            swap_slot_free((int)get_pfn(*pte));
        }
        *pte = 0;
        local_flush_tlb_page(va);
    }
    spin_lock_release(&paging_lock);
}

/* ------------------------------------------------------------------ */
/* 进程页表回收                                                        */
/* ------------------------------------------------------------------ */
static void free_level(PTE *table, int level, uintptr_t base_va,
                       uintptr_t pgdir)
{
    int limit = (level == 2) ? (int)(NUM_PTE_ENTRY / 2) : (int)NUM_PTE_ENTRY;

    for (int i = 0; i < limit; ++i) {
        PTE entry = table[i];
        uintptr_t va = base_va + ((uintptr_t)i << (NORMAL_PAGE_SHIFT +
                                                     PPN_BITS * level));

        if (!(entry & _PAGE_PRESENT)) {
            if (level == 0 && (entry & _PAGE_SOFT)) {
                /* 已换出的页：只还 swap 槽 */
                swap_slot_free((int)get_pfn(entry));
            }
            continue;
        }

        if (level == 0) {
            ptr_t page = (ptr_t)pa2kva(get_pa(entry));
            if (va >= SHM_VA_BASE && va < SHM_VA_END &&
                shm_unmap_va(va, pgdir))
                ;               /* 共享页由 shm_unmap_va 按引用计数回收 */
            else
                freePage(page);
        } else if (entry & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) {
            /* 大页（本实验不用） */
            freePage((ptr_t)pa2kva(get_pa(entry)));
        } else {
            PTE *next = (PTE *)pa2kva(get_pa(entry));
            free_level(next, level - 1, va, pgdir);
            freePage((ptr_t)next);
        }
    }
}

void free_user_pgdir(uintptr_t pgdir)
{
    spin_lock_acquire(&paging_lock);
    for (int i = 0; i < MAX_SWAP_ENTRIES; ++i)
        if (swap_entries[i].used && swap_entries[i].pgdir == pgdir)
            swap_entries[i].used = 0;

    free_level((PTE *)pgdir, 2, 0, pgdir);
    freePage((ptr_t)pgdir);
    spin_lock_release(&paging_lock);
}

/* 撤掉启动期的恒等映射（0x50000000-0x51000000，顶层的第 1 项） */
void remove_identity_map(void)
{
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA);
    pgdir[1] = 0;
    local_flush_tlb_all();
}
