# C-Core Bootloader（UCAS 操作系统研讨课 Project 1）

完整的五任务实现：bootblock → 内核加载与 C 运行环境 → 自建 createimage 与多任务加载
→ 紧凑镜像 + 按名加载 → 双镜像内核分区 + 运行时动态加载用户程序。全部在 QEMU 上
验证通过（无开发板）。

## 概览

- 启动链路：QEMU `-kernel u-boot` → 输入 `loadboot` → bootblock（0x50200000）
  → 调 BIOS 从 SD 读内核到 0x50201000 → head.S 清 BSS、设栈、跳 main。
- 镜像：`build/image = build/kernel.img ∥ build/apps.img`（cat 拼接，等价于
  "两文件分别写入 SD 卡连续扇区"）。
- 用户程序：按名字运行（`auipc` / `bss` / `data` / `2048`），可用 `list` 查看
  应用信息（name/start/size/entry）。

## 构建与运行

```bash
make dirs && make elf      # 交叉编译 bootblock / 内核 / 4 个 app
make image                 # createimage 产出 kernel.img+apps.img 并 cat 成 image
make run                   # 启动 QEMU，出现 "=>" 后输入 loadboot
make debug + make gdb      # QEMU 联调
```

启动后交互示例：

```
loadboot
It's Dreamerhyh's bootloader...
Hello OS!
Available tasks: 2048, auipc, bss, data
Please input a task name: list
name              start size   entry
2048            0    10    0x52000000
...
Please input a task name: auipc
[auipc] Info: testpc = 0x...
```

## 布局摘要

- 内存：bootblock `0x50200000`；内核 `0x50201000`；内核栈 `0x50500000`；
  app 共享栈 `0x50400000`；BIOS 跳转表 `0x51ffff00`；app 窗口 `0x52000000 + i*0x10000`。
- 镜像信息字段（kernel.img 第 0 扇区尾部）：`0x1f8`=app 数，`0x1fc`=内核扇区数，
  `0x1fe`=`55 aa` 签名；AppInfo 区在 kernel.img 扇区 `1+K`（`start_sector` 为
  apps.img 内相对扇区号）。

## 文档

- 教学文档（11 件套）：`docs/00-项目总览.md` … `docs/10-常见报错FAQ.md`（完整目录见 [docs/](docs/)）
- 设计评审（slide 26 四要点 + 演示流程 + 预答清单）：`docs/P1-design-review.md`
- 任务书/课件原文：`docs/taskbook-extracts-p1.md`
- 各任务日志与工作流程：`log/`（勘察报告、工作流程、task1–5 日志、task5 设计提案）

## 分支与提交

全部工作在 `Project1` 分支；每个任务一个中文 commit（见 `git log`）。