#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x52000000
#define TASK_MAXNUM      16
#define TASK_SIZE        0x10000
#define TASK_NAME_LEN    16       /* app 名最长长度（含 '\0'） */

#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* Task 4 的镜像布局（紧凑排列，无固定空泡）：
 *
 *   扇区 0                bootblock
 *   扇区 1 .. K           内核，K = NBYTES2SEC(内核实际大小)
 *                          （bootblock 从镜像偏移 0x1fc 读出 K）
 *   扇区 K+1              AppInfo 区（task_info_t 数组，固定 1 扇区，见 APPINFO_SECTORS）
 *   扇区 K+1+APPINFO_SECTORS ...   各 app 按实际大小紧凑排列，app i 紧贴 app i-1
 *
 * 可计算性论证（为什么 AppInfo 一定在 K+1）：
 *   bootblock 唯一能可靠拿到的布局信息是 0x1fc 处的内核扇区数 K，
 *   app 又是紧凑排列的、尾部不可预期，所以"1+K"是 AppInfo 唯一可自定位的位置。
 *   内核侧只要有 K 就能算出 AppInfo 扇区号，进而在启动时读出 task_info_t 数组。
 */
#define APPINFO_SECTORS    NBYTES2SEC(TASK_MAXNUM * sizeof(task_info_t))  /* = 1 */

/* bootblock 经寄存器传给内核的引导参数（Task 4 + Task 5）：
 *   a0 = AppInfo 区起始扇区号
 *   a1 = 用户程序数目（bootblock 从镜像偏移 0x1f8 读出）
 *   a2 = apps 区基址（SD 绝对扇区 = 1+K+A，即 kernel.img 总扇区数）
 * head.S 在清完 BSS 后把这三个值存进下面的全局量，再跳 main。
 * 注意：内核一律不直接读 0x50200xxx（那是 bootblock 扇区，只允许 bootblock 读）。 */
extern uint64_t boot_appinfo_sector;
extern uint64_t boot_appnum;
extern uint64_t boot_apps_base;

/* Task 4 的 task_info_t：按"名称 + 起始扇区 + 扇区数 + 内存入口"设计 */
typedef struct {
    char      name[TASK_NAME_LEN];   /* app 名，与 ELF 文件名一致 */
    uint32_t  start_sector;          /* 该 app 在镜像中的起始扇区号 */
    uint32_t  sector_num;            /* 占用扇区数 = NBYTES2SEC(p_filesz) */
    uint32_t  vaddr_entry;           /* 内存入口地址 */
} task_info_t;

extern task_info_t tasks[TASK_MAXNUM];

#endif