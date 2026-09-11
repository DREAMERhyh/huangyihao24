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

	// 任务4：把镜像布局参数传给内核（替代内核直接读 0x50200xxx）：
	//   布局：扇区 0 = bootblock，1..1+K-1 = 内核，1+K = AppInfo 区，
	//         之后 = 各 app 紧凑排列。K 是上面从 0x1fc 读出的内核扇区数。
	//   传参约定（head.S 会保存，见 task.h）：
	//     a0 = AppInfo 区起始扇区号 = 1 + K
	//          （t0 在循环结束时正好停在 1+K，直接复用）
	//     a1 = 用户程序数目（从本扇区偏移 0x1f8 读出）
	//   这依赖 U-Boot `virtio read 50200000 0 2` 预读前 2 扇区到 0x50200000：
	//   0x502001fc/0x1f8 才在内存里可读（见 log/task3 前置项 3 的隐藏依赖说明）。
	move  a0, t0               // a0 = 1 + K（AppInfo 起始扇区）
	li    t3, os_size_loc - 4  // t3 = 0x502001f8（app 数目字段地址）
	lh    a1, 0(t3)            // a1 = 用户程序数目

	// 任务2：跳转到内核入口，启动 UCAS-OS
	jal   kernel

	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's Dreamerhyh's bootloader...\n\r"

