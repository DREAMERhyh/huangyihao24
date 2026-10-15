# 中间产物索引

本文件夹存放《操作系统研讨课》（B0911011H, 2026 Fall, RISC-V 版）教学资料的**可搜索文本版**与**关键页截图**，
由根目录下的 PDF / xlsx 抽取而来，供学习与答疑使用。

- 抽取工具：PyMuPDF（`pymupdf`）+ pandas
- 抽取脚本：[_tools/](_tools/)
- 抽取时间：2026-09-29（P0~P2）；2026-10-14（P3 增补）

## 目录结构

```
中间产物/
├── 00-课程总览/
│   ├── Lecture0-课程导论/        课程目标、评分、S/A/C-Core 分级、时间表、联系方式
│   └── 分组名单/                 P0/P1 班级名册（8 行 × 8 人）
├── P0-环境与预备知识/
│   └── Project0-课程环境与预备知识/  虚拟机、git、RISC-V 汇编、QEMU+gdb、预备课作业
├── P1-引导与镜像文件/
│   ├── GuideBook-P1-引导与镜像/   任务书第一章（任务 1-5）
│   └── Lecture1-Bootloader/       P1 课件
├── P2-简易内核/
│   ├── GuideBook-P2-简易内核/     任务书第二章（任务 1-5）
│   ├── Lecture2-简易内核/         P2 课件
│   └── P2-startcode与板卡启动/     git 取代码流程 + PYNQ-Z2 上板步骤
└── P3-进程管理、通信与多核执行/
    ├── GuideBook-P3-进程管理、通信与多核执行/   任务书第三章（任务 1-5）★ 当前进度
    └── Lecture3-进程管理、通信与多核执行/       P3 课件 ★ 当前进度
```

每个文档文件夹下：

- `*.md` —— 全文逐页文本，带 `## Page N` 页码锚点，可直接搜索/引用
- `pages/*.png` —— 关键图页截图（**PPT 里的代码片段是图片，纯文本抽取拿不到，必须看图**）

## 学习笔记

| 文件 | 内容 |
|---|---|
| [Lecture2-中文精读笔记.md](P2-简易内核/Lecture2-简易内核/Lecture2-中文精读笔记.md) | Lecture 2 全部 48 页的**逐页中文翻译 + 要点讲解**。含术语速查表、design review 问题总表、文件路径速查、6 个必踩坑清单 |

## 重要：哪些内容只在截图里

抽取成 `.md` 时，以下内容是**以图片形式嵌在 PPT 里的，文本版里没有**，需要看 `pages/` 下的截图：

| 文件 | 页 | 内容 |
|---|---|---|
| Lecture2 | p07 | `typedef struct pcb` 完整结构体定义 |
| Lecture2 | p11 | `do_scheduler()` 代码骨架 + 3 个 TODO |
| Lecture2 | p13 | `init_pcb_stack()` 代码 + regs_context_t / switchto_context_t 布局 |
| Lecture2 | p15 | `switch_to` 汇编完整骨架（含 `SWITCH_TO_SIZE`） |
| Lecture2 | p09/p19 | 进程状态机图（Ready/Running/Blocked/Exited） |
| GuideBook-P2 | p06 | 单内核栈 vs 多内核栈示意 |
| GuideBook-P2 | p09/p12/p20 | 任务 1/2/3 的参考运行结果 |
| GuideBook-P3 | p05/p08/p10/p12/p16/p17 | 图P3-1 运行效果示例；图P3-2/3 exec·waitpid 参考结果；图P3-4 argc/argv 用户栈布局图；图P3-5/6 barrier·条件变量测试；图P3-7 双核加速比；图P3-8 绑核效果 |
| Lecture3 | p08/p12/p15/p20 | 终端运行截图（shell/ps/unknown cmd）；`sys_exec` 两种签名代码行；`&` 用法截图；mailbox 有界缓冲示意图 |

## 时间线

| 节点 | 日期 |
|---|---|
| Project 0 | 2026-09-02 |
| Lecture 0 / Lecture 1 | 2026-09-09 |
| P1 design review | 2026-09-16 |
| P1 due | 2026-09-23 |
| Lecture 2 | 2026-09-23 |
| P2 design review | 2026-09-30（周三） |
| P2 due | 2026-10-14 |
| Lecture 3 | 2026-10-14 |
| **P3 design review** | **2026-10-21（周三）** |
| **P3 due（C-Core 无延期）** | **2026-10-28** |

上课时间固定在**每周三**（对照 Lecture 0 秋季教学日历）。

## 重新生成

```bash
"D:/anaconda3/envs/pytorch_env/python.exe" _tools/extract_all.py      # PDF -> md
"D:/anaconda3/envs/pytorch_env/python.exe" _tools/extract_xlsx.py     # xlsx -> md
"D:/anaconda3/envs/pytorch_env/python.exe" _tools/render_pages.py     # 关键页 -> png
"D:/anaconda3/envs/pytorch_env/python.exe" _tools/render_more.py      # 补充页 -> png
"D:/anaconda3/envs/pytorch_env/python.exe" _tools/extract_p3.py       # P3 增补（md + 关键页 png）
```

脚本里的路径是硬编码的，换机器需要改 `ROOT`（`extract_p3.py` 例外：路径从脚本位置推出）。
