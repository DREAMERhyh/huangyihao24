# 操作系统研讨课-Project 0

> 源文件：`操作系统研讨课-Project 0.pdf`　页数：37


---

## Page 1　*(1 张图)*

Project 0 课程环境和预备知识
2026.9.2
卢天越

---

## Page 2　*(1 张图)*

今日课程内容
• 预备知识 & 课程环境搭建
• 1、虚拟机环境搭建
• 2、git环境介绍
• 3、RISC-V汇编介绍
• 4、QEMU模拟器和gdb介绍
• 5、预备课作业：9月9号课上检查
Operating Systems
2

---

## Page 3　*(1 张图)*

今日课程内容
• 预备知识 & 课程环境搭建
• 1、虚拟机环境搭建
• 2、git环境介绍
• 3、RISC-V汇编介绍
• 4、QEMU模拟器和gdb介绍
• 5、预备课作业：9月9号课上检查
Operating Systems
3

---

## Page 4　*(1 张图)*

课程环境搭建
• 请来领sd卡和读卡器
• sd卡和读卡器插在电脑上应能看到以下内容
– 五个分区，两个是windows可以读出来的分区，
一个分区包含一个boot.bin文件给开发板用，一个
分区里面是一些压缩包和安装文件
– 注意sd卡插入读卡器时的正反
• 后续实验中也会用到sd卡和读卡器，压缩包和
安装文件之外的其他分区的内容不要动
• 本ppt稍后会上传至国科大在线
Operating Systems
4

---

## Page 5　*(1 张图)*

虚拟机环境安装和使用
• VirtualBox
– 拷贝sd卡中的安装程序并安装
– 已安装的同学不必重复安装
• 安装VirtualBox_extension
– 之前安装过VirtualBox的同学需要自己去官网找一下对应版
本的extension
• 载入虚拟机镜像
– 选择UCAS_OS_2025.ova文件，导入虚拟电脑，导入文件
• 启动虚拟机
– 登录用户stu, 密码123456
– Linux系统输入密码是不会显示的
• 看到命令提示符，可以输入，则成功
Operating Systems
5

---

## Page 6　*(1 张图)*

ova文件的验证
• certutil -hashfile “XXXX.ova” SHA1
• 看结果是否是
5f779bb318acab23406040195b62fb31
2729cea9
Operating Systems
6

---

## Page 7　*(1 张图)*

虚拟机环境相关介绍
• Linux系统
– 请大家自己熟悉一下常用的Linux命令和软件，例如ls,
cd, cp, vim等
• 虚拟机中已包含QEMU模拟器和RISC-V的编译环境
• Windows系统可以选择在虚拟机启动的情况下用
Xshell、MobaXTerm、vscode等软件ssh连接
– sd卡中拷贝有MobaXTerm的安装包
– 新建链接：IP：127.0.0.1，端口：2333
– 解决字体、多窗口等问题
• （不推荐）如果有自带Linux系统的同学也可以用
自带的系统，但是需要安装交叉编译环境并拷贝
QEMU模拟器。
Operating Systems
7

---

## Page 8　*(2 张图)*

Operating Systems
8
在Terminal这里可以使用各种
Linux命令
能看到虚拟机上的
文件夹

---

## Page 9　*(1 张图)*

今日课程内容
• 预备知识 & 课程环境搭建
• 1、虚拟机环境搭建
• 2、git环境介绍
• 3、RISC-V汇编介绍
• 4、QEMU模拟器和gdb介绍
• 5、预备课作业：9月9号课上检查
Operating Systems
9

---

## Page 10　*(1 张图)*

Gitlab环境
• 使用gitlab云环境来管理同学们的提交代码，
同学们也可以用这个环境来管理和备份自
己的代码版本
• 与组成原理课相同，gitlab链接为：
– http://8.152.206.202/
– 没有账号的同学请将你的常用邮箱号发给我
（lutianyue@ict.ac.cn），用于请管理员注册
新的账号
Operating Systems
10

---

## Page 11　*(2 张图)*

Gitlab仓库
• 有账号的同学已经有了仓库，内容是本次
预备课作业的start-code
– 后续课程每个project也都会提供一些start-
code作为提示和引导
Operating Systems
11

---

## Page 12　*(2 张图)*

Git管理
• Git本地配置
– 配置 Git 的用户名和邮箱
– git config --global user.name "<Your Name>"
– git config --global user.email "<Your Email>"
• Git clone到本地
– git clone <URL>
• 代码提交与推送
– 1. 提交至暂存区
• git add <文件名或路径>
– 2. 提交修改(本地仓库版本更改)
• git commit -m "commit message"
– 3. 推送至GitLab远程仓库
• git push
Operating Systems
12

---

## Page 13　*(1 张图)*

后续git管理
• 每个project一个分支，并且每个project都
有一个初始代码，成为start-code
– 每次建好分支之后，通过git merge的方式获
取新的start-code（后续到P2之前会详细说明）
• 其他常用git命令
– git checkout切换版本
– git log查看log（git commit后面写的文本）
Operating Systems
13

---

## Page 14　*(1 张图)*

P1 start-code的获取
• 每次project前，助教老师会把该project的start-
code上传，同学们通过git获取
– git remote add upstream
http://8.152.206.202/ucas-os/os-202602-code.git
• 添加远程仓库
– git checkout -b Project1
• 建立本地新分支
– git pull upstream Project1
• 拉取代码
– git push --set-upstream origin Project1
• 推送自己的新分支
• 后续Project会通过git merge的方式合并新老代码
Operating Systems
14

---

## Page 15　*(2 张图)*

Operating Systems
15
在gitlab（http://8.152.206.202/）的UCAS-OS-202602 group下面找到你的
project，里面有几个初始文件。有的话可以尝试git到本地虚拟机里面。
如果没有gitlab账号或者看不到仓库，请联系邮箱lutianyue@ict.ac.cn（写明你
的账号邮箱），稍后集中联系管理员处理。

---

## Page 16　*(1 张图)*

今日课程内容
• 预备知识 & 课程环境搭建
• 1、虚拟机环境搭建
• 2、git环境介绍
• 3、RISC-V汇编介绍
• 4、QEMU模拟器和gdb介绍
• 5、预备课作业：9月9号课上检查
Operating Systems
16

---

## Page 17　*(1 张图)*

RISC-V汇编简介
• RISC-V是一个基于精简指令集（RISC）原则
的开源指令集架构（ISA）
– 共有 32 个通用寄存器
– 指令只有六种格式，并且所有的指令都是 32 位长
• 用于寄存器-寄存器操作的 R 型指令
• 用于短立即数和访存 load 操作的 I 型指令
• 用于访存 store 操作的 S 型指令
• 用于条件跳转操作的 B 型指令
• 用于长立即数的 U 型指令
• 用于无条件跳转的 J 型指令
– Load/store 类指令负责将数据在内存和寄存器之
间传输，其余指令都是寄存器之间的操作
Operating Systems
17

---

## Page 18　*(1 张图)*

RISC-V通用寄存器
Operating Systems
18
寄存器编号 寄存器名
用途
x0
zero
常数 0
x1
ra
子程序调用返回地
址
x2
sp
栈指针
x3
gp
全局指针
x4
tp
线程指针
x5
t0
临时寄存器
x6-x7
t1-t2
临时寄存器
x8
s0/fp
存储寄存器/
帧寄存器
x9
s1
存储寄存器
x10-x11
a0-a1
函数入参/返回值
x12-x17
a2-a7
函数入参
x18-x27
s2-s11
存储寄存器
x28-x31
t3-t6
临时寄存器
f0-f7
ft0-ft7
FP 临时寄存器
f8-f9
fs0-fs1
FP 存储寄存器
f10-f11
fa0-fa1
FP 函数入参/返回
值
f12-f17
fa2-fa7
FP 函数入参
f18-f27
fs2-fs11
FP 存储寄存器
f28-f31
ft8-ft11
FP 临时寄存器

---

## Page 19　*(1 张图)*

RISC-V基本指令
• 运算指令:
– add a0,a1,a2// a0=a1+a2
• 加 (Add)，R 型指令，寄存器里的数相加
– addi a0, a1, 0x10 // a0=a1+0x10
• 加立即数 (Add Immediate)，I 型指令，立即数与寄存
器里的数相加
– sll a0, a1, a2 // a0=a1<<a2
• 逻辑左移 (Shift Left Logical)， R 型指令，空出的位置
填0
– slt a0, a1, a2 // a0=a1<a2?1:0
• 小于则置位(Set if Less Than), R 型指令
– srl/and/xor/or ...
Operating Systems
19

---

## Page 20　*(1 张图)*

RISC-V基本指令
• 访存指令:
– lw a0, 0(s0) // a0=s0[0], load 32bit
• 字加载 (Load Word)，I 型指令
– ld a0, 0(s0) // a0=s0[0], load 64bit
• 双字加载 (Load Doubleword)，I 型指令
– sw a0, 0(s0) // s0[0]=a0, store 32bit
• 存字 (Store Word)，S 型指令
– sd a0, 0(s0) // s0[0]=a0, store 64bit
• 存双字 (Store Doubleword)，S 型指令
– la a0, SYMBOL // 伪指令，load一个符号变量到
地址
– lh/lb/lui/sh/ ...
Operating Systems
20

---

## Page 21　*(1 张图)*

从C语言到汇编
Operating Systems
21
• C代码
if(a0 == a1){
// do somthing
}else{
// do other
}
ØRISC-V汇编
bne a0, a1, do_else
// do something
j done
do_else:
// do other
done:
注：bne, 不相等时分支 (Branch if Not Equal)

---

## Page 22　*(1 张图)*

从C语言到汇编
Operating Systems
22
• C代码
for(i=0; i<n; i++){
// do something
}
ØRISC-V汇编
li s0, 0 // i = 0
li s1, n // n
// if (i >= n) goto done
bge s0, s1, done
loop:
// do something
addi s0, s0, 1 // i++
// if (i < n) goto loop
blt s0, s1, loop
done:

---

## Page 23　*(1 张图)*

函数调用
• RISC处理器的寄存器分为临时寄存器和存储寄
存器
• 一般约定:
– 存储寄存器在函数调用前后，值不会发生变化
– 临时寄存器在函数调用后，有可能值被改变
• 因此，为了保证程序的正常执行
– 函数调用者需要在调用函数前保存自己需要的临时
寄存器
– 被调用者需要保证存储寄存器的值在调用前后不发
生变化
• 方法：将寄存器的值临时保存到栈上
Operating Systems
23

---

## Page 24　*(2 张图)*

函数调用前后的保存与恢复
Operating Systems
24
caller:
addi sp, sp, -12 // 在栈上预留空间
sd ra, 4(sp) // 保存子程序调用返回地址
// do something
sw t0, 0(sp) // 保存临时寄存器上的数值
call callee  // 调用子程序callee
lw t0, 0(sp) // 恢复临时寄存器上的数值
callee:
addi sp, sp, -16 // 在栈上预留空间
sd ra, 0(sp) // 保存子程序调用返回地址
sd s0, 8(sp) // 保存存储寄存器上的数值
// do something
ld s0, 8(sp) // 恢复存储寄存器上的数值
ld ra, 0(sp) // 加载子程序调用返回地址
addi sp, sp, 16 // 恢复栈指针
ret // 从子程序返回

---

## Page 25　*(1 张图)*

汇编语言其他注意点
• 以P1的start-code为例
– .equ os_size_loc, 0x502001fc
• 可以使用这样的方式实现常量名，类似#define
– msg: .string “It‘s a bootloader...\n\r”
• 这样的方式实现了字符串，常量名为msg
– stop:
j stop
nop
实现了结尾的死循环，避免程序跑飞，相当于while(1)
Operating Systems
25

---

## Page 26　*(1 张图)*

预备课作业
• 1、用RISC-V汇编编写两个测试程序：
– （1）1到50的叠加，叠加的结果放到指定的内存地址
上（实现时使用常量宏存这个地址，通过la指令把叠加
结果写到这个地址上）
– （2）判断1到200的每个数字是否为质数，要求用函数
调用实现，函数参数为数字，返回值为是否是质数。
– 这两个测试写完后用make命令编译（需要按现有提供
的Makefile脚本填写源代码文件路径，测试程序的名
字改为同学自己的名字）
• 2、在Linux命令行用riscv64***objdump命令打
印反汇编结果，用cat命令打印源代码内容，用ls
命令列出文件夹内容
Operating Systems
26

---

## Page 27　*(1 张图)*

今日课程内容
• 预备知识 & 课程环境搭建
• 1、虚拟机环境搭建
• 2、git环境介绍
• 3、RISC-V汇编介绍
• 4、QEMU模拟器和gdb介绍
• 5、预备课作业：9月9号课上检查
Operating Systems
27

---

## Page 28　*(1 张图)*

QEMU模拟器
• QEMU模拟器：通过软件模拟一个硬件平台
– 硬件寄存器->软件变量，硬件运算->软件运算
– 将同学们写的操作系统运行在模拟器上，与运行在
开发板上功能基本相同
• 优点：
– 1、硬件平台的输出有限，使用软件模拟加快调试
速度
• 可以接入gdb：单步调试，查看系统状态
– 2、硬件平台无法修改，通过在软件模拟器上修改
的方式来做研究
Operating Systems
28

---

## Page 29　*(1 张图)*

使用QEMU
• 启动QEMU模拟器
– 使用提供的启动脚本
– 启动脚本路径：
• 代码框架中会包含Makefile，在该文件所在路径下
make run命令运行
• QEMU退出：
– Ctrl+a x退出（先按ctrl+a，然后按x）
– 或者Ctrl+a c呼出QEMU命令行，用quit命令
Operating Systems
29

---

## Page 30　*(1 张图)*

使用gdb连接QEMU
• 启动gdb，连接QEMU
– Makefile所在路径，make debug运行QEMU，
另外一个窗口make gdb启动gdb
• 不连接gdb的话用make run即可
Operating Systems
30

---

## Page 31　*(1 张图)*

gdb调试技巧
• 在gdb界面中可以使用以下命令：
– 设置断点：b，后面跟上内存地址或代码中的行数，例：
b *0xa0800000
– 继续运行：c
– 单步运行（单条汇编指令）：si
– 查看当前寄存器内容：i r
– 查看内存内容：x，命令格式：x/nfu [addr]
– n是内存单元个数，f是显示格式，u是内存单元大小
– 显示指定地址之后的10条汇编指令：x/10i addr
– 显示指定地址之后的10条数据单元：x/10x addr
– 退出：q
• 以上只是基础gdb命令，大家可以探索更多命令以
及组合的方式
Operating Systems
31

---

## Page 32　*(1 张图)*

Printl工具
• Printl：本课程开发的打印工具
– 与printf类似，在代码中插入后，可以把输出内容
打印到log文件中，而不会输出到屏幕
– 某些调试信息太多，在屏幕上看不清楚，在文件里
面比较方便查看
• 例如，在一个多次循环的代码中，每次循环输出某些状
态，并查看数值变化是否符合预期
• 开发板上也支持printl功能，作为开发板上的
一种辅助调试手段
– 帮助解决个别开发板与QEMU现象不一致的情况
Operating Systems
32

---

## Page 33　*(1 张图)*

课程作业检查标准
• 强调：每次project的完成代码都必须现场
给助教老师在开发板上运行通过，且上传
到gitlab上
– 后续会发放开发板
• 不能只在QEMU模拟器完成
Operating Systems
33

---

## Page 34　*(1 张图)*

今日课程内容
• 预备知识 & 课程环境搭建
• 1、虚拟机环境搭建
• 2、git环境介绍
• 3、RISC-V汇编介绍
• 4、QEMU模拟器和gdb介绍
• 5、预备课作业：9月9号课上检查
Operating Systems
34

---

## Page 35　*(1 张图)*

预备课作业
• 1、用RISC-V汇编编写两个测试程序：
– （1）1到50的叠加，叠加的结果放到指定的内存地址
上（实现时使用常量宏存这个地址，通过la指令把叠加
结果写到这个地址上）
– （2）判断1到200的每个数字是否为质数，要求用函数
调用实现，函数参数为数字，返回值为是否是质数。
– 这两个测试写完后用make命令编译（需要按现有提供
的Makefile脚本填写源代码文件路径，测试程序的名
字改为同学自己的名字）
• 2、在Linux命令行用riscv64***objdump命令打
印反汇编结果，用cat命令打印源代码内容，用ls
命令列出文件夹内容
Operating Systems
35

---

## Page 36　*(1 张图)*

预备课作业
• 3、用QEMU模拟器运行并用gdb连接QEMU运
行，打印运行结果的内存或寄存器状态
• 4、将源代码（包括Makefile）上传至gitlab，两
个作业题目放在不同的git分支上（使用git
checkout命令切换）
• 上述作业的基础代码即P0的start-code，包含一
个空的test.S和写好的makefile文件，请同学们在
其基础上完成。
• 在9月9日课上，现场检查作业中各项操作
Operating Systems
36

---

## Page 37　*(2 张图)*

课程微信群
Operating Systems
37
