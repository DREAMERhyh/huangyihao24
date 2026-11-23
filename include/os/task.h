#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x52000000
/* 镜像里 AppInfo 最多容纳的 app 数（与 tools/createimage.c 的同名宏一致）。
 * 任务5 从 16 提到 32：test_project3 有 16 个测试程序 + shell + fly = 18 个，
 *   16 会让 createimage 的 taskinfo[] 栈溢出（实测）。 */
#define TASK_MAXNUM      32
#define TASK_SIZE        0x10000
#define TASK_NAME_LEN    16       /* app 名最长长度（含 '\0'） */

#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* 任务5 的镜像布局（**字节级**紧密排列，不补到扇区边界）：
 *
 *   kernel.img:
 *     字节 0 .. 511      bootblock（补 0 到 512 —— 全镜像唯一强制补零处：
 *                        · 0x1fc/0x1fe 字段住在第 0 扇区最后 4 字节；
 *                        · u-boot 按块读第 0 扇区并跳到 0x50200000）
 *     字节 512 ..        内核（KB 字节，不补）
 *     紧接其后           AppInfo（APPINFO_BYTES = 1024 字节，不补）
 *     => kernel.img 总字节数 = 512 + KB + 1024
 *
 *   apps.img:
 *     各 app 首尾相接（app i 从 app i-1 的最后一个字节 + 1 开始）
 *     末尾补若干 0，只为让 kernel.img + apps.img 落在整扇区上（块设备边界）
 *
 * 由于 AppInfo / apps 都不再按扇区对齐，内核读取它们时**必须经过中转缓冲**：
 * 块设备只能整扇区读，没有偏移参数（见 init_task_info / load_task_img）。
 */
#define APPINFO_BYTES    (TASK_MAXNUM * sizeof(task_info_t))   /* 32×32 = 1024 字节 */

/* bootblock 经寄存器传给内核的引导参数（任务5 起为【字节】语义）：
 *   a0 = 核号
 *   a1 = AppInfo 起始字节偏移（镜像内）
 *   a2 = 用户程序数目（bootblock 从镜像偏移 0x1f8 读出）
 *   a3 = apps 区起始字节偏移（= kernel.img 总字节数）
 * head.S 在清完 BSS 后把这三个值存进下面的全局量，再跳 main。
 * 注意：内核一律不直接读 0x50200xxx（那是 bootblock 扇区，只允许 bootblock 读）。 */
extern uint64_t boot_appinfo_off;
extern uint64_t boot_appnum;
extern uint64_t boot_apps_base_off;

/* task_info_t：按"名称 + 起始字节 + 字节数 + 内存入口"设计。
 * 任务5 起字段改为【字节】语义——镜像内部按字节紧密排列，不再按扇区对齐。 */
typedef struct {
    char      name[TASK_NAME_LEN];   /* app 名，与 ELF 文件名一致 */
    uint32_t  start_byte;            /* 该 app 在 apps.img 中的【字节偏移】 */
    uint32_t  byte_size;             /* 该 app 的【字节数】（各 PT_LOAD 段 p_filesz 之和） */
    uint32_t  vaddr_entry;           /* 内存入口地址（P4 统一为 0x10000） */
    uint32_t  mem_size;              /* 该 app 的【内存大小】（各 PT_LOAD 段 p_memsz 之和） */
} task_info_t;

/* 收官护栏：这些断言守住 createimage 与内核结构体一致、以及"中转缓冲够大"等前提；
 * 若将来改动破坏任一约束，编译期即报错，而不是在运行期跑飞。 */
_Static_assert(sizeof(task_info_t) == 32, "task_info_t 必须是 32 字节（createimage 同款）");
_Static_assert(TASK_NAME_LEN == 16, "TASK_NAME_LEN 必须与 createimage 一致（16）");
_Static_assert(APPINFO_BYTES == 1024, "AppInfo 必须是 32×32 = 1024 字节");

extern task_info_t tasks[TASK_MAXNUM];

#endif
