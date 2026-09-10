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


	// TODO: [p1-task2] call BIOS to read kernel in SD card


	// TODO: [p1-task4] load task-related arguments and pass them to kernel


	// TODO: [p1-task2] jump to kernel to start UCAS-OS

	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's Dreamerhyh's bootloader...\n\r"

