# 任务5 修订：镜像内部改为【字节级】紧密排列

> 本文件记录任务5 收尾后的一次**布局语义修订**：从"各结构补到扇区边界"
> 改为"按字节首尾相接"。所有改动与验证结论都在此留档。

## 一、触发与冲突点（按规范先点破）

要求：**镜像内部按字节紧密排列**——一个结构结束于字节 N，下一个就从字节 N+1 开始，
不再补 0 到扇区边界。

接到要求时先核对了三处，确认存在冲突后才动手：

| # | 冲突 | 处理 |
|---|---|---|
| 1 | **任务书原文没有这条要求**。入库原文（`docs/taskbook-extracts-p1.md` §4.4）只说"具体的格式，以及应该怎么排列 image 文件中各个结构的顺序，**请同学们自行思考实现**" | 已向本人确认这是任务书/助教的新要求，按新要求执行 |
| 2 | **与已批准的 `log/task5-设计提案.md` 冲突**：该提案写的是"各 app 按实际大小紧凑排列 \| `Σ(app_sec × 512)` 字节，**恰好整扇区**" | 本次修订取代该节，旧提案保留为历史记录 |
| 3 | **与块设备物理约束冲突**：`bios_sd_read(addr, num, block)` 只能**整扇区读**、没有偏移参数。app 若不在扇区边界上，直接整扇区读会**整体错位**（例：app1 在第 9 扇区的第 144 字节，读到 0x52010000 时内容会晚 144 字节） | 采用**中转缓冲 + memcpy**（见下），加载路径增加一次数据搬运 |

## 二、新布局

```
build/kernel.img（4048 字节）
  字节    0 ..  511   bootblock（132 字节 → 补 0 到 512）★唯一强制补零处
  字节  512 .. 3599   内核（3088 字节，不补）
  字节 3600 .. 4047   AppInfo（448 字节，不补）

build/apps.img（10800 字节）
  字节    0 .. 4751   2048  （4752 字节）
  字节 4752 .. 5395   auipc （644 字节）     ← 紧贴上一个的最后一个字节
  字节 5396 .. 5763   bss   （368 字节）
  字节 5764 .. 10299  data  （4536 字节）
  字节 10300..10799   末尾补 0（500 字节，仅为整张 image 落在整扇区上）

build/image = cat kernel.img apps.img = 14848 字节 = 29 扇区
```

**★ 为什么 bootblock 仍补到 512（唯一例外）**：

1. `0x1fc`（内核扇区数）与 `0x1fe`（`55 aa` 签名）两个字段就住在第 0 扇区的
   **最后 4 个字节**（官方 §4.3 推荐约定）。若内核从字节 130 开始，这两个字段
   会写进内核的数据里，**把内核改坏**；
2. u-boot 是按块读第 0 扇区再跳到 `0x50200000` 的；
3. 故 bootblock 独占第 0 扇区是**硬性约束**，不是"想补 0"。

## 三、镜像信息字段（第 0 扇区尾部，全部改为字节语义）

| 偏移 | 宽度 | 含义 |
|---|---|---|
| `0x1f0` | 4 字节 | **AppInfo 起始字节偏移**（新增） |
| `0x1f4` | 4 字节 | **apps 区起始字节偏移**（新增，= kernel.img 总字节数） |
| `0x1f8` | 2 字节 | 用户程序数目 |
| `0x1fc` | 2 字节 | 内核扇区数（保持官方 `lh` 约定） |
| `0x1fe` | 2 字节 | 签名 `55 aa` |

之所以能新增 `0x1f0/0x1f4`：`0x1f0~0x1f7` 这 8 字节原本就是空的（`0x1f8` 才被占用）。
bootblock 用 `lw`/`lh` 三条指令一次性读全（`li t3, os_size_loc - 12` → `0x1f0`）。

## 四、改动清单（8 个文件）

| 文件 | 改动 |
|---|---|
| `tools/createimage.c` | 布局计算改为**字节累加**（`cur += used_bytes[...]`，不再 `NBYTES2SEC`）；`task_info_t` 字段 `start_sector/sector_num` → `start_byte/byte_size`；`write_img_info()` 增加两个 4 字节字段；apps.img 末尾做整扇区对齐；自检全部改为字节口径 |
| `include/os/task.h` | `task_info_t` 字段改名（**仍是 28 字节**）；`APPINFO_SECTORS` → `APPINFO_BYTES`(=448)；全局量改名（`boot_appinfo_off` / `boot_apps_base_off`）；`_Static_assert` 第三条改为"AppInfo 必须是 448 字节" |
| `arch/riscv/boot/bootblock.S` | 传参改为从 `0x1f0/0x1f4/0x1f8` 读（`lw`/`lw`/`lh`），三条指令一次读完 |
| `arch/riscv/kernel/head.S` | 保存到改名后的全局量（逻辑不变，仍是"清完 BSS 再存"） |
| `init/main.c` | `init_task_info()`：AppInfo 现在**不对齐**，改为"算扇区+偏移 → 读 2 块到 1024B 缓冲 → 从 offset 拷 448B"；`show_task_list()` 表头与列改为字节语义（`offset size entry`） |
| `kernel/loader/loader.c` | **核心**：`abs = boot_apps_base_off + start_byte` → 拆成扇区+偏移 → 读 `NBYTES2SEC(offset+size)` 块进 8KB 中转缓冲 → `memcpy` 到窗口地址；缓冲装不下则拒绝加载（不越界） |
| `.scratch/verify.sh` | 制品断言改为"从镜像字段反推布局"，新增**字节级紧贴**的铁证断言（`auipc.start_byte == 2048.start_byte + 2048.byte_size`） |
| `README.md` / `docs/P1-design-review.md` | 布局章节与输出示例同步为字节语义 |

## 五、关键实现：两处"中转缓冲"

块设备没有偏移参数，所以凡是**不对齐**的读取都要走中转：

**① 内核读 AppInfo（`init/main.c`）**
```c
uint32_t start  = (uint32_t)boot_appinfo_off;      // 3600
uint32_t sector = start / SECTOR_SIZE;             // 7
uint32_t offset = start % SECTOR_SIZE;             // 16
uint32_t need   = NBYTES2SEC(offset + APPINFO_BYTES);   // 2 块
bios_sd_read(appinfo_buf /*1024B*/, need, sector);
memcpy(tasks, appinfo_buf + offset, task_num * sizeof(task_info_t));
```

**② 内核加载 app（`kernel/loader/loader.c`）**
```c
uint32_t abs_off = boot_apps_base_off + tasks[i].start_byte;
uint32_t sector  = abs_off / SECTOR_SIZE;
uint32_t offset  = abs_off % SECTOR_SIZE;
uint32_t need    = NBYTES2SEC(offset + tasks[i].byte_size);
if (need * SECTOR_SIZE > LOAD_STAGING_BYTES) return 0;   // 防御
bios_sd_read(load_staging /*8192B*/, need, sector);
memcpy((uint8_t *)vaddr_entry, load_staging + offset, tasks[i].byte_size);
```

**代价**：每个 app 多一次数据搬运 + 一块常驻 8KB 缓冲（在 `.bss` 里）。
**收益**：apps.img 从 11264 字节降到 10300 字节（省掉 964 字节 padding）。

## 六、实测验证

`make image` 打印（本次构建）：

```
0x50200000: bootblock byte     0 ..   511  (132 bytes → 补到 512)
0x50201000: main     byte   512 ..  3599  (3088 bytes, 7 sectors)
-APPINFO--- appinfo  byte  3600 ..  4047  (448 bytes)
0x52000000: 2048     byte     0 ..  4751  (4752 bytes)
0x52010000: auipc    byte  4752 ..  5395  (644 bytes)
0x52020000: bss      byte  5396 ..  5763  (368 bytes)
0x52030000: data     byte  5764 .. 10299  (4536 bytes)
[layout] apps.img   : 10800 bytes = 10300(apps) + 500(尾部对齐)
[layout] image      : 14848 bytes = 29 sectors
[img info] appinfo offset : 3600 @ 0x1f0
[img info] apps offset    : 4048 @ 0x1f4
[img info] kernel sectors : 7 @ 0x1fc
```

**QEMU 实机 `list` 输出**（字节偏移，紧贴可见）：

```
name              offset size   entry
2048            0    4752    0x52000000
auipc           4752    644    0x52010000
bss             5396    368    0x52020000
data            5764    4536    0x52030000
```

**自动化验证：18 项断言全部通过**（`.scratch/verify.sh`），其中：

| # | 断言 | 结果 |
|---|---|---|
| 1 | `image == kernel.img + apps.img` | PASS |
| 2 | 整张 image 落在整扇区（29 扇区） | PASS |
| 3 | `apps 偏移(4048) == kernel.img 总字节数` | PASS |
| 4 | AppInfo 紧贴 kernel.img 末尾（448 字节） | PASS |
| 5 | `0x1fe = 55 aa` 签名完好 | PASS |
| 6 | 2048 的 `start_byte = 0` | PASS |
| **7** | **`auipc.start_byte(4752) == 2048.start_byte(0) + 2048.byte_size(4752)`——字节级紧贴的铁证** | **PASS** |
| 8 | 名册第 1 行名字正确 | PASS |
| 9~18 | 四个 app 按名启动、2048 退出返回内核、未知名/空行被拒（行为回归） | PASS |

## 七、复查项（沿用任务4 立下的规矩）

**`head.S` 的 `sd` 清 BSS 依赖 `__bss_start` 8 字节对齐**——本次 `.bss` 因新增
8KB 中转缓冲大幅变大，**已复查**：

```
__bss_start = 0x50201c10   →  0x50201c10 % 8 == 0  ✅ 仍 8 对齐
__BSS_END__ = 0x50204600   →  BSS 共 10736 字节（10.5 KB）
距内核栈顶 0x50500000 还有约 3054 KB，安全
```

## 八、遗留风险

1. **中转缓冲是定长 8KB**：若将来某个 app 的字节数 + 511 > 8192，
   `load_task_img` 会**拒绝加载**（返回 0，主循环报 `unknown task or load failed`），
   不会越界。此时需要把 `LOAD_STAGING_BYTES` 调大或改为分块搬运。
2. **内核读 AppInfo 的缓冲固定 2 扇区**：因为 `offset(≤511) + APPINFO_BYTES(448) ≤ 959 < 1024`，
   这是由 `APPINFO_BYTES = 448` 保证的；`_Static_assert(APPINFO_BYTES == 448)` 会在
   编译期挡住"结构体变大导致溢出"的情况，但**如果将来 TASK_MAXNUM 变大，这个断言也要同步改**。
3. **`apps.img` 末尾的 500 字节补零**是为了让整张 image 落在整扇区上（块设备边界）。
   它是"容器级"的对齐，不是"结构级"的 padding——镜像内部各结构仍严格字节级紧贴。
