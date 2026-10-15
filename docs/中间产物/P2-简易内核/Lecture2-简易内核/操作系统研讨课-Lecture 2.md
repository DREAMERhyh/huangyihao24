# 操作系统研讨课-Lecture 2

> 源文件：`操作系统研讨课-Lecture 2.pdf`　页数：48


---

## Page 1　*(1 张图)*

Lecture 2 A Simple Kernel
2026.09.23

---

## Page 2　*(1 张图)*

Schedule
• Project 1 due
• Project 2 assignment
Operating Systems
2

---

## Page 3　*(2 张图)*

Project 2 – A Simple Kernel
• Booting procedure
Operating Systems
3

---

## Page 4　*(1 张图)*

Project 2 – A Simple Kernel
• Requirement I
– Write a simple kernel (non-preemptive)
• Process management
– Start a set of processes
– Provide context switches between processes
with non-preemptive scheduling
– Support basic mutex locking to enable BLOCK
state of processes
Operating Systems
4

---

## Page 5　*(1 张图)*

Project 2 – A Simple Kernel
• A set of user processes
– print1, print2, flyer
– Please first read the codes of different tasks
to understand what they do
Operating Systems
5

---

## Page 6　*(1 张图)*

Project 2 – A Simple Kernel
• Process Control Block (PCB)
– A in-memory data structure in OS kernel
containing the information to manage a
process
– Please refer to the given definition pcb_t
(include/os/sched.h)
Operating Systems
6

---

## Page 7　*(2 张图)*

Project 2 – A Simple Kernel
• Process Control
Block (PCB)
– Process ID
– Process status
– Kernel stack pointer
– User stack pointer
– Register context
– You may need to add
new fields to PCB in
subsequent projects
Operating Systems
7

---

## Page 8　*(1 张图)*

Project 2 – A Simple Kernel
• Initialize PCB
– Initialize PCBs for tested processes
• Assign ID, set status/cursor_x/cursor_y
• PCBs can be organized as a linked list
– Allocate memory for kernel stack and user stack
• Please use allocKernelPage/allocUserPage functions
(kernel/mm/mm.c)
Operating Systems
8

---

## Page 9　*(2 张图)*

Project 2 – A Simple Kernel
• Scheduler (non-preemptive kernel)
Operating Systems
9

---

## Page 10　*(1 张图)*

Project 2 – A Simple Kernel
• Yield
– A process itself releases the control of CPU
by calling kernel scheduler
– Kernel scheduler places the current
running process to the end of the ready
queue, and chooses another process to
run
– Please refer to kernel_yield() function
Operating Systems
10

---

## Page 11　*(2 张图)*

Project 2 – A Simple Kernel
• Context switch
– Kernel scheduler executes context switch
to run new process
– kernel/sched/sched.c  do_scheduler()
Operating Systems
11

---

## Page 12　*(1 张图)*

Project 2 – A Simple Kernel
• Context switch
– Save context
• CPU registers →kernel stack (in memory)
– Restore context
• kernel stack →CPU registers
Operating Systems
12

---

## Page 13　*(2 张图)*

Project 2 – A Simple Kernel
• Kernel stack
initialization
– Kernel stack is used
to save and restore
the context of a
process
– Pls. refer to
switchto_context_t
Operating Systems
13

---

## Page 14　*(1 张图)*

Project 2 – A Simple Kernel
• switch_to function
– Work like a function call, but after the call
the return address is another process
– In switch_to,  first save the process
context of the previous process, and then
restore the process context of the next
process
– Where to place the process context?
• Kernel stack
Operating Systems
14

---

## Page 15　*(2 张图)*

Project 2 – A Simple Kernel
• switch_to function
Operating Systems
15

---

## Page 16　*(2 张图)*

Project 2 – A Simple Kernel
• Start a process
– After initialization, the kernel calls
do_scheduler()
Operating Systems
16

---

## Page 17　*(1 张图)*

Project 2 – A Simple Kernel
• Start a process
– You need to choose a new process to run in
do_scheduler
• FIFO is a preferred scheduling algorithm
• In do_scheduler(), switch_to is called to perform
context switch between the current running
process and the new one
Operating Systems
17

---

## Page 18　*(1 张图)*

Project 2 – A Simple Kernel
• Start a process
– Note that, when you start the first process,
there is no current running user process,
how do you handle this?
• Please refer to pid0_pcb (include/os/sched.h)
• It can be used for store context of kernel
process (main.c)
Operating Systems
18

---

## Page 19　*(2 张图)*

Project 2 – A Simple Kernel
• BLOCK state of a process
Operating Systems
19

---

## Page 20　*(1 张图)*

Project 2 – A Simple Kernel
• Mutex lock
– What if no process currently holds the lock?
• Acquire the lock
– What if the lock is currently held?
• Wait
– Implement lock-related functions
– Manage processes that do not acquire the
lock
• Ready queue vs. wait queue?
Operating Systems
20

---

## Page 21　*(1 张图)*

Project 2 A Simple Kernel
• Step by step – Task 1
– Initialize PCBs for tested processes
– Implement do_scheduler and switch_to
– Start tested processes and support context
switch among these processes as a non-
preemptive kernel
Operating Systems
21

---

## Page 22　*(1 张图)*

Project 2 A Simple Kernel
• Step by step – Task 2
– Implement mutex lock to support BLOCK
state
Operating Systems
22

---

## Page 23　*(1 张图)*

Project 2 A Simple Kernel
• Requirement for design review
– switch_to函数的工作机制是怎样的？是如何支持
两个进程的上下文切换的?
– 你在初始化PCB和内核栈时需要做哪些事情？
– 你在switchto_context_t中会保存哪些内容?
– 请介绍你设计的内核调度器(do_scheduler)的工作
流程
– 当一个进程被阻塞时，内核会进行哪些处理？
Operating Systems
23

---

## Page 24　*(1 张图)*

Project 2 – A Simple Kernel
• Requirement II
– Write a simple kernel (preemptive)
• Provide preemptive scheduling supporting
exception handler, including
– System call handler
– Clock interrupt handler
– Round-robin scheduler
• A complex scheduler (optional for C-Core)
Operating Systems
24

---

## Page 25　*(2 张图)*

Project 2 A Simple Kernel
• A set of user process
– User processes call
syscalls to invoke kernel
services
– Please refer to
unistd.h(tiny_libc/include)
and syscall.c (tiny_libc/)
Operating Systems
25

---

## Page 26　*(1 张图)*

Project 2 A Simple Kernel
• Interrupt
• A signal to the processor emitted by
hardware indicating an event requiring
immediate process
• The processor responds by suspending its
current running task, saving its state, and
executing interrupt handler
• After the interrupt handler finishes, the
processor resumes normal activities
Operating Systems
26

---

## Page 27　*(2 张图)*

Project 2 A Simple Kernel
• Interrupt
Operating Systems
27
Normal execution
Normal execution
with interrupt

---

## Page 28　*(1 张图)*

Project 2 A Simple Kernel
• Hardware interrupt
– A change in execution caused by a hardware
event
• Timer interrupt for timesharing
• I/O device interrupt: disk, network, keyboard etc.
• We do not handle other interrupts here
– Page fault, TLB miss etc.
• Trap
– System calls
Operating Systems
28

---

## Page 29　*(1 张图)*

Project 2 A Simple Kernel
• Handling exception
– Save context
– Determine what causes
exception
– Invoke specific routine
based on type of
exception
– Restore context
– Return from exception
Operating Systems
29
Exception
handler
Program
SRET

---

## Page 30　*(1 张图)*

Project 2 A Simple Kernel
• Control Status Registers(CSRs)
– We need to manipulate CSRs when
implementing interrupt handler
• Two types of CSRs
– One type is for setting up interrupt
– The other is for handling interrupt
– Pls. refer to Table P2-2 in guidebook

---

## Page 31　*(2 张图)*

Project 2 A Simple Kernel
• Control Status Registers(CSRs)

---

## Page 32　*(2 张图)*

Project 2 A Simple Kernel
• Setup interrupt
– sie register
– STIE(Supervisor-level timer interrupt enable)
– Use enable_preempt
(arch/riscv/kernel/entry.S) to set up timer
interrupt
Operating Systems
32

---

## Page 33　*(2 张图)*

Project 2 A Simple Kernel
• Setup interrupt
– sstatus register
Operating Systems
33

---

## Page 34　*(1 张图)*

Project 2 A Simple Kernel
• Setup interrupt
– SIE (Supervisor Interrupt Enable) bit enables or
disables all interrupts in supervisor mode
– SPIE (Supervisor Previous Interrupt Enable) bit
indicates whether supervisor interrupts were
enabled prior to trapping into supervisor mode.
– SPP (Supervisor Previous Privilege Mode) bit
indicates the privilege mode before the trapping
– You need to correctly initialize SPIE(1), SPP(0) in
sstatus register
Operating Systems
34

---

## Page 35　*(1 张图)*

Project 2 A Simple Kernel
• Manipulate CSR registers
– csrr: read csr
• Loads data from a CSR register into a CPU register
– csrw: write csr
• Stores data into a CSR register
– csrc/csrs：clear/set a CSR register’s
corresponding bits
• Examples
– csrr a0, sip
– csrw stvec, t0
Operating Systems
35

---

## Page 36　*(1 张图)*

Project 2 A Simple Kernel
• What if interrupt occurs while in
interrupt handler?
– The processor automatically disables all
interrupts when an interrupt occurs
– The processor automatically re-enables all
interrupts after sret is called
Operating Systems
36

---

## Page 37　*(3 张图)*

Project 2 A Simple Kernel
• Entry to handle exception
– stvec register
– BASE field hold the exception handler’s address
– exception_handler_entry in entry.S is the entry
point for handling exceptions
Operating Systems
37

---

## Page 38　*(3 张图)*

Project 2 A Simple Kernel
• Identifying exception
type
– scause register
– pls. refer to Table P2-3
– When exception
occurs, uses scause
register to identify
exception type
Operating Systems
38

---

## Page 39　*(1 张图)*

Project 2 A Simple Kernel
• Return from exception
– sepc register
– The PC address when exception occurs is
stored in sepc
– sret can return to the address in sepc
– Please pay attention to the sepc address
when handling syscall
• sret should return to sepc+4
Operating Systems
39

---

## Page 40　*(1 张图)*

Project 2 A Simple Kernel
• Support  process sleep
– Blocking sleep
• Block the task when it calls sleep()
• Use a separate queue to keep sleeping tasks
– Wake up  the task
• When the timing reaches sleeping threshold of
the task
– About timing
• We provide timing related functions in time.c
• Use these functions to decide whether sleeping
ends
Operating Systems
40

---

## Page 41　*(1 张图)*

Project 2 A Simple Kernel
• Implement timer interrupt handler
– Deal with normal tasks
• Schedule based on your scheduling policy
• Round Robin policy is OK
– Deal with sleeping tasks
• Check whether waking up the task
– Reset timer using bios_set_timer
(include/os/bios.h)
– CPU frequency is obtained using
bios_read_fdt in main.c
Operating Systems
41

---

## Page 42　*(1 张图)*

Project 2 A Simple Kernel
• Step by step – Task3
– Implement syscalls
– Support do_sleep
– In this task, we do not have timer interrupt,
you need to consider when to check
sleeping time
Operating Systems
42

---

## Page 43　*(1 张图)*

Project 2 A Simple Kernel
• Step by step – Task 4
– Implement timer interrupt handler
– Implement round robin scheduler
Operating Systems
43

---

## Page 44　*(2 张图)*

Project 2 A Simple Kernel
• Step by step – Task 5
– There exist five airplanes with
different CYCLE_PER_MOVE
– Please design a scheduling
policy to enable the five
airplanes to fly with the same
speed
• e.g. the slow airplane gets large
time slice than the fast one
Operating Systems
44

---

## Page 45　*(1 张图)*

A note to “loadbootd”
• Open CPU in DASIC mode
– Prevent user code jump/call kernel code
– Prevent user code access kernel data
– Provide memory protection before virtual
memory is available
– Run both in QEMU and Pynq
• A/C core should pass loadbootd check
Operating Systems
45

---

## Page 46　*(1 张图)*

Project 2 A Simple Kernel
• Requirement for design review
– 请描述syscall的执行流程，以及开发syscall时，如何
实现参数传递？
– 在初始化中断处理时，需要设置哪些寄存器，具体初始
化为什么值？
– 当进程因异常进入内核时，需要进行哪些上下文保存？
– 开发任务3时，init_pcb_stack函数需要完成哪些初始
化操作？
– 任务3和4中，分别何时唤醒睡眠的进程？
– Any questions you are not clear enough
Operating Systems
46

---

## Page 47　*(1 张图)*

Project 2 – A Simple Kernel
• Requirement for S/A/C-Core
Operating Systems
47
Core type
Task requirements
S-Core
Tasks 1, 2,3-（只用打印例外报错信息即可）
A-Core
Tasks 1,2,3,4
C-Core
Tasks 1,2,3,4,5

---

## Page 48　*(1 张图)*

Project 2 – A Simple Kernel
• P2 schedule
– 30th Sep.
• P2 review
– 14th Oct.
• P2 due
Operating Systems
48
