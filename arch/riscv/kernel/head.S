/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0x50500000

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* 任务2：清零 BSS 段
   *   起止地址取自 riscv.lds 定义的符号 __bss_start 与 __BSS_END__，
   *   由链接器按本内核实际的段布局赋值，无需硬编码。
   *   镜像里只有已初始化的数据（PT_LOAD 的 p_filesz 部分），
   *   BSS 不在镜像中，必须在这里清零，否则 main 里的 bss_check 会失败。 */
  la    t0, __bss_start
  la    t1, __BSS_END__
clear_loop:
  bge   t0, t1, clear_done
  sd    zero, 0(t0)
  addi  t0, t0, 8
  j     clear_loop
clear_done:

  /* 任务2：建立 C 运行环境
   *   栈顶 0x50500000（KERNEL_STACK），向下生长；
   *   然后跳转到 init/main.c 的 main 函数。 */
  li    sp, KERNEL_STACK
  call  main


loop:
  wfi
  j loop

END(_start)
