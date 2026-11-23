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

/* P4 任务1：所有用户进程共用同一套用户虚拟地址布局。
 *   程序：0x10000 起（由 ELF 的 p_vaddr 决定）
 *   主线程栈：0xf00010000 向下 */
#define USER_STACK_ADDR   0xf00010000lu
#define USER_STACK_PAGES  8

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

extern ptr_t allocPage(int numPage);
extern void freePage(ptr_t baseAddr);

extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern PTE *walk_page_table(uintptr_t va, uintptr_t pgdir, int create);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);
extern uintptr_t map_user_page(uintptr_t va, uintptr_t pgdir, uint64_t perm);
extern void free_user_pgdir(uintptr_t pgdir);
extern void paging_init(void);

#endif /* MM_H */
