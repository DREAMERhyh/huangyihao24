/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                  Timer
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#ifndef INCLUDE_TIME_H_
#define INCLUDE_TIME_H_

#include <type.h>

/* 时钟中断间隔：10ms（time_base / 100）。
 *   原值 150000 只对 QEMU 的 10MHz timebase 成立（=15ms）；
 *   开发板 FDT 的 timebase 与 QEMU 不同（bios_read_fdt(TIMEBASE)），
 *   同样的 150000 ticks 在板上可能只有零点几毫秒——时间片再被
 *   SLICE_LEN 切成 1/4，调度器（尤其细粒度锁版）跑不过来，
 *   表现为 shell 起不来。
 *   改成按 time_base 自适应的 10ms（往届 cauphenuny 的 universal 方案）。
 *   ★ time_base 在 main() 里从 FDT 读出（init/main.c:220），本宏的所有
 *   使用点都在那之后：主核 main 的 set_timer（247 行）、从核被唤醒后的
 *   main（192 行）、do_scheduler 的 SLICE_LEN（运行时）。 */
#define TIMER_INTERVAL (time_base / 100)

extern uint64_t time_base;
extern uint64_t time_elapsed;

extern uint64_t get_timer(void);
extern uint64_t get_ticks(void);
extern uint64_t get_time_base(void);
extern void latency(uint64_t time);

extern void check_sleeping(void);

#endif
