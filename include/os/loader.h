#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <type.h>

/* 任务3：按 taskid 加载 →（任务4 改为）按 taskname 加载。
 * P4：同时把程序装进指定进程的页表 pgdir，返回入口虚拟地址。 */
uint64_t load_task_img(const char *taskname, uintptr_t pgdir);

#endif