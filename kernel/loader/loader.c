#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

uint64_t load_task_img(const char *taskname)
{
    /**
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     *    —— 已废弃，改按名字加载（[p1-task4]）
     * 2. [p1-task4] load task via task name
     */
    // 任务4：在 tasks[] 里按名字匹配。
    //   tasks[] 由 init_task_info 从 AppInfo 区读出；未用的槽位被 createimage
    //   清零（name[0]=='\0'），这里直接跳过，不需要知道实际任务数。
    for (int i = 0; i < TASK_MAXNUM; ++i)
    {
        if (tasks[i].name[0] == '\0')
            continue;

        if (strcmp(tasks[i].name, taskname) == 0)
        {
            // 从 SD 卡读入该 app 的入口窗口。
            //   start_sector / sector_num / vaddr_entry 都来自镜像 AppInfo 区，
            //   不再依赖固定扇区数布局（Task 3 的公式已退役）。
            if (bios_sd_read(tasks[i].vaddr_entry,
                             tasks[i].sector_num,
                             tasks[i].start_sector) != 0)
                return 0;   // 任务存在但镜像读取失败

            return (uint64_t)tasks[i].vaddr_entry;
        }
    }

    return 0;   // 未找到
}