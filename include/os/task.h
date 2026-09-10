#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x52000000
#define TASK_MAXNUM      16
#define TASK_SIZE        0x10000


#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* 任务3 的镜像布局：
 *   第 0 扇区                              bootblock
 *   第 1 扇区起                            内核，占 KERNEL_FIXED_SECTORS 个扇区
 *   第 1+KERNEL_FIXED_SECTORS 扇区起       各 app，每个占 APP_FIXED_SECTORS 个扇区
 * 注意：这两个宏必须与 tools/createimage.c 里的同名宏保持一致 */
#define KERNEL_FIXED_SECTORS   32
#define APP_FIXED_SECTORS      15

/* 镜像信息区，位于第 0 扇区尾部，由 createimage 写入：
 *   偏移 0x1f8 : 用户程序数目（2 字节，小端）
 *   偏移 0x1fc : 内核占用的扇区数（2 字节，小端，bootblock 用 lh 读取）
 *   偏移 0x1fe : 官方签名 0x55 0xaa */
#define IMG_APP_NUM_LOC        0x502001f8

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {

} task_info_t;

extern task_info_t tasks[TASK_MAXNUM];

#endif