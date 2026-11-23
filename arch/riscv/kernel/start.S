/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

/* P4 启动栈（物理地址，VM 尚未开启）：
 *   · 主核用 0x50201000-0x50202000（内核镜像从 0x50202000 开始）
 *   · 从核用 0x50301000，避免两核在 boot 阶段共用同一栈
 * bootblock 的 secondary 段也用同一字面量 0x50301000。 */
.equ boot_stack_top_base, 0x50202000
.equ boot_stack_top_secondary, 0x50301000

.section ".bootkernel_entry","ax"
ENTRY(_boot)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* a0 是 bootblock 传来的核号；主核的镜像参数在 a1/a2/a3。 */
  bnez a0, boot_secondary_stack
  la sp, boot_stack_top_base
  j boot_stack_ready
boot_secondary_stack:
  la sp, boot_stack_top_secondary
boot_stack_ready:

  call boot_kernel

loop:
  wfi
  j loop

END(_boot)
