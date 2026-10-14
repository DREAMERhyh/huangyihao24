#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.text
.global main

main:
	// fence on all memory and I/O
	fence
    // a0 is mhartid
	bnez a0, secondary

	// 任务1：调用 BIOS 的 bios_putstr 打印 bootblock 标志串
	//   调用约定：a0 = 字符串首地址，a7 = 功能号，然后跳转到 bios_func_entry
	//   0x50150000 处是一条 ecall + ret，ret 依赖 ra 返回，
	//   用 call(即 jal) 可让汇编器自动填好返回地址 ra
	la    a0, msg
	li    a7, BIOS_PUTSTR
	call  bios_func_entry


	// 任务2：调用 BIOS 的 bios_sd_read 把内核从 SD 卡读入内存
	//   内核占用的扇区数由 createimage 写在镜像偏移 0x1fc 处
	//   （即内存 0x502001fc，位于 bootblock 自己扇区的尾部），这里用 lh 读出
	//   镜像布局：第 0 扇区 = bootblock，第 1 扇区起 = 内核
	//   加载目标：0x50201000，每个扇区 512 字节
	li    t3, os_size_loc       // t3 = 0x502001fc（扇区数字段地址）
	lh    t2, 0(t3)             // t2 = 内核占用的扇区数
	li    t0, 1                 // t0 = 起始扇区号（SD 卡第 1 扇区）
	li    t1, kernel            // t1 = 目标内存地址

read_loop:
	beqz  t2, read_done         // 扇区数为 0 则不读（防御性判断）

	move  a0, t1                // a0 = 本次读取的目标内存地址
	li    a1, 1                 // a1 = 每次读 1 个扇区
	move  a2, t0                // a2 = 本次读取的扇区号
	li    a7, BIOS_SDREAD       // a7 = 11
	call  bios_func_entry

	addi  t1, t1, 512           // 下一个扇区的目标地址
	addi  t0, t0, 1             // 下一个扇区号
	addi  t2, t2, -1            // 剩余扇区数减一
	j     read_loop

read_done:

	// 任务5：把镜像布局参数传给内核（内核不直接读 0x50200xxx）。
	//   布局改为【字节级】：kernel.img = 512(boot) + KB(内核) + 896(AppInfo)，
	//   apps.img 紧接其后；apps 内部各 app 首尾相接、不补到扇区边界。
	//   传参约定（head.S 会保存，见 task.h）：
	//     a0 = AppInfo 起始字节偏移（镜像偏移 0x1f0，4 字节）
	//     a1 = 用户程序数目        （镜像偏移 0x1f8，2 字节）
	//     a2 = apps 区起始字节偏移（镜像偏移 0x1f4，4 字节）
	//   这依赖 U-Boot `virtio read 50200000 0 2` 预读前 2 扇区到 0x50200000：
	//   0x502001f0~0x502001ff 才在内存里可读（见 log/task3 前置项 3）。
	li    t3, os_size_loc - 12  // t3 = 0x502001f0
	lw    a0, 0(t3)             // a0 = AppInfo 起始字节偏移
	lw    a2, 4(t3)             // a2 = apps 区起始字节偏移（0x502001f4）
	lh    a1, 8(t3)             // a1 = 用户程序数目（0x502001f8）

	// 任务2：跳转到内核入口，启动 UCAS-OS
	jal   kernel

	/************************************************************/
secondary:
	/* [P3-task3] 从核启动路径（U-Boot mret 时 a0=mhartid 非 0 即到此）。
	 *   从核在此【自旋轮询】主核的"初始化完成"内存标志，读到后直接
	 *   跳内核入口 _start（head.S 用 mhartid 分出从核分支接管）。
	 *
	 *   ★ 为什么不用 IPI 唤醒（踩坑记录，试过三种死法）：
	 *   · 主核 send_ipi 经 U-Boot 发出的是 CLINT MSIP（M 态软中断），
	 *     U-Boot 的 M 态 trap 有"MSIP→SSIP 转发"（interrupts.c 的
	 *     IRQ_M_SOFT 分支），但 MSIP 要进 M 态 trap 需要 mstatus.MIE=1；
	 *   · 而从核由 U-Boot 的 enter_supervisor_mode mret 进来，MPIE 被
	 *     置 0（→MIE=0），【S 态无权改 MIE】：写 mie/mstatus 是非法
	 *     指令（实测 epc=5020007c 炸进 U-Boot 的 hang）。
	 *   即"到得了的使能位改不了（S 态），改得了的门触发不了（M 态）"。
	 *   内存标志没有特权级/使能门依赖，且 QEMU 多核内存一致，
	 *   是最可靠的核间唤醒通道。
	 *
	 *   1. 清中断环境（都是 S 态 CSR）：从核不指望任何中断，纯自旋 */
	csrw  sie, zero
	csrw  sip, zero
	csrci sstatus, 2          /* 清 SR_SIE */

	/*   2. 独立启动栈（任务书 5.1：主从核栈空间必须相互独立；
	 *   与 head.S 的 SECONDARY_BOOT_STACK 同一字面量）。
	 *   本段只用寄存器（jal 写 ra），不压栈，设上是防御性。 */
	li    sp, 0x50301000

	/*   3. 轮询唤醒标志（主核在全部初始化完成后写 1 并 fence，
	 *   见 kernel/smp/smp.c 的 wakeup_other_hart 与 smp.h 的
	 *   SMP_WAKEUP_ADDR——同一字面量）。 */
wait_for_wakeup:
	li    t0, 0x50300000
	ld    t1, 0(t0)
	beqz  t1, wait_for_wakeup
	fence

	/*   4. 跳内核入口 _start（与主核的 jal kernel 同一路径） */
	jal   kernel

	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's Dreamerhyh's bootloader...\n\r"

