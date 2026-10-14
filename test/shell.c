/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* shell 的显示区：屏幕下半部分（上半部分留给任务输出） */
#define SHELL_BEGIN  20
#define CMD_BUF_LEN  128
#define ARGV_MAX     10

/* ---------------------------------------------------------------------
 * 读一行命令。
 *   · 阻塞轮询 sys_getchar（串口在 P3 里由 shell 独占消费）；
 *   · 读到可打印字符就回显、退格（BS/DEL）就擦除；
 *   · 回车（\r 或 \n）结束，返回长度（空行返回 0）。
 *   · 读不到字符时 sys_yield 让出 CPU——shell 空转不该白占时间片，
 *     把 CPU 让给正在跑的任务；下一次被调度回来继续轮询。
 * --------------------------------------------------------------------- */
static int read_line(char *buf, int cap)
{
    int len = 0;

    while (1)
    {
        int ch = sys_getchar();

        if (ch < 0)                 /* 无输入 */
        {
            sys_yield();
            continue;
        }

        if (ch == '\r' || ch == '\n')
        {
            printf("\n");           /* 回车回显：换到下一行 */
            buf[len] = '\0';
            return len;
        }

        if (ch == '\b' || ch == '\177')
        {
            if (len > 0)
            {
                len--;
                printf("\b");       /* 驱动负责"左移一格并擦除" */
            }
            continue;
        }

        if (ch == 0x03)             /* Ctrl+C：目前不做中断处理，忽略 */
            continue;

        if (ch < ' ' || ch > '~')   /* 其它不可打印字符忽略 */
            continue;

        if (len >= cap - 1)         /* 缓冲满：忽略多余字符 */
            continue;

        buf[len++] = (char)ch;
        printf("%c", ch);           /* 回显 */
    }
}

/* ---------------------------------------------------------------------
 * 按空格/制表符把一个命令行切成参数（原地写 '\0'）。
 * 返回参数个数。 */
static int split(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0')
    {
        while (*p == ' ' || *p == '\t')
            *p++ = '\0';
        if (*p == '\0')
            break;
        if (argc >= max)
            break;
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
    }

    return argc;
}

/* ---------------------------------------------------------------------
 * exec <name> [args...] [&]
 *   按名启动一个新任务；启动参数原样透传（argv[0] 就是程序名，
 *   与任务里 main(argc, argv) 的约定一致）。
 *   默认（不带 &）等它跑完（sys_waitpid）；带 & 则后台运行。 */
static void cmd_exec(int argc, char **argv)
{
    int background = 0;

    if (argc < 2)
    {
        printf("Usage: exec <task> [args...] [&]\n");
        return;
    }

    if (strcmp(argv[argc - 1], "&") == 0)
    {
        background = 1;
        argc--;
    }

    pid_t pid = sys_exec(argv[1], argc - 1, &argv[1]);
    if (pid == 0)
    {
        printf("Error: cannot load task \"%s\"\n", argv[1]);
        return;
    }

    printf("Info: execute %s successfully, pid = %d ...\n", argv[1], pid);

    if (!background)
        sys_waitpid(pid);
}

/* ---------------------------------------------------------------------
 * kill <pid>：终止指定进程 */
static void cmd_kill(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: kill <pid>\n");
        return;
    }

    int pid = atoi(argv[1]);
    if (sys_kill(pid) == 0)
        printf("Error: cannot find process %d\n", pid);
}

/* ---------------------------------------------------------------------
 * parse_mask：解析核亲和掩码，形如 0x1 / 3（十六进制或十进制） */
static uint64_t parse_mask(const char *s)
{
    uint64_t v = 0;
    int hex = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));

    if (hex)
        s += 2;
    for (; *s != '\0'; ++s)
    {
        int d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (hex && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else if (hex && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else
            return 0;                      /* 有非法字符：视作无效掩码 */
        v = v * (hex ? 16 : 10) + d;
    }
    return v;
}

/* ---------------------------------------------------------------------
 * taskset <mask> <task> [args...] [&]   以指定核亲和掩码启动任务
 * taskset -p <mask> <pid>               修改已存在进程的掩码
 *   mask 的 bit i = 允许在第 i 号核上运行；掩码 0 视为非法。
 *   不带 -p 时行为与 exec 相同（参数透传、& 后台），只是给新任务
 *   指定掩码——affinity 的子任务靠 exec 继承拿到同一个掩码。 */
static void cmd_taskset(int argc, char **argv)
{
    /* taskset -p <mask> <pid> */
    if (argc >= 4 && strcmp(argv[1], "-p") == 0)
    {
        uint64_t mask = parse_mask(argv[2]);
        int pid = atoi(argv[3]);

        if (mask == 0 || sys_set_affinity(pid, mask) == 0)
            printf("Error: cannot set affinity of process %d\n", pid);
        else
            printf("taskset -p %s %d.\n", argv[2], pid);
        return;
    }

    /* taskset <mask> <task> [args...] [&] */
    if (argc < 3)
    {
        printf("Usage: taskset <mask> <task> [args...] [&]\n");
        return;
    }

    uint64_t mask = parse_mask(argv[1]);
    if (mask == 0)
    {
        printf("Error: invalid mask \"%s\"\n", argv[1]);
        return;
    }

    int end = argc;
    int background = 0;
    if (strcmp(argv[argc - 1], "&") == 0)
    {
        background = 1;
        end--;
    }

    pid_t pid = sys_exec_mask(argv[2], end - 2, &argv[2], mask);
    if (pid == 0)
    {
        printf("Error: cannot load task \"%s\"\n", argv[2]);
        return;
    }

    printf("taskset %s %s.\n", argv[1], argv[2]);
    /* 官图 P3-8 的时序：taskset 启动后 shell 立即回来，不等子进程
     *   （affinity 还在跑时用户就接着输入 taskset -p / ps 了）。
     *   所以这里不带 & 也不 waitpid；& 只作兼容被吃掉。 */
    (void)background;
}

/* ---------------------------------------------------------------------
 * 命令分发 */
static void run_command(int argc, char **argv)
{
    if (strcmp(argv[0], "ps") == 0)
    {
        sys_ps();
    }
    else if (strcmp(argv[0], "clear") == 0)
    {
        sys_clear();
        sys_move_cursor(0, SHELL_BEGIN);   /* 清屏后回到 shell 区顶部 */
    }
    else if (strcmp(argv[0], "exec") == 0)
    {
        cmd_exec(argc, argv);
    }
    else if (strcmp(argv[0], "kill") == 0)
    {
        cmd_kill(argc, argv);
    }
    else if (strcmp(argv[0], "taskset") == 0)
    {
        cmd_taskset(argc, argv);
    }
    else if (strcmp(argv[0], "help") == 0)
    {
        printf("Commands: ps | clear | exec <task> [args...] [&] | kill <pid> | taskset ... | help\n");
    }
    else
    {
        printf("Unknown command: %s\n", argv[0]);
    }
}

int main(void)
{
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    printf("> root@UCAS_OS: ");

    char buf[CMD_BUF_LEN];

    while (1)
    {
        if (read_line(buf, CMD_BUF_LEN) > 0)
        {
            char *argv[ARGV_MAX];
            int argc = split(buf, argv, ARGV_MAX);

            if (argc > 0)
                run_command(argc, argv);
        }

        printf("> root@UCAS_OS: ");
    }

    return 0;
}
