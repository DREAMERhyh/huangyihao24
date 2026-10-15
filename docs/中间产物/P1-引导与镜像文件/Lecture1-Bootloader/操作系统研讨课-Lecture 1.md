# 操作系统研讨课-Lecture 1

> 源文件：`操作系统研讨课-Lecture 1.pdf`　页数：30


---

## Page 1　*(1 张图)*

Lecture 1 Bootloader
2026.09.09

---

## Page 2　*(1 张图)*

Project 1 – Bootloader
• Requirements
– Write a bootloader to start a tiny kernel,
and the kernel is able to load a few user
applications
Operating Systems
2

---

## Page 3　*(2 张图)*

Project 1 – Bootloader
• Booting procedure
Operating Systems
3

---

## Page 4　*(1 张图)*

Project 1 – Bootloader
• BIOS
– Basic Input/Output System
– Firmware used to perform hardware
initialization after power-on
– Automatically load bootloader in
bootblock
• Bootblock
– Loaded by BIOS
– Located in the first sector on hard disk
Operating Systems
4

---

## Page 5　*(1 张图)*

Project 1 – Bootloader
• Bootloader
– A small program to enable operating
system
• Load the kernel
• Switch control to the kernel
Operating Systems
5

---

## Page 6　*(2 张图)*

Project 1 – Bootloader
• Memory layout
– BBL (BIOS)
• 0x5000 0000 – 0x501F FFFF
– Bootloader will be placed
at 0x5020 0000 by BIOS
– Place your kernel at
0x5020 1000 use your
own bootloader
Operating Systems
6

---

## Page 7　*(3 张图)*

Project 1 – Bootloader
• Memory layout
– Makefile in start-code defines the
location of you kernel in memory
Operating Systems
7

---

## Page 8　*(1 张图)*

Project 1 – Bootloader
• Tiny kernel
– head.S
• Define .entry_function section
• Clear bss segment
• Prepare runtime for kernel, such as setting stack
pointer
• Call kernel main function
Operating Systems
8

---

## Page 9　*(1 张图)*

Project 1 – Bootloader
• Tiny kernel
– main.c
• Check bss segment
• Initialize jump table for calling lower-level BIOS
functions
• Print welcome message
• Initialize test applications
• Wait for input
– Echo input
– Load and execute test applications
Operating Systems
9

---

## Page 10　*(1 张图)*

Project 1 – Bootloader
• Jump table for BIOS functions
– BBL provides SBI(Supervisor Binary
Interface) to support reading SD card,
putting/getting char to/from console
– You do not need to modify them, you can
read source code for your interests
• arch/riscv/bios/common.c
• arch/riscv/include/asm/biosdef.h
Operating Systems
10

---

## Page 11　*(2 张图)*

Project 1 – Bootloader
• Kernel image
– Executable file
• bootloader
• Kernel
• User applications
Operating Systems
11

---

## Page 12　*(2 张图)*

Project 1 – Bootloader
• Create image
– Use createimage tool to generate the image
– Parse ELF files and combine their segments
Operating Systems
12

---

## Page 13　*(1 张图)*

Project 1 – Bootloader
• ELF object file format
– Executable and Linking Format (ELF)
– Object file
• Binary representation of programs
– Created by assembler and linker
Operating Systems
13

---

## Page 14　*(2 张图)*

Project 1 – Bootloader
• ELF object file format
– Executable and Linking Format (ELF)
Operating Systems
14

---

## Page 15　*(2 张图)*

Project 1 – Bootloader
• ELF object file format
– Executable and Linking Format (ELF)
Operating Systems
15

---

## Page 16　*(2 张图)*

Project 1 – Bootloader
• ELF object file format
– ELF header
Operating Systems
16

---

## Page 17　*(2 张图)*

Project 1 – Bootloader
• ELF object file format
– Program header
Operating Systems
17

---

## Page 18　*(2 张图)*

Project 1 – Bootloader
• App volume
– Segments of user
applications
– You can use fix-sized
sectors to store
applications with padding
– You can store applications
in a compacted layout
without padding
Operating Systems
18

---

## Page 19　*(2 张图)*

Project 1 – Bootloader
• App Info
– A simple index to tell
kernel
• The sector offset and
size of an application in
the image
• The memory location of
the entry point of an
application
• Application name
Operating Systems
19

---

## Page 20　*(2 张图)*

Project 1 – Bootloader
• App Info
– The app info is used
by kernel to load an
application
– You need to write app
info into image file,
and let bootloader
load it into memory
when loading kernel
Operating Systems
20

---

## Page 21　*(1 张图)*

Project 1 – Bootloader
• Step by step – Task1
– Develop a simple bootloader to only print
characters
– Help to understand how the bootloader
work and how to invoke BIOS functions
(using assembly programming)
Operating Systems
21

---

## Page 22　*(1 张图)*

Project 1 – Bootloader
• Step by step – Task 2
– Develop bootblock.S to load kernel
– Develop head.S to clear bss, set stack, and
jump to kernel main function
– Develop main.c to get console input and
echo the inputted char
– Use the given createimage to generate
kernel image
Operating Systems
22

---

## Page 23　*(1 张图)*

Project 1 – Bootloader
• Step by step – Task 3
– Develop createimage.c (write_img_info
function) to add info into kernel image file,
such as kernel size, user application number
and location
• Kernel, applications occupy the fix-sized sectors
– Develop main.c to allow user input task ID for
launching applications
– Develop loader.c to support load user
applications
– Develop crt0.S to initialize C runtime for
application
Operating Systems
23

---

## Page 24　*(1 张图)*

Project 1 – Bootloader
• Step by step – Task 4
– Do not use fix-sized sectors to store kernel
and applications. Instead, use their actual
size when storing them into image file
without padding
– Use application name to launch an
application
Operating Systems
24

---

## Page 25　*(1 张图)*

Project 1 – Bootloader
• Step by step – Task 5 (C-Core)
– Write kernel and user applications into two
separate images
• The kernel image needs to store the information
about user applications, such as the addresses, sizes
of applications
– When kernel launches, only the kernel image is
loaded
• You can provide commands to show user
application information after the kernel is loaded
– When a user application starts, you need to
load the application from the SD card at
runtime
Operating Systems
25

---

## Page 26　*(1 张图)*

Project 1 – Bootloader
• Requirement for design review
– 介绍P1的代码框架，以及和P1任务相关的代码
文件和目录
– 介绍bootBlock和内核分别如何调用BIOS提供
的功能
– 介绍内核如何加载一个应用，并启动该应用
– 介绍image文件的组成和内部布局
– 其他任何想交流讨论的问题
Operating Systems
26

---

## Page 27　*(1 张图)*

Project 1 – Bootloader
• Requirement for S/A/C-Core
Operating Systems
27
Core type
Task requirements
S-Core
Tasks 1, 2, 3
A-Core
Tasks 1, 2, 3, 4
C-Core
Tasks 1, 2, 3, 4, 5

---

## Page 28　*(1 张图)*

Tips
• Pls. first use QEMU for test. You will get
the Pynq board at P1 due
• Learn to work on Linux
– Read the outputs carefully
• Read the task handbook carefully
• Pay attention to the memory address
when you place kernel images
Operating Systems
28

---

## Page 29　*(1 张图)*

Tips
• About asking questions
– Think and try to describe your problem
clearly
• Pls. do not just show us a screenshot
• Pls. do google searching/LLM querying before
you ask questions
– Discuss with your classmate
Operating Systems
29

---

## Page 30　*(1 张图)*

Schdule
• P1 design review
– Sep. 16
• P1 due
– Sep. 23
Operating Systems
30
