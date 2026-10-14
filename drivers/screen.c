#include <screen.h>
#include <printk.h>
#include <os/string.h>
#include <os/sched.h>
#include <os/irq.h>
#include <os/kernel.h>

#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   50
#define SCREEN_LOC(x, y) ((y) * SCREEN_WIDTH + (x))

/* screen buffer */
char new_screen[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};
char old_screen[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};

/* cursor position */
static void screen_reflush_locked(void);   /* 任务5：定义见文件后部 */

static void vt100_move_cursor(int x, int y)
{
    // \033[y;xH
    printv("%c[%d;%dH", 27, y, x);
}

/* clear screen */
static void vt100_clear()
{
    // \033[2J
    printv("%c[2J", 27);
}

/* hidden cursor */
static void vt100_hidden_cursor()
{
    // \033[?25l
    printv("%c[?25l", 27);
}

/* P3 任务1：屏幕满时上滚一行（软件滚动）。
 *   原实现里 cursor_y 会加到 SCREEN_HEIGHT（=50），之后写字符时
 *   SCREEN_LOC(x, 50) 直接越过 4000 字节的缓冲——P2 的输出都停在
 *   前几行没触发，P3 的 shell/ps 会很快写到屏幕底部，必须处理。
 *   做法：缓冲内整体上移一行、末行清空；old_screen 全部置哨兵 0，
 *   迫使下一轮 screen_reflush 把 4000 格全部重发，真实终端（和
 *   emu.py 重放器）按定位重画后仍与缓冲一致，不需要清屏指令。 */
static void screen_scroll_one(void)
{
    int i;

    for (i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH; ++i)
        new_screen[i] = new_screen[i + SCREEN_WIDTH];
    for (i = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
         i < SCREEN_HEIGHT * SCREEN_WIDTH; ++i)
        new_screen[i] = ' ';

    memset(old_screen, 0, sizeof(old_screen));
}

/* write a char */
void screen_write_ch(char ch)
{
    if (ch == '\n')
    {
        current_running->cursor_x = 0;
        /* 已在最后一行再换行 → 屏幕滚动，光标停在最后一行 */
        if (current_running->cursor_y >= SCREEN_HEIGHT - 1)
            screen_scroll_one();
        else
            current_running->cursor_y++;
    }
    else if (ch == '\b' || ch == '\177')
    {
        /* P3 任务1：退格——光标左移一格并把该格擦成空格。
         *   只处理同一行内退格；行首退格（cursor_x==0）忽略——shell 的
         *   命令缓冲也不会跨行。 */
        if (current_running->cursor_x > 0)
        {
            current_running->cursor_x--;
            new_screen[SCREEN_LOC(current_running->cursor_x,
                                  current_running->cursor_y)] = ' ';
        }
    }
    else
    {
        new_screen[SCREEN_LOC(current_running->cursor_x, current_running->cursor_y)] = ch;
        if (++current_running->cursor_x >= SCREEN_WIDTH)
        {
            current_running->cursor_x = 0;
            if (current_running->cursor_y >= SCREEN_HEIGHT - 1)
                screen_scroll_one();
            else
                current_running->cursor_y++;
        }
    }
}



void init_screen(void)
{
    vt100_hidden_cursor();
    vt100_clear();
    screen_clear();
}

void screen_clear(void)
{
    int i, j;

    /* 任务5：整个"清缓冲+刷屏"在 console_lock 下（内部用 _locked
     *   变体，避免同一把自旋锁二次获取——不可重入）。 */
    FINE_LOCK(&console_lock);

	vt100_clear();
    for (i = 0; i < SCREEN_HEIGHT; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            new_screen[SCREEN_LOC(j, i)] = ' ';
			old_screen[SCREEN_LOC(j, i)] = ' ';
        }
    }
    current_running->cursor_x = 0;
    current_running->cursor_y = 0;
    screen_reflush_locked();

    FINE_UNLOCK(&console_lock);
}

void screen_move_cursor(int x, int y)
{
    FINE_LOCK(&console_lock);

    if (x >= SCREEN_WIDTH)
        x = SCREEN_WIDTH - 1;
    else if (x < 0)
        x = 0;
    if (y >= SCREEN_HEIGHT)
        y = SCREEN_HEIGHT - 1;
    else if (y < 0)
        y = 0;
    current_running->cursor_x = x;
    current_running->cursor_y = y;
    vt100_move_cursor(x + 1, y + 1);

    FINE_UNLOCK(&console_lock);
}



void screen_write(char *buff)
{
    int i = 0;
    int l = strlen(buff);

    /* 任务5：一次锁住整个字符串的写入（fly 刷屏与内核打印互斥） */
    FINE_LOCK(&console_lock);

    for (i = 0; i < l; i++)
    {
        screen_write_ch(buff[i]);
    }

    FINE_UNLOCK(&console_lock);
}

/*
 * This function is used to print the serial port when the clock
 * interrupt is triggered. However, we need to pay attention to
 * the fact that in order to speed up printing, we only refresh
 * the characters that have been modified since this time.
 * 任务5：拆出 _locked 变体——screen_clear 在自己的临界段里也要刷屏，
 *   直接调完整版会重复拿 console_lock（自旋锁不可重入）。
 */
static void screen_reflush_locked(void)
{
    int i, j;

    /* here to reflush screen buffer to serial port */
    for (i = 0; i < SCREEN_HEIGHT; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            /* We only print the data of the modified location. */
            if (new_screen[SCREEN_LOC(j, i)] != old_screen[SCREEN_LOC(j, i)])
            {
                vt100_move_cursor(j + 1, i + 1);
                bios_putchar(new_screen[SCREEN_LOC(j, i)]);
                old_screen[SCREEN_LOC(j, i)] = new_screen[SCREEN_LOC(j, i)];
            }
        }
    }

    /* recover cursor position */
    vt100_move_cursor(current_running->cursor_x + 1, current_running->cursor_y + 1);
}

void screen_reflush(void)
{
    FINE_LOCK(&console_lock);
    screen_reflush_locked();
    FINE_UNLOCK(&console_lock);
}
