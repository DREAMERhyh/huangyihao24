#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

/* 任务5：镜像内部按【字节】紧密排列，app 在镜像里**可能不在扇区边界上**；
 * 而块设备只能整扇区读、没有偏移参数。所以加载一个 app 要分两步：
 *   ① 把"覆盖该 app 的若干整扇区"读进本中转缓冲；
 *   ② 从缓冲内的 offset 处起，把 size 字节拷到 app 的窗口地址。
 * 缓冲 8KB（= 16 扇区），足以容纳"任意 app 字节数 + 最多 511 字节偏移"
 * 在目前规模内的组合；若某天 app 变大到装不下，会**拒绝加载**而不是越界写。 */
#define LOAD_STAGING_BYTES 8192
static uint8_t load_staging[LOAD_STAGING_BYTES]
    __attribute__((aligned(SECTOR_SIZE)));

uint64_t load_task_img(const char *taskname)
{
    /**
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     *    —— 已废弃，改按名字加载（[p1-task4]）
     * 2. [p1-task4] load task via task name
     */
    // 任务4/5：在 tasks[] 里按名字匹配。
    //   tasks[] 由 init_task_info 从 AppInfo 区读出；未用的槽位被 createimage
    //   清零（name[0]=='\0'），这里直接跳过，不需要知道实际任务数。
    for (int i = 0; i < TASK_MAXNUM; ++i)
    {
        if (tasks[i].name[0] == '\0')
            continue;

        if (strcmp(tasks[i].name, taskname) == 0)
        {
            // ① 算出该 app 在 SD 卡上的【绝对字节偏移】，再拆成"扇区号 + 扇区内偏移"
            uint32_t abs_off = (uint32_t)boot_apps_base_off
                             + tasks[i].start_byte;
            uint32_t sector  = abs_off / SECTOR_SIZE;
            uint32_t offset  = abs_off % SECTOR_SIZE;
            uint32_t size    = tasks[i].byte_size;
            uint32_t need    = NBYTES2SEC(offset + size);   // 要读几个整扇区

            // ② 防御：中转缓冲装不下就拒绝（绝不越界写）
            if (need * SECTOR_SIZE > LOAD_STAGING_BYTES)
                return 0;

            // ③ 读整扇区到中转缓冲
            if (bios_sd_read((unsigned)(unsigned long)load_staging, need,
                             sector) != 0)
                return 0;   // 任务存在但镜像读取失败

            // ④ 从缓冲内偏移处精确拷贝 size 字节到 app 的窗口
            memcpy((uint8_t *)(unsigned long)tasks[i].vaddr_entry,
                   (const uint8_t *)(load_staging + offset), size);

            return (uint64_t)tasks[i].vaddr_entry;
        }
    }

    return 0;   // 未找到
}
