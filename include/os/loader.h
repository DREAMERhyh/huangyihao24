#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <type.h>

/* 任务3：按 taskid 加载 →（任务4 改为）按 taskname 加载 */
uint64_t load_task_img(const char *taskname);

#endif