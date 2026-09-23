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

// bootblock 经 head.S 传来的引导参数（任务5 起为【字节】语义）：
//   a0 = AppInfo 起始字节偏移，a1 = 用户程序数目，a2 = apps 区起始字节偏移
//   定义见 task.h；赋值在 head.S 清完 BSS 后完成
uint64_t boot_appinfo_off;
uint64_t boot_appnum;
uint64_t boot_apps_base_off;

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
    // 任务5：从 AppInfo 区读出 task_info_t 数组，填进 tasks[]。
    //   位置与数量来自 bootblock 传参（head.S 已存进 boot_appinfo_off /
    //   boot_appnum），不再直接读 0x50200xxx。
    //
    //   ★ 与任务4 的关键差别：AppInfo 在镜像里**不再按扇区对齐**（字节级紧密
    //   排列），而块设备只能整扇区读、没有偏移参数。所以必须：
    //     ① 算出 AppInfo 落在哪个扇区、扇区内偏移多少；
    //     ② 把"覆盖它的若干个整扇区"读进中转缓冲；
    //     ③ 从缓冲的 offset 处起，拷 APPINFO_BYTES 字节进 tasks[]。
    //   中转缓冲需 2 个扇区：offset ≤ 511，APPINFO_BYTES = 448，
    //   故 offset + 448 ≤ 959 < 1024。
    static uint8_t appinfo_buf[2 * SECTOR_SIZE]
        __attribute__((aligned(SECTOR_SIZE)));

    task_num = (int)boot_appnum;

    if (task_num <= 0 || task_num > TASK_MAXNUM) {
        task_num = 0;
        return;
    }

    uint32_t start  = (uint32_t)boot_appinfo_off;
    uint32_t sector = start / SECTOR_SIZE;              // 从哪块开始读
    uint32_t offset = start % SECTOR_SIZE;              // 块内偏移多少
    uint32_t need   = NBYTES2SEC(offset + APPINFO_BYTES);   // 要读几块

    if (need > sizeof(appinfo_buf) / SECTOR_SIZE) {     // 防御（不会发生）
        task_num = 0;
        return;
    }

    if (bios_sd_read((unsigned)(unsigned long)appinfo_buf, need,
                     sector) != 0) {
        task_num = 0;
        return;
    }

    // 只拷"有内容的那几行"（task_num × 28 字节），从缓冲的 offset 处起
    memcpy((void *)tasks, (const void *)(appinfo_buf + offset),
           (size_t)task_num * sizeof(task_info_t));
}

// 任务5：十进制无符号打印（内核无 itoa，除 10 取余、倒序输出）
static void put_uint(unsigned int val)
{
    char tmp[12];
    int i = 0;

    if (val == 0) {
        bios_putchar('0');
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + val % 10);
        val /= 10;
    }
    while (i > 0)
        bios_putchar(tmp[--i]);
}

// 任务5：十六进制打印（地址用 16 进制更直观）
static void put_hex(unsigned int val)
{
    static const char hex[] = "0123456789abcdef";
    char tmp[9];
    int i = 0;

    bios_putstr("0x");
    if (val == 0) {
        bios_putchar('0');
        return;
    }
    while (val > 0) {
        tmp[i++] = hex[val & 0xf];
        val >>= 4;
    }
    while (i > 0)
        bios_putchar(tmp[--i]);
}

// 任务5：显示应用信息（slide 25 可选项；name / start(相对扇区) / size / entry）
static void show_task_list(void)
{
    // 任务5：offset / size 均为【字节】语义（镜像内部按字节紧密排列）
    bios_putstr("name              offset size   entry\r\n");
    for (int i = 0; i < task_num; ++i)
    {
        int pad = TASK_NAME_LEN - strlen(tasks[i].name);

        bios_putstr(tasks[i].name);
        while (pad-- > 0)
            bios_putchar(' ');
        put_uint(tasks[i].start_byte);
        bios_putstr("    ");
        put_uint(tasks[i].byte_size);
        bios_putstr("    ");
        put_hex(tasks[i].vaddr_entry);
        bios_putstr("\r\n");
    }
}

// 任务4：读入一行由 \r 或 \n 结束的程序名。
//   支持退格（BS=0x08 / DEL=0x7f）；名字截断到 TASK_NAME_LEN-1；
//   若某行超长则"整个名字"判非法，清空到行尾后返回 0。
//   \r 与 \n 都立即终止；后续的 \r 或 \n（如 \r\n 的另一半）会以空行被
//   瞬间消费掉，由调用方忽略空行，不会卡住。
static int readline(char *buf, int cap)
{
    int len = 0;

    buf[0] = '\0';
    while (1)
    {
        int ch = bios_getchar();

        if (ch == -1)
            continue;   // 无输入跳过

        if (ch == '\n' || ch == '\r') {   // 双终止符
            buf[len] = '\0';
            return len;
        }

        if (ch == '\b' || ch == 0x7f) {   // 退格：BS 或 DEL
            if (len > 0) {
                len--;
                bios_putchar('\b');        // 回显擦除
                bios_putchar(' ');
                bios_putchar('\b');
            }
            continue;
        }

        if (ch < ' ' || ch > '~')         // 不可打印字符忽略
            continue;

        if (len >= cap - 1) {              // 名字超长：本行作废
            while (1) {
                while ((ch = bios_getchar()) == -1)
                    ;
                if (ch == '\n' || ch == '\r')
                    break;
            }
            buf[0] = '\0';
            return 0;
        }

        bios_putchar(ch);                  // 回显
        buf[len++] = (char)ch;
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

    // Init task information from bootblock-passed AppInfo (〃'▽'〃)
    init_task_info();

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

    // 任务4：列出可用的任务名，方便按名加载
    bios_putstr("Available tasks: ");
    for (int i = 0; i < task_num; ++i) {
        bios_putstr(tasks[i].name);
        if (i + 1 < task_num)
            bios_putstr(", ");
    }
    bios_putstr("\n\r");

    // 任务4：交互式读入程序名，在 AppInfo 里匹配、加载并执行
    //   未知名/空行/超长行都提示错误并回到输入循环，不得跑飞
    while (1)
    {
        char name[TASK_NAME_LEN];

        bios_putstr("Please input a task name: ");

        int len = readline(name, TASK_NAME_LEN);

        if (len <= 0) {
            bios_putstr("Error: invalid input!\n\r");
            continue;
        }

        // 任务5：信息显示命令（slide 25 可选项）——输入 "list" 查看应用信息
        if (strcmp(name, "list") == 0)
        {
            show_task_list();
            continue;
        }

        uint64_t entry = load_task_img(name);

        if (entry == 0)
        {
            bios_putstr("Error: unknown task or load failed!\n\r");
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
    }

    return 0;
}
