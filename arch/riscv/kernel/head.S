/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0xffffffc050500000

/* P3 任务3：从核的两张固定栈（避开所有已用内存区：
 *   0x50000000-0x50200000 U-Boot/bios、0x50200000-0x50228800 内核镜像、
 *   0x504Fxxxx-0x5050xxxx 主核内核栈/mm 动态区、0x52... app 窗口）
 *   · BootStack  —— head.S 从核分支 → main 从核路径 → idle wfi 循环
 *   · TrapStack  —— 从核的陷阱栈（sscratch），与主核"陷阱栈"对应
 *   bootblock.S 的 secondary 段用同一字面量 0x50301000 作为启动栈。 */
#define SECONDARY_BOOT_STACK	0xffffffc050301000
#define SECONDARY_TRAP_STACK	0xffffffc050302000

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* P3 任务3：从核（mhartid != 0）走独立入口。
   *   从核是被主核 IPI 唤醒后 trap 到这里的，此刻：
   *     ① 内核 BSS 已由主核清过并正在使用，从核【绝不能】再清；
   *     ② a0~a2 是 trap 时的寄存器残值，不是 bootblock 的引导参数，
   *        不能写进 boot_appinfo_off 等全局量。
   *   从核只需建立自己的 C 运行环境（独立启动栈）后进 main，
   *   由 main 的从核分支做本核设置。 */
  csrr  t0, CSR_MHARTID
  bnez  t0, SecondaryStart

  /* 任务2：清零 BSS 段
   *   起止地址取自 riscv.lds 定义的符号 __bss_start 与 __BSS_END__，
   *   由链接器按本内核实际的段布局赋值，无需硬编码。
   *   镜像里只有已初始化的数据（PT_LOAD 的 p_filesz 部分），
   *   BSS 不在镜像中，必须在这里清零，否则全局变量会读到垃圾值。 */
  la    t0, __bss_start
  la    t1, __BSS_END__
clear_loop:
  bge   t0, t1, clear_done
  sd    zero, 0(t0)
  addi  t0, t0, 8
  j     clear_loop
clear_done:

  /* 任务5：保存 bootblock 传来的引导参数（全部是【字节】偏移）
   *   a0 = AppInfo 起始字节偏移，a1 = 用户程序数目，a2 = apps 区起始字节偏移
   *   必须放在清完 BSS 之后：这三个全局量在 .bss 里，先清再存。
   *   此后 main() 从全局量取值，内核不再直接读 0x50200xxx。 */
  la    t2, boot_appinfo_off
  sd    a0, 0(t2)
  la    t3, boot_appnum
  sd    a1, 0(t3)
  la    t4, boot_apps_base_off
  sd    a2, 0(t4)

  /* 任务2：建立 C 运行环境
   *   栈顶 0x50500000（KERNEL_STACK），向下生长；
   *   然后跳转到 init/main.c 的 main 函数。
   *   P3 任务5（路线A）：a0 = 本核 mhartid，作为 main(boot_cpu) 的参数。
   *   这是【启动期唯一一次】读核号（由上面的 bnez 已保证此路径 = 0，
   *   这里重读一次只为把"我是哪个核"显式传给 main）；此后全内核核号
   *   都从 current_running->running_cpu 取，不再有运行时 csrr mhartid
   *   （细粒度版双核高频 syscall 下它偶发被判非法指令 → 崩溃）。 */
  csrr  a0, CSR_MHARTID
  li    sp, KERNEL_STACK
  call  main


loop:
  wfi
  j loop

/* P3 任务3：从核入口——跳过 BSS/引导参数，用独立栈进 main。
 *   此时处于"被 IPI trap 进 S 态"的上下文：sepc=wfi 的 PC、scause=软中断，
 *   但 main 的从核分支不返回（进 idle 循环），不依赖任何 trap 返回路径。 */
SecondaryStart:
  /* 消费掉唤醒用的 S 态软中断：U-Boot 转发过来的 mip.SSIP 在 trap 进入后
   * 仍是挂起位（硬件不自动清）。现在不清，等 main 从核路径
   * enable_preempt() 重开 SSIE 时会触发一次"幽灵 trap"。 */
  csrw  CSR_SIP, zero
  /* P3 任务5（路线A）：同上——t0 还是入口处的 mhartid，
   *   作为 main(boot_cpu) 的参数传下去（启动期读一次，此后不读）。 */
  mv    a0, t0
  li    sp, SECONDARY_BOOT_STACK
  call  main
1:
  wfi
  j 1b

END(_start)
