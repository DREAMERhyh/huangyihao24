#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>

uint64_t load_task_img(int taskid)
{
    /**
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */
    // 镜像布局（createimage 用固定扇区数排好）：
    //   扇区 0                                    bootblock
    //   扇区 1 .. 1+KERNEL_FIXED_SECTORS-1        内核
    //   扇区 1+KERNEL_FIXED_SECTORS+i*APP_FIXED_SECTORS    第 i 个 app
    //
    // 入口地址为什么可以直接用窗口基址，而不必从镜像里读 e_entry：
    //   1) riscv.lds 里 ENTRY(_start)，且 .text 段的第一个输入段是 *(.entry_function)
    //   2) crt0.S 的 _start 定义在 .entry_function 段，所以排在 app 代码之前
    //   3) Makefile 用 -Wl,--defsym=TEXT_START=<窗口基址> 给每个 app 单独赋值
    //   因此每个 app 的 _start 恰好落在自己的窗口基址上。实测四个 app 的
    //   e_entry 与窗口基址完全一致：2048=0x52000000 auipc=0x52010000
    //   bss=0x52020000 data=0x52030000。
    //   Task 4 改成按名加载时，e_entry 会存进 task_info_t，届时以镜像里的值为准。
    uint64_t entry = TASK_MEM_BASE + (uint64_t)taskid * TASK_SIZE;
    uint32_t block = 1 + KERNEL_FIXED_SECTORS + (uint32_t)taskid * APP_FIXED_SECTORS;

    // 一次读满该 app 的分配扇区数；镜像里的 padding 全是 0，
    // 所以窗口内不会被残留数据污染
    if (bios_sd_read((unsigned)entry, APP_FIXED_SECTORS, block) != 0)
        return 0;   // 读取失败，用 0 作为错误哨兵（合法入口恒 >= 0x52000000）

    return entry;
}
