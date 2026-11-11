#include <os/task.h>
#include <os/mm.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

/* P4：加载器不再把 app 拷到固定物理窗口，而是：
 *   ① 从 SD 卡读 app 的 PT_LOAD 数据到内核中转缓冲；
 *   ② 为 [vaddr_entry, vaddr_entry + mem_size) 逐页建用户页表；
 *   ③ 把 byte_size 字节抄进映射后的物理页，其余保持 0（.bss）。
 * apps.img 里只存 PT_LOAD 段数据（createimage 的 write_segment），
 * 本实验每个 app 只有一个 PT_LOAD，所以按顺序拷贝即可。
 */
#define LOAD_STAGING_BYTES 32768
static uint8_t load_staging[LOAD_STAGING_BYTES]
    __attribute__((aligned(SECTOR_SIZE)));

uint64_t load_task_img(const char *taskname, uintptr_t pgdir)
{
    for (int i = 0; i < TASK_MAXNUM; ++i) {
        if (tasks[i].name[0] == '\0')
            continue;
        if (strcmp(tasks[i].name, taskname) != 0)
            continue;

        uint32_t abs_off = (uint32_t)boot_apps_base_off + tasks[i].start_byte;
        uint32_t sector = abs_off / SECTOR_SIZE;
        uint32_t offset = abs_off % SECTOR_SIZE;
        uint32_t size = tasks[i].byte_size;
        uint32_t mem_size = tasks[i].mem_size;
        uint32_t need = NBYTES2SEC(offset + size);

        if (size == 0 || need * SECTOR_SIZE > LOAD_STAGING_BYTES)
            return 0;
        if (mem_size < size)
            mem_size = size;

        if (bios_sd_read((unsigned)kva2pa((uintptr_t)load_staging), need,
                         sector) != 0)
            return 0;

        /* 下面的逐页拷贝假设程序入口是页对齐的（本课程 ELF 都在
         * 0x10000）；非对齐入口会让第 2 页起的数据整体错位，直接拒绝。 */
        if (tasks[i].vaddr_entry & (PAGE_SIZE - 1))
            return 0;

        uintptr_t va = tasks[i].vaddr_entry;
        uint32_t remain = size;
        uint32_t src_off = 0;

        for (uint32_t done = 0; done < mem_size; done += PAGE_SIZE) {
            uintptr_t cur_va = va + done;
            uintptr_t page_va = ROUNDDOWN(cur_va, PAGE_SIZE);
            uintptr_t kva = alloc_page_helper(page_va, pgdir);
            uint32_t page_off = (uint32_t)(cur_va & (PAGE_SIZE - 1));
            uint32_t n = PAGE_SIZE - page_off;

            if (n > mem_size - done)
                n = mem_size - done;
            if (n > remain)
                n = remain;

            if (n > 0) {
                memcpy((void *)(kva + page_off),
                       (const void *)(load_staging + offset + src_off), n);
                src_off += n;
                remain -= n;
            }
        }

        local_flush_icache_all();
        return (uint64_t)va;
    }

    return 0;
}
