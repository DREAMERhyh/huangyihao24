# C-Core Bootloader（UCAS 操作系统研讨课 Project 1）

五个任务全部实现，并在 QEMU 上验证通过（无开发板）。
**本文件只列验收时实际要敲的命令**；原理与设计说明见 [docs/](docs/)。

---

## 一、构建

```bash
cd ~/oslab-course/huangyihao24      # 仓库根目录

make dirs                           # 建 build/ 目录
make elf                            # 交叉编译 bootblock / 内核 / 4 个 app
make image                          # createimage 产出 kernel.img + apps.img，再 cat 拼成 image
```

等价的一条命令：

```bash
make all
```

**构建成功标志**（`make image` 末尾会打印布局核算）：

```
[layout] kernel.img :  4048 bytes = 512(boot) + 3088(kernel) + 448(appinfo)
[layout] apps.img   : 10800 bytes = 10300(apps) + 500(尾部对齐)
[layout] apps 区偏移  : 4048 (byte) —— 内核字节数 3088，二者【不补到扇区】
[layout] total      : 14848 bytes = 29 sectors（末尾补 500 字节）
```

---

## 二、在 QEMU 上运行验证

```bash
make run
```

**QEMU 里的操作顺序**：

```
① 等约 6 秒（U-Boot 自检 + autoboot 走完，出现 "=> " 提示符）

② 输入：
        loadboot

③ 屏幕依次出现：
        It's Dreamerhyh's bootloader...
        Hello OS!
        bss check: t version: 2
        Available tasks: 2048, auipc, bss, data
        Please input a task name:

④ 依次输入（每个名字后按回车）：
        list         ← 应用信息表（name / offset / size / entry，offset 与 size 均为字节）
        auipc        ← [auipc] Info: testpc = 0x5201xxxx
        bss          ← [bss] Info: passed bss check!
        data         ← [data] Info: checking data --- [OK]!
        2048         ← 打印游戏菜单后按回车继续；按 q 退出游戏
        nope         ← 应提示 unknown task 且不跑飞

⑤ 退出 QEMU：
        Ctrl+a    然后按    x
```

---

## 三、自动化验证（可选）

```bash
bash .scratch/verify.sh
```

脚本会自动起 QEMU、注入上述输入、断言 14 项、打印
`>>> 任务5 全部断言通过 <<<`，并清理残留进程。

---

## 四、上板（有开发板时）

```bash
make all                            # 构建（与 QEMU 完全相同）
make floppy                         # sudo dd if=build/image of=/dev/sdb3 conv=notrunc
make minicom                        # sudo minicom -D /dev/ttyUSB1
```

在 minicom 里同样输入 `loadboot`，然后按上面第 ②③④ 步操作。

---

## 附一：关键地址与镜像字段（速查）

| 项 | 值 |
|---|---|
| bootblock 加载地址 | `0x50200000` |
| 内核入口 | `0x50201000` |
| 内核栈顶 | `0x50500000` |
| app 共享栈顶 | `0x50400000` |
| BIOS 跳转表 | `0x51ffff00` |
| app 窗口 | `0x52000000 + i*0x10000` |
| 镜像信息字段 | `0x1f0`=AppInfo 字节偏移、`0x1f4`=apps 区字节偏移、`0x1f8`=app 数、`0x1fc`=内核扇区数、`0x1fe`=`55 aa` |
| 镜像内部布局 | **字节级紧密**（不补扇区）：bootblock 512B → 内核 → AppInfo(448B) → apps 首尾相接；仅 bootblock 补到 512（`0x1fc/0x1fe` 字段占位所需） |

## 附二：文档索引

- 教学文档 11 件套：`docs/00-项目总览.md` … `docs/10-常见报错FAQ.md`
- 设计评审（slide 26 四要点 + 演示流程 + 预答清单）：`docs/P1-design-review.md`
- 代码演进史 + 学习问答集（78 问）：`docs/11-代码演进史.md`
- 汇报素材（我写的代码清单）：`docs/12-我写的代码清单.md`
- 任务书/课件原文：`docs/taskbook-extracts-p1.md`
- 各任务日志与工作流程：`log/`

## 附三：分支

全部工作在 `Project1` 分支；每个任务一个中文 commit（见 `git log`）。
