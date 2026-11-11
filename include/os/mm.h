#ifndef MM_H
#define MM_H

#include <type.h>
#include <pgtable.h>

#define MEM_SIZE 32
#define PAGE_SIZE 4096

/* 内核启动栈（head.S 同一字面量）与内核动态页区。
 * 0x50200000-0x51000000 是内核镜像/页表；0x52000000 起才给动态分配。 */
#define INIT_KERNEL_STACK 0xffffffc050500000lu
#define FREEMEM_KERNEL    0xffffffc052000000lu
#define ALLMEM_KERNEL     0xffffffc060000000lu

/* P4 C-core：所有用户进程共用同一套用户虚拟地址布局。
 *   程序：0x10000 起（由 ELF 的 p_vaddr 决定）
 *   主线程栈：0xf00010000 向下
 *   线程栈：0xe00000000 起，每线程一页
 *   共享内存：0x100000000 起，内核动态找空页 */
#define USER_STACK_ADDR   0xf00010000lu
#define USER_STACK_PAGES  8
#define THREAD_STACK_BASE 0xe00000000lu
#define THREAD_STACK_STEP 0x2000lu
#define SHM_VA_BASE       0x100000000lu
#define SHM_VA_END        0x100100000lu

/* 人为限制"可换出的用户页"数量，超过就触发 FIFO 换页。
 * 128 页 = 512KB，足够普通测试；swap 测试会访问超过这个数量的页。 */
#define USER_PAGE_LIMIT   128
#define SWAP_START_SECTOR 0x10000u   /* 32MB 处开始；镜像在 Makefile 里 pad 到 64MB */
#define SWAP_MAX_PAGES    8192u      /* 32MB swap 区 */
#define SWAP_SECTORS_PER_PAGE (PAGE_SIZE / 512)

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

extern ptr_t allocPage(int numPage);
extern void freePage(ptr_t baseAddr);
extern void *kmalloc(size_t size);

extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern PTE *walk_page_table(uintptr_t va, uintptr_t pgdir, int create);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);
extern uintptr_t map_user_page(uintptr_t va, uintptr_t pgdir, uint64_t perm);
extern void free_user_pgdir(uintptr_t pgdir);
extern void remove_identity_map(void);
extern void paging_init(void);

/* 共享内存 */
extern uintptr_t shm_page_get(int key);
extern void shm_page_dt(uintptr_t addr);

#endif /* MM_H */
