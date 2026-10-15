# 操作系统研讨课-Lecture 0

> 源文件：`操作系统研讨课-Lecture 0.pdf`　页数：24


---

## Page 1　*(1 张图)*

操作系统研讨课
Course: B0911011H
陈明宇
蒋德钧
Fall Term 2026-2027

---

## Page 2　*(1 张图)*

Lecture 0 Introduction
2026.09.09

---

## Page 3　*(1 张图)*

Lecture 0: Introduction
• Overview
– Course introduction
– Course administration
Operating Systems
3

---

## Page 4　*(1 张图)*

Course objectives
• Practice what you learn in OS theory
course
• Obtain capabilities of system
programming, e.g. hardware-software
co-designing
• Establish full stack view
Operating Systems
4

---

## Page 5　*(1 张图)*

Course contents
• Build an operating system kernel
Operating Systems
5
Bootloader
timer
interrupt
Process mgt.
(PCB mgt)
VM mgt. (TLB
handler)
File system(disk
mgt., file/dir op)
synchronizati
on primitive
syscalls
NIC driver
shell
Test
applications
1
2
3
4
5
6
User
space
Kernel
space

---

## Page 6　*(1 张图)*

Course contents
• Build an operating system kernel
– Bootloader
– Kernel supporting multitasking
– Process communication and management,
supporting dual cores
– Virtual memory management
– Device driver
– File system
Operating Systems
6

---

## Page 7　*(1 张图)*

Course administration
• Classrooms
– 学园二204(机房)
• Schedule
– 秋、春季学期都开课
– 作业量相同，题目会略有变化
– 有压力的同学可以选春季课程
Operating Systems
7

---

## Page 8　*(2 张图)*

Course administration
• Schedule（秋季）
– may adjust according to project progress
Operating Systems
8

---

## Page 9　*(2 张图)*

Course administration
• Schedule（秋季）
– may re-schedule according to project progress
Operating Systems
9

---

## Page 10　*(1 张图)*

Course administration
• 课程召集人
– 陈明宇：cmy@ict.ac.cn
• 主讲老师（秋季）
– 蒋德钧：jiangdejun@ict.ac.cn
• 助教
– 卢天越：lutianyue@ict.ac.cn
– 许晋升：xujinsheng22@mails.ucas.ac.cn (local TA)
– 王传皓：wangchuanhao23@mails.ucas.ac.cn
– 潘宇轩：2252393382@qq.com (local TA)
– 朱徐塬：zhuxuyuan22@mails.ucas.ac.cn (local TA)
– 张扬：zhangyang239@mails.ucas.ac.cn (local TA)
– 祝立言：zhuliyan23@mails.ucas.ac.cn (local TA)
• Office hour
– Make appointment if any necessary
Operating Systems
10

---

## Page 11　*(1 张图)*

Development environment
• Software
– Providing a virtual machine with VirtualBox
– Ubuntu 24.04
– Pre-installed RISC-V compiling environment
Operating Systems
11

---

## Page 12　*(1 张图)*

Development environment
• Hardware – RISC-V core board
– CPU core
• Nutshell dual-core (25MHz)
– 512MB RAM
– 8/16/32GB SD card
– 1Gb NIC port
Operating Systems
12

---

## Page 13　*(2 张图)*

Development environment
• Pynq board with Nutshell core
Operating Systems
13

---

## Page 14　*(1 张图)*

Grading
• Grading per project
– Each project has 100 points
– Design review
• If you do not take design review, at most you
only get 60 points for a project
• Design review with different TAs
– Code submission and running
• S-Core、A-Core、C-Core
– On-site code checking
• Q&A
• May ask you to modify your code
Operating Systems
14

---

## Page 15　*(1 张图)*

Design review
• Grouping with different TAs
– One student per group
– Grouping for each project
– Group students randomly
– Individual submission and presentation
Operating Systems
15

---

## Page 16　*(1 张图)*

Code submission
• Source code + README
– Please commit your code to gitlab for on-
site check
• A document recording lessons learned
from debugging (no page limits)
• Submission site: course web site
– http://8.152.206.202/
– Make sure no compilation errors
– Use git tag to identify your submission
Operating Systems
16

---

## Page 17　*(1 张图)*

Grading
• Core category for each project
– S-Core
• Simple core including simplest functionalities for an
OS to run
• Once you finish it, you will get 0~70
– A-Core
• A basic core including basic functionalities for an OS
to run
• Once you finish it, you will get 71~85
– C-Core
• Complex core with advanced features of an OS
• Once you finish it, you will get 86 ~ 100
Operating Systems
17

---

## Page 18　*(1 张图)*

Grading
• Final grading
– Final grades = σ𝑖=1
6
𝑃𝑟𝑜𝑗𝑒𝑐𝑡 𝑖 ∗𝑊𝑒𝑖𝑔ℎ𝑡 𝑖
– Weights for each project
– Once you have unfinished S-Core, you will
at most get 65 points for the whole course
Operating Systems
18
P1
P2
P3
P4
P5
P6
10%
20%
20%
20%
10%
20%

---

## Page 19　*(1 张图)*

Lecture 0: Introduction
• Final grading (how to get 100 points)
– You will get 100 points as the final grade if
you
• Finish all C-Cores for each project when you
have a working operating system with all
advanced features
• Another possible option: finish big bonus on
time (depends on the course progress)
Operating Systems
19

---

## Page 20　*(1 张图)*

Grading
• Please submit your project on time, but
– No point deduction for delayed S Core
– One week delay for A Core is allowed. In such
case, you at most get 80 for A Core
– No delay is allowed for C Core
– If necessary, we will re-schedule the deadline
Operating Systems
20

---

## Page 21　*(1 张图)*

Grading
• Copying others’code is ABSOLUTELY
prohibited
– NO points will be given
• You can ask AI for help, but you MUST
know every line of your code
Operating Systems
21

---

## Page 22　*(1 张图)*

Lecture 0: Introduction
• Daily Q&A
– WeChat
Operating Systems
22

---

## Page 23　*(1 张图)*

Lecture 0: Introduction
• Suggestions on accomplish this course
– Think and design before coding
– Start early as possible as you can, DON’T be
deadline driven
– Prepare the first version of your design
before design review, and discuss with TAs
– Learn to use tools, e.g. gdb + qemu
– Encourage incremental git commit
– XV6 may help
Operating Systems
23

---

## Page 24　*(1 张图)*

Lecture 0: Introduction
• Any question?
Operating Systems
24
