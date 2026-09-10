#include <common.h>
#include <asm.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/loader.h>
#include <type.h>

#define VERSION_BUF 50

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];

// 任务3：镜像里记录的用户程序数目（createimage 写在镜像偏移 0x1f8）
int task_num = 0;

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
}

static void init_task_info(void)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
}

// 任务3：从镜像信息区读出用户程序数目，作为 taskid 的合法上界
static void init_task_num(void)
{
    task_num = *(const uint16_t *)IMG_APP_NUM_LOC;
}

// 任务3：逐个读字符并回显，直到读到一个数字为止，返回它代表的 taskid
static int read_taskid(void)
{
    while (1)
    {
        int ch = bios_getchar();

        if (ch == -1)
            continue;   // 无输入时跳过，否则会把 -1 当字符打出乱码

        bios_putchar(ch);

        if (ch >= '0' && ch <= '9')
            return ch - '0';   // 非数字字符（含回车）只回显、不参与解析
    }
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

int main(void)
{
    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info();

    // 任务3：从镜像信息区读出用户程序数目
    init_task_num();

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    // 任务3：交互式读入 taskid，加载并执行对应用户程序
    //   taskid 必须 < app 总数，越界则提示错误并回到输入循环，不得跑飞
    while (1)
    {
        bios_putstr("Please input a task id: ");

        int id = read_taskid();

        if (id >= task_num)
        {
            bios_putstr("Error: task id out of range!\n\r");
            continue;
        }

        uint64_t entry = load_task_img(id);

        if (entry == 0)
        {
            bios_putstr("Error: load task img failed!\n\r");
            continue;
        }

        // 跳转到 app 入口；app 的 crt0 会在 app main 返回后跳回内核
        //
        // 关于返回地址的说明（勘误后）：GCC 对函数指针调用本就会生成
        //   jalr ra, rs（rd=x1，返回地址进 ra）；objdump 会把 rd=ra 的
        //   jalr 简写成 `jalr a0` 这类省略形式（见 log/task3-工作日志.md
        //   "jalr 根因复核"），早期据此误判为"返回地址进 a0"。
        // 这里仍显式先设 ra 再间接跳转，属于双保险：行为与 GCC 隐式生成
        //   完全等价，且把"返回地址落在哪"写清楚，便于阅读与维护。
        asm volatile(
            "la   ra, 1f\n\t"
            "jalr a0, %0\n\t"
            "1:\n\t"
            :
            : "r"(entry)
            : "ra", "a0", "memory"
        );

        // TODO: [p1-task4] 改为按 taskname 加载并执行用户程序
    }

    return 0;
}
