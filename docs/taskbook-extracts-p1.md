# 任务书与课件原文摘录（P1）

> **使用规则**：本文件是任务书 / P1 课件的唯一权威引用源。今后引用任务书一律
> 以本文件的逐字文本为准，**不得凭记忆或转述构造"原文"**。
>
> 原始来源：`docs/guide_book_p1.pdf` 与 `docs/Lecture 1.pdf`。文本由验收方直接
> 提供逐字抄录，本站无法直接解析 PDF，故以验收方提供的文本为准。
>
> 标注约定：`【出处】` 后面的 "任务书原文 / 课件原文" 均逐字；凡非原文而属我们
> 自行定义的验收标准或设计，一律标注为 **"验收方要求"**，绝不冒充任务书原文。

---

## §4.6 正文（任务书原文，guide_book_p1.pdf，节标题：任务5：SD卡镜像分区管理和动态加载用户程序）

> 本任务是做C-core的同学必须完成的。在前面的任务中，用户程序和内核被放到同一个镜像文件中，并被一起拷贝到了SD卡上。本任务要求同学们模拟文件系统的分区管理，把内核部分和用户程序分别做成两个镜像文件。在内核启动的过程中，只把内核镜像从SD卡读取到内存中。用户程序在要执行的时候再动态的从SD卡中加载到内存地址执行，从而模拟系统中存在一个复杂的文件系统，里面包含各种用户程序。这样的系统也具有较好的扩展性，在用户程序很多的时候，也不影响内核启动的SD卡读取时间。

---

## §4.6 要点解读（任务书原文，guide_book_p1.pdf，恰好 4 条）

> 要完成内核和用户的数据分成两个镜像文件，需要同学们对createimage的过程进行修改，找到对应的数据并做到两个镜像文件中。所以在这个过程中，同学需要主要的要点包含以下几个：
> 1、在createimage中区分编译出来的文件哪些属于内核，哪些属于用户程序。并分别写入不同的文件。
> 2、修改Makefile，将两个文件都写入SD卡。
> 3、在内核镜像中标识用户镜像文件中各个用户程序的地址、大小等信息，用于后续的加载。
> 4、修改用户程序的加载过程，只加载要运行的用户程序。
> C-core任务具有一定难度，鼓励同学们思考，我们也不会给予太多的提示，需要同学们多阅读现有的代码框架，理解任务需求，并完成任务。

---

## §4.4 相关句（任务书原文，guide_book_p1.pdf，布局自主权）

> 同时，各个用户程序的task_info_t结构体也需要写入image中作为AppInfo，等待内核的读取。具体的格式，以及应该怎么排列image文件中各个结构的顺序，请同学们自行思考实现。

---

## slide 25（课件原文，Lecture 1.pdf，标题 Task 5 (C-Core)，3 条主 bullet 含 2 条子 bullet）

> Write kernel and user applications into two separate images
> The kernel image needs to store the information about user applications, such as the addresses, sizes of applications
> When kernel launches, only the kernel image is loaded
> You can provide commands to show user application information after the kernel is loaded
> When a user application starts, you need to load the application from the SD card at runtime

---

## 出处说明与勘误对照

1. **任务书中不存在"§5"编号**；Task 5 即 §4.6。
2. 任务书**没有**"启动日志证明只加载内核镜像"之类的验收条款——那是我们自行定义的
   验收标准，只能标注为 **"验收方要求"**，不得标注为任务书原文。
3. Phase 0 勘察报告中此前转述的条目（如"app 数目 @0x1fe"、"bootblock 由 u-boot
   预读 2 扇区"等），凡与本文档冲突的，**以本文档入库文本为准**；勘误见
   `log/Phase0-代码勘察报告.md` 勘误 #4。