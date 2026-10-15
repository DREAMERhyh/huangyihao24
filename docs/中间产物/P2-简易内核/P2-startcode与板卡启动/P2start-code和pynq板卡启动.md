# P2start-code和pynq板卡启动

> 源文件：`P2start-code和pynq板卡启动.pdf`　页数：4


---

## Page 1

P2 start-code获取
• 1. 先切到本地已有分支 Project1
• git checkout Project1
• 2. 基于 Project1 创建并切换到新分支 Project2
• git checkout -b Project2
• 3. 拉取 start-code 最新信息
• git fetch upstream
• 这里upstream是之前获取P1的startcode时，命令git remote add后面跟着的名字
• 4. 合并 start-code 的 Project2 分支到本地 Project2
• git merge upstream/Project2
• merge过程可能报告一些conflict，需要根据报错去每个文件手动处理
• 5. 推送到自己的 Project2
• git push -u origin Project2

---

## Page 2　*(1 张图)*

*（本页无可抽取文本，内容以图片/公式为主）*

---

## Page 3

启动检查
• 1. 将开发板设置为 SD 卡启动：图示中的 1 设置的是开发板的启动方式。PYNQ-Z2 支持从 SD、QSPI
和 JTAG 启动。我们将跳线插到最左侧，选择用SD启动。板子上也有标明，最左侧是 SD 启动。请保
证跳线跳在了最左侧的两根针上面。
• 2. 设置电源选项图示中的 2 设置的是开发板的供电方式。PYNQ 支持电源供电和 Micro-USB 供电。我
们把跳线跳在 USB 这个选项上（靠上侧的两个针脚），用Micro USB直接供电。
• 3. 插入 SD 卡。注意插入方向，不要插反。
• 4. 插入 Micro-USB。Micro-USB 一端连接图示 4 位置，一端连接笔记本的 USB 口。 该线同
时兼顾供电和传输数据的功能。
• 5. 设置 SW1 开关拨动图示 7 位置处的 SW1 开关到靠近板卡边缘一侧。该开关用于切换单
核和双核。开关拨动到靠近板卡边缘一侧是单核。
• 6. 打开电源开关：打开图示 5 位置的电源开关，开发板上红色指示灯亮起，之后绿色的
Done 信号灯亮起。之后代表 NutShell 的 LED 灯 LD0 亮起 1 秒后熄灭，随后 Done 信号灯
重新亮起，表明开发板已开始工作。 （部分开发板灯的颜色不同，亮起即可）
• 7. RESET 键：图示 6 位置为 reset 按键，板子上标记了 SRST 字样。按下该按键会让开发板
重置。如果需要重新从开发板加电开始再执行一遍，可以按该按键。

---

## Page 4

板卡连接
• 1、检查VirtualBox是否连接到板卡
• 设备->USB里面TUL选项打勾
• 虚拟机Linux系统下存在/dev/ttyUSB1
• 2、配置minicom
• 命令：sudo minicom –s
• A-Serial Device: /dev/ttyUSB1
• E-Bps/Par/Bits: 115200
• F-Hardware Flow Control: No
• 3、烧写sd卡
• 注意确保已经先用make all命令生成好操作系统镜像
• sd卡插在读卡器里，读卡器插到电脑上被虚拟机获取
• 命令：make floppy
• 4、连接minicom
• 命令：make minicom / sudo minicom
• 看到命令提示符，输入loadboot
