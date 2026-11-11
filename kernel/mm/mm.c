#include <os/mm.h>
#include <os/sched.h>
#include <os/irq.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/string.h>
#include <printk.h>

/* =====================================================================
 * P4 内存管理（任务1：启用虚拟内存）
 *
 * 物理页：0x52000000 起按 4KB 向上分配；freePage 回收进小 free list。
 * 用户页表：三级 Sv39，根页表里 256..511 项从内核页表复制（内核空间），
 *   0..255 项给用户空间。
 * ===================================================================== */

#define MAX_FREE_PAGES 1024

static spin_lock_t mem_lock;
static spin_lock_t paging_lock;
static uintptr_t free_top = FREEMEM_KERNEL;
static ptr_t free_pages[MAX_FREE_PAGES];
static int free_n;

void paging_init(void)
{
    spin_lock_init(&mem_lock);
    spin_lock_init(&paging_lock);
    free_top = FREEMEM_KERNEL;
    free_n = 0;
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

/* 为一个用户虚页分配物理页，并清零。 */
static ptr_t alloc_user_page(uintptr_t pgdir, uintptr_t va)
{
    (void)pgdir;
    (void)va;
    ptr_t page = allocPage(1);

    memset((void *)page, 0, PAGE_SIZE);
    return page;
}

static uintptr_t alloc_page_helper_locked(uintptr_t va, uintptr_t pgdir)
{
    PTE *pte = walk_page_table(va, pgdir, 1);
    ptr_t page;

    if (*pte & _PAGE_PRESENT) {
        page = (ptr_t)pa2kva(get_pa(*pte));
        return (uintptr_t)page + (va & OFFSET_MASK_4K);
    }

    page = alloc_user_page(pgdir, va & ~OFFSET_MASK_4K);
    set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
    set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                       _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
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
    if (*pte & _PAGE_PRESENT) {
        page = (ptr_t)pa2kva(get_pa(*pte));
    } else {
        page = alloc_user_page(pgdir, va & ~OFFSET_MASK_4K);
        set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
    }
    set_attribute(pte, _PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED |
                       _PAGE_DIRTY | (perm & (_PAGE_READ | _PAGE_WRITE |
                                                _PAGE_EXEC)));
    spin_lock_release(&paging_lock);
    return (uintptr_t)page + (va & OFFSET_MASK_4K);
}

/* ------------------------------------------------------------------ */
/* 缺页处理（任务2）                                                   */
/* ------------------------------------------------------------------ */
/* 注意：本函数只做"分配 + 建映射"，不得调用 do_scheduler/do_block
 * 等会切换任务的函数——大锁版的 BKL 重入深度依赖"最外层 trap 才放锁"。 */
void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    uintptr_t va = (uintptr_t)stval;
    pcb_t *task = current_running;
    uintptr_t pgdir;
    PTE *pte;

    /* 内核空间的缺页不该发生；非规范地址（Sv39 要求 bit63..39 全等于
     * bit38）也修不了，否则会反复缺页死循环。都走统一的报错路径。 */
    if (((va >> 38) & 1) || (va >> 39) != 0) {
        handle_other(regs, stval, scause);
        return;
    }

    spin_lock_acquire(&paging_lock);
    pgdir = task->pgdir;
    pte = walk_page_table(va, pgdir, 1);
    if (pte == 0) {
        spin_lock_release(&paging_lock);
        handle_other(regs, stval, scause);
        return;
    }

    if (*pte & _PAGE_PRESENT) {
        /* 板上 A/D 位为 0 会触发缺页；只补位，不能覆盖原有权限。 */
        *pte |= _PAGE_ACCESSED | _PAGE_DIRTY;
    } else {
        ptr_t page = alloc_user_page(pgdir, va & ~OFFSET_MASK_4K);
        set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
        set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                           _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED |
                           _PAGE_DIRTY);
    }

    local_flush_tlb_page(va);
    local_flush_icache_all();
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

        if (!(entry & _PAGE_PRESENT))
            continue;

        if (level == 0) {
            freePage((ptr_t)pa2kva(get_pa(entry)));
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
