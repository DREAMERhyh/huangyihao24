#ifndef PGTABLE_H
#define PGTABLE_H

#include <type.h>

#define SATP_MODE_SV39 8
#define SATP_MODE_SV48 9

#define SATP_ASID_SHIFT 44lu
#define SATP_MODE_SHIFT 60lu

#define NORMAL_PAGE_SHIFT 12lu
#define NORMAL_PAGE_SIZE (1lu << NORMAL_PAGE_SHIFT)
#define LARGE_PAGE_SHIFT 21lu
#define LARGE_PAGE_SIZE (1lu << LARGE_PAGE_SHIFT)

#define PPN_BITS 9lu
#define NUM_PTE_ENTRY (1lu << PPN_BITS)
#define VPN_MASK ((1lu << PPN_BITS) - 1)
#define VA_MASK ((1lu << 39) - 1)
#define OFFSET_MASK_4K ((1lu << NORMAL_PAGE_SHIFT) - 1)

/* 内核虚拟地址 = 物理地址 + 0xffffffc0_00000000（P4 直接映射） */
#define KERNEL_KVA_BASE 0xffffffc000000000lu

/* 内核根页表固定放在 0x51000000 物理页 */
#define PGDIR_PA 0x51000000lu
#define PGDIR_KVA (PGDIR_PA + KERNEL_KVA_BASE)
#define BOOT_PGDIR_PA (PGDIR_PA + NORMAL_PAGE_SIZE)

/* Flush translations in this hart's TLB. */
static inline void local_flush_tlb_all(void)
{
    __asm__ __volatile__ ("sfence.vma" : : : "memory");
}

static inline void local_flush_tlb_page(unsigned long addr)
{
    __asm__ __volatile__ ("sfence.vma %0" : : "r" (addr) : "memory");
}

static inline void local_flush_icache_all(void)
{
    __asm__ __volatile__ ("fence.i" ::: "memory");
}

static inline void set_satp(unsigned mode, unsigned asid, unsigned long ppn)
{
    unsigned long v = ((unsigned long)mode << SATP_MODE_SHIFT) |
                      ((unsigned long)asid << SATP_ASID_SHIFT) |
                      (ppn & ((1lu << SATP_ASID_SHIFT) - 1));
    __asm__ __volatile__("sfence.vma\ncsrw satp, %0" : : "rK"(v) : "memory");
}

/*
 * PTE format:
 * | 63..54 | 53..10 | 9 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *  保留      PPN      RSW   D   A   G   U   X   W   R   V
 */
#define _PAGE_PRESENT  (1lu << 0)
#define _PAGE_READ     (1lu << 1)
#define _PAGE_WRITE    (1lu << 2)
#define _PAGE_EXEC     (1lu << 3)
#define _PAGE_USER     (1lu << 4)
#define _PAGE_GLOBAL   (1lu << 5)
#define _PAGE_ACCESSED (1lu << 6)
#define _PAGE_DIRTY    (1lu << 7)
#define _PAGE_SOFT     (1lu << 8)   /* 软件位：页已被换出到 SD 卡 */

#define _PAGE_PFN_SHIFT 10lu
#define _PAGE_FLAG_MASK ((1lu << _PAGE_PFN_SHIFT) - 1)

typedef uint64_t PTE;

/* 虚实地址平移 */
static inline uintptr_t kva2pa(uintptr_t kva)
{
    return kva - KERNEL_KVA_BASE;
}

static inline uintptr_t pa2kva(uintptr_t pa)
{
    return pa + KERNEL_KVA_BASE;
}

/* 从 PTE 取物理地址（4KB 粒度）；非叶子节点也可用来取下一级页表物理地址 */
static inline uint64_t get_pa(PTE entry)
{
    return (entry >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT;
}

/* 从 PTE 取页框号（PPN） */
static inline long get_pfn(PTE entry)
{
    return (long)(entry >> _PAGE_PFN_SHIFT);
}

static inline void set_pfn(PTE *entry, uint64_t pfn)
{
    *entry = (*entry & _PAGE_FLAG_MASK) | ((pfn & ((1lu << 44) - 1))
                                           << _PAGE_PFN_SHIFT);
}

static inline long get_attribute(PTE entry, uint64_t mask)
{
    return entry & mask;
}

static inline void set_attribute(PTE *entry, uint64_t bits)
{
    *entry = (*entry & ~_PAGE_FLAG_MASK) | (bits & _PAGE_FLAG_MASK);
}

static inline void clear_attribute(PTE *entry, uint64_t bits)
{
    *entry &= ~bits;
}

/* 清空一个 4KB 页目录；boot 阶段 VM 未开时传物理地址，之后传内核虚地址 */
static inline void clear_pgdir(uintptr_t pgdir_addr)
{
    PTE *p = (PTE *)pgdir_addr;
    for (int i = 0; i < (int)NUM_PTE_ENTRY; ++i)
        p[i] = 0;
}

static inline void get_vpn(uintptr_t va, uint64_t *vpn2, uint64_t *vpn1,
                           uint64_t *vpn0)
{
    va &= VA_MASK;
    *vpn2 = (va >> (NORMAL_PAGE_SHIFT + 2 * PPN_BITS)) & VPN_MASK;
    *vpn1 = (va >> (NORMAL_PAGE_SHIFT + PPN_BITS)) & VPN_MASK;
    *vpn0 = (va >> NORMAL_PAGE_SHIFT) & VPN_MASK;
}

#endif /* PGTABLE_H */
