#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KERNEL_IMAGE_FILE "./kernel.img"
#define APPS_IMAGE_FILE   "./apps.img"
#define ARGS "[--extended] [--vm] <bootblock> <executable-file> ..."

#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2)
#define BOOT_LOADER_SIG_1 0x55
#define BOOT_LOADER_SIG_2 0xaa

#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* task_info_t，与 include/os/task.h 完全一致（必须保持同步）。
 * P4 增加 mem_size（p_memsz 之和），加载器据此建立用户页表。
 * 大小 16+4+4+4+4 = 32 字节，32 × 32 = 1024 字节。 */
#define TASK_NAME_LEN 16
/* 与内核 include/os/task.h 的 TASK_MAXNUM 保持一致。 */
#define TASK_MAXNUM   32
typedef struct {
    char     name[TASK_NAME_LEN];
    uint32_t start_byte;    /* 该 app 在 apps.img 内的【字节偏移】 */
    uint32_t byte_size;     /* 该 app 的【字节数】（各 PT_LOAD 段 p_filesz 之和） */
    uint32_t vaddr_entry;   /* 内存入口地址 */
    uint32_t mem_size;      /* 该 app 的【内存大小】（各 PT_LOAD 段 p_memsz 之和） */
} task_info_t;
static task_info_t taskinfo[TASK_MAXNUM];

/* AppInfo 区的字节数（32 × 32 = 1024） */
#define APPINFO_BYTES (TASK_MAXNUM * sizeof(task_info_t))

/* 镜像信息区（位于第 0 扇区尾部，任务5 起全部按【字节】语义）：
 *   0x1f0 : AppInfo 起始字节偏移（4 字节）
 *   0x1f4 : apps 区起始字节偏移（4 字节，= kernel.img 的总字节数）
 *   0x1f8 : 用户程序数目（2 字节）
 *   0x1fc : 内核扇区数（2 字节，bootblock 用 lh 读，官方推荐约定保持不变）
 *   0x1fe : 签名 0x55 0xaa（2 字节）
 */
#define IMG_APPINFO_OFF_LOC 0x1f0
#define IMG_APPS_OFF_LOC    0x1f4
#define IMG_APP_NUM_LOC     0x1f8

/* structure to store command line options */
static struct {
    int vm;
    int extended;
} options;

/* prototypes of local functions */
static void create_image(int nfiles, char *files[]);
static void error(char *fmt, ...);
static void read_ehdr(Elf64_Ehdr *ehdr, FILE *fp);
static void read_phdr(Elf64_Phdr *phdr, FILE *fp, int ph, Elf64_Ehdr ehdr);
static uint32_t get_filesz(Elf64_Phdr phdr);
static uint32_t get_memsz(Elf64_Phdr phdr);
static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr);
static void write_padding(FILE *img, int *phyaddr, int new_phyaddr);
static void write_img_info(uint32_t appinfo_off, uint32_t apps_off,
                           uint16_t ksectors, short tasknum, FILE *img);
static void write_appinfo(int appnum, int appinfo_offset, FILE *img);

int main(int argc, char **argv)
{
    char *progname = argv[0];

    /* process command line options */
    options.vm = 0;
    options.extended = 0;
    while ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == '-')) {
        char *option = &argv[1][2];

        if (strcmp(option, "vm") == 0) {
            options.vm = 1;
        } else if (strcmp(option, "extended") == 0) {
            options.extended = 1;
        } else {
            error("%s: invalid option\nusage: %s %s\n", progname,
                  progname, ARGS);
        }
        argc--;
        argv++;
    }
    if (options.vm == 1) {
        error("%s: option --vm not implemented\n", progname);
    }
    if (argc < 3) {
        /* at least 3 args (createimage bootblock main) */
        error("usage: %s %s\n", progname, ARGS);
    }
    create_image(argc - 1, argv + 1);
    return 0;
}

/* 预扫描：返回 ELF 中所有 PT_LOAD 段的 p_filesz 之和，并输出入口地址
 * 与 p_memsz 之和（P4 加载器要用 mem_size 建页表/清 BSS）。 */
static uint32_t scan_elf(const char *path, uint64_t *entry_out,
                         uint32_t *memsz_out)
{
    FILE *fp = fopen(path, "r");
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;
    uint32_t filesz_sum = 0;
    uint32_t memsz_sum = 0;

    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    *entry_out = ehdr.e_entry;

    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type != PT_LOAD)
            continue;
        filesz_sum += get_filesz(phdr);
        memsz_sum += get_memsz(phdr);
    }
    fclose(fp);
    if (memsz_out != NULL)
        *memsz_out = memsz_sum;
    return filesz_sum;
}

static void create_image(int nfiles, char *files[])
{
    int tasknum = nfiles - 2;
    int phyaddr = 0;
    FILE *img = NULL, *aimg = NULL;
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;

    /* 每个文件的布局信息（顺序：files[0]=bootblock, files[1]=main, files[2..]=app） */
    uint32_t used_bytes[TASK_MAXNUM + 2];
    uint32_t mem_bytes[TASK_MAXNUM + 2];
    uint64_t entry_addr[TASK_MAXNUM + 2];

    /* 预扫描一遍文件，得到每个文件实际会写进镜像的字节数 */
    for (int fidx = 0; fidx < nfiles; ++fidx)
        used_bytes[fidx] = scan_elf(files[fidx], &entry_addr[fidx],
                                    &mem_bytes[fidx]);

    /* ---- 任务5 双镜像布局：镜像内部按【字节】紧密排列（不补到扇区） ----
     *
     * kernel.img:
     *   字节 0 .. 511        bootblock（补 0 到 512 —— 全镜像唯一强制的补零：
     *                        · 0x1fc/0x1fe 两个字段就住在第 0 扇区最后 4 字节，
     *                          内核若从 130 开始会被这两个字段改坏；
     *                        · u-boot 也是按块读第 0 扇区并跳到 0x50200000）
     *   字节 512 ..          内核（KB = 内核字节数，**不补**）
     *   紧接其后             AppInfo（APPINFO_BYTES = 1024 字节，**不补**）
     *   => kernel.img 总字节 = 512 + KB + 1024
     *
     * apps.img:
     *   各 app 首尾相接：app i 从"app i-1 的最后一个字节 + 1"开始（**不补**）
     *   末尾补若干 0，只为让 kernel.img + apps.img 落在整扇区上（块设备边界）
     */
    uint32_t kernel_bytes   = used_bytes[1];
    uint32_t kernel_sectors = NBYTES2SEC(kernel_bytes);   /* 仍给 bootblock 读内核用 */
    uint32_t appinfo_off    = SECTOR_SIZE + kernel_bytes;  /* AppInfo 起始字节偏移 */
    uint32_t apps_base_off  = appinfo_off + APPINFO_BYTES; /* apps 区起始字节偏移 */

    uint32_t app_rel_byte[TASK_MAXNUM];   /* apps.img 内【字节偏移】 */
    uint32_t cur = 0;
    for (int i = 0; i < tasknum; ++i) {
        app_rel_byte[i] = cur;
        cur += used_bytes[2 + i];          /* ← 字节累加，不再是 NBYTES2SEC */
    }
    uint32_t apps_bytes  = cur;
    uint32_t total_bytes = apps_base_off + apps_bytes;
    uint32_t tail_pad    = (SECTOR_SIZE - (total_bytes % SECTOR_SIZE)) % SECTOR_SIZE;

    printf("===== 任务5 双镜像布局（字节级紧密） =====\n");

    /* 填充 taskinfo[]：start_byte / byte_size 一律为【字节】语义 */
    for (int i = 0; i < tasknum; ++i) {
        if (strlen(files[2 + i]) >= TASK_NAME_LEN) {
            error("task name '%s' too long (>= %d)\n", files[2 + i],
                  TASK_NAME_LEN);
        }
        strcpy(taskinfo[i].name, files[2 + i]);
        taskinfo[i].start_byte  = app_rel_byte[i];
        taskinfo[i].byte_size   = used_bytes[2 + i];
        taskinfo[i].vaddr_entry = (uint32_t)entry_addr[2 + i];
        taskinfo[i].mem_size    = mem_bytes[2 + i];
    }
    for (int i = tasknum; i < TASK_MAXNUM; ++i)
        memset(&taskinfo[i], 0, sizeof(task_info_t));

    /* ========== kernel.img ========== */
    img = fopen(KERNEL_IMAGE_FILE, "w");
    assert(img != NULL);

    /* 字节 0..511：bootblock（唯一强制补零处） */
    FILE *fp = fopen(files[0], "r");
    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    printf("0x%04lx: %-8s byte %5d .. %5d  (%d bytes → 补到 512)\n",
           ehdr.e_entry, files[0], 0, SECTOR_SIZE - 1, used_bytes[0]);
    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type != PT_LOAD)
            continue;
        write_segment(phdr, fp, img, &phyaddr);
    }
    fclose(fp);
    if (phyaddr > SECTOR_SIZE) {
        error("bootblock (%d bytes) exceeds one sector (%d bytes)\n",
              phyaddr, SECTOR_SIZE);
    }
    write_padding(img, &phyaddr, SECTOR_SIZE);

    /* 字节 512..：内核（不补） */
    fp = fopen(files[1], "r");
    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    printf("0x%04lx: %-8s byte %5d .. %5d  (%d bytes, %d sectors)\n",
           ehdr.e_entry, files[1], SECTOR_SIZE,
           SECTOR_SIZE + (int)kernel_bytes - 1, kernel_bytes, kernel_sectors);
    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type != PT_LOAD)
            continue;
        write_segment(phdr, fp, img, &phyaddr);
    }
    fclose(fp);
    if ((uint32_t)phyaddr != appinfo_off)
        error("kernel layout mismatch: expected %d bytes, wrote %d\n",
              appinfo_off, phyaddr);

    /* 紧接内核：AppInfo（不补） */
    printf("-APPINFO--- %-8s byte %5d .. %5d  (%d bytes)\n",
           "appinfo", appinfo_off, apps_base_off - 1, (int)APPINFO_BYTES);
    write_appinfo(tasknum, appinfo_off, img);
    /* fwrite 不经过 phyaddr：手工同步到"AppInfo 写完"的位置 */
    phyaddr = apps_base_off;

    /* ---- 镜像信息字段（0x1f0/0x1f4/0x1f8/0x1fc/0x1fe） ---- */
    write_img_info(appinfo_off, apps_base_off, (uint16_t)kernel_sectors,
                   (short)tasknum, img);

    /* kernel.img 大小自检 */
    if ((uint32_t)phyaddr != apps_base_off)
        error("kernel.img layout mismatch: expected %d bytes, wrote %d\n",
              apps_base_off, phyaddr);
    fclose(img);

    /* ========== apps.img ========== */
    aimg = fopen(APPS_IMAGE_FILE, "w");
    assert(aimg != NULL);
    phyaddr = 0;

    for (int i = 0; i < tasknum; ++i) {
        fp = fopen(files[2 + i], "r");
        assert(fp != NULL);
        read_ehdr(&ehdr, fp);
        printf("0x%04lx: %-8s byte %5d .. %5d  (%d bytes)\n",
               ehdr.e_entry, files[2 + i], app_rel_byte[i],
               app_rel_byte[i] + (int)taskinfo[i].byte_size - 1,
               taskinfo[i].byte_size);
        for (int ph = 0; ph < ehdr.e_phnum; ph++) {
            read_phdr(&phdr, fp, ph, ehdr);
            if (phdr.p_type != PT_LOAD)
                continue;
            write_segment(phdr, fp, aimg, &phyaddr);
        }
        fclose(fp);
        /* 注意：这里【不补零】——下一个 app 紧贴本 app 的最后一个字节 */
    }

    /* apps.img 大小自检 + 末尾补零（仅为整张 image 落在整扇区上） */
    if ((uint32_t)phyaddr != apps_bytes)
        error("apps.img layout mismatch: expected %d bytes, wrote %d\n",
              apps_bytes, phyaddr);
    write_padding(aimg, &phyaddr, apps_bytes + tail_pad);
    fclose(aimg);

    printf("\n===== 布局核算（字节级紧密） =====\n");
    printf("[layout] kernel.img : %5d bytes = 512(boot) + %d(kernel) + %d(appinfo)\n",
           apps_base_off, kernel_bytes, (int)APPINFO_BYTES);
    printf("[layout] apps.img   : %5d bytes = %d(apps) + %d(尾部对齐)\n",
           apps_bytes + tail_pad, apps_bytes, tail_pad);
    printf("[layout] appinfo 偏移 : %d (byte)\n", appinfo_off);
    printf("[layout] apps 区偏移  : %d (byte) —— 内核字节数 %d，二者【不补到扇区】\n",
           apps_base_off, kernel_bytes);
    printf("[layout] total      : %d bytes = %d sectors（末尾补 %d 字节）\n",
           total_bytes + tail_pad, (total_bytes + tail_pad) / SECTOR_SIZE, tail_pad);
    printf("[layout] app#1 相对字节 : %d（apps.img 内，字节级无空泡）\n",
           tasknum > 0 ? (int)app_rel_byte[0] : -1);
}

static void read_ehdr(Elf64_Ehdr * ehdr, FILE * fp)
{
    int ret;

    ret = fread(ehdr, sizeof(*ehdr), 1, fp);
    assert(ret == 1);
    assert(ehdr->e_ident[EI_MAG1] == 'E');
    assert(ehdr->e_ident[EI_MAG2] == 'L');
    assert(ehdr->e_ident[EI_MAG3] == 'F');
}

static void read_phdr(Elf64_Phdr * phdr, FILE * fp, int ph,
                      Elf64_Ehdr ehdr)
{
    int ret;

    fseek(fp, ehdr.e_phoff + ph * ehdr.e_phentsize, SEEK_SET);
    ret = fread(phdr, sizeof(*phdr), 1, fp);
    assert(ret == 1);
    if (options.extended == 1) {
        printf("\tsegment %d\n", ph);
        printf("\t\toffset 0x%04lx", phdr->p_offset);
        printf("\t\tvaddr 0x%04lx\n", phdr->p_vaddr);
        printf("\t\tfilesz 0x%04lx", phdr->p_filesz);
        printf("\t\tmemsz 0x%04lx\n", phdr->p_memsz);
    }
}

static uint32_t get_filesz(Elf64_Phdr phdr)
{
    return phdr.p_filesz;
}

static uint32_t get_memsz(Elf64_Phdr phdr)
{
    return phdr.p_memsz;
}

static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr)
{
    if (phdr.p_memsz != 0 && phdr.p_type == PT_LOAD) {
        /* write the segment itself */
        /* NOTE: expansion of .bss should be done by kernel or runtime env! */
        if (options.extended == 1) {
            printf("\t\twriting 0x%04lx bytes\n", phdr.p_filesz);
        }
        fseek(fp, phdr.p_offset, SEEK_SET);
        while (phdr.p_filesz-- > 0) {
            fputc(fgetc(fp), img);
            (*phyaddr)++;
        }
    }
}

static void write_padding(FILE *img, int *phyaddr, int new_phyaddr)
{
    if (options.extended == 1 && *phyaddr < new_phyaddr) {
        printf("\t\twrite 0x%04x bytes for padding\n", new_phyaddr - *phyaddr);
    }

    while (*phyaddr < new_phyaddr) {
        fputc(0, img);
        (*phyaddr)++;
    }
}

/* 任务4：把 task_info_t 数组写到 AppInfo 扇区。
 *   整个数组（TASK_MAXNUM 个槽位）都写，未用的槽位在调用前已清 0。 */
static void write_appinfo(int appnum, int appinfo_offset, FILE *img)
{
    fseek(img, appinfo_offset, SEEK_SET);
    fwrite(taskinfo, sizeof(task_info_t), TASK_MAXNUM, img);
    if (appnum > 0) {
        printf("[appinfo] %d task(s) @ 0x%04x\n", appnum, appinfo_offset);
    }
}

static void write_img_info(uint32_t appinfo_off, uint32_t apps_off,
                           uint16_t ksectors, short tasknum, FILE * img)
{
    /* 任务5：把布局信息写进 kernel.img 第 0 扇区尾部（全部按【字节】语义）
     *   0x1f0 (4字节) : AppInfo 起始字节偏移
     *   0x1f4 (4字节) : apps 区起始字节偏移
     *   0x1f8 (2字节) : 用户程序数目
     *   0x1fc (2字节) : 内核扇区数（bootblock 用 lh 读，官方推荐约定）
     *   0x1fe (2字节) : 签名 0x55 0xaa
     * 小端写入；4 字节字段拆成 4 次 fputc。 */
    fseek(img, IMG_APPINFO_OFF_LOC, SEEK_SET);
    for (int i = 0; i < 4; ++i)
        fputc((appinfo_off >> (8 * i)) & 0xff, img);

    fseek(img, IMG_APPS_OFF_LOC, SEEK_SET);
    for (int i = 0; i < 4; ++i)
        fputc((apps_off >> (8 * i)) & 0xff, img);

    fseek(img, IMG_APP_NUM_LOC, SEEK_SET);
    fputc(tasknum & 0xff, img);
    fputc((tasknum >> 8) & 0xff, img);

    fseek(img, OS_SIZE_LOC, SEEK_SET);
    fputc(ksectors & 0xff, img);
    fputc((ksectors >> 8) & 0xff, img);

    fseek(img, BOOT_LOADER_SIG_OFFSET, SEEK_SET);
    fputc(BOOT_LOADER_SIG_1, img);
    fputc(BOOT_LOADER_SIG_2, img);

    printf("\n[img info] appinfo offset : %u @ 0x%03x\n", appinfo_off,
           IMG_APPINFO_OFF_LOC);
    printf("[img info] apps offset    : %u @ 0x%03x\n", apps_off,
           IMG_APPS_OFF_LOC);
    printf("[img info] app number     : %d @ 0x%03x\n", tasknum, IMG_APP_NUM_LOC);
    printf("[img info] kernel sectors : %d @ 0x%03x\n", ksectors, OS_SIZE_LOC);
    printf("[img info] boot signature : 0x%02x 0x%02x @ 0x%03x\n",
           BOOT_LOADER_SIG_1, BOOT_LOADER_SIG_2, BOOT_LOADER_SIG_OFFSET);
}

/* print an error message and exit */
static void error(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (errno != 0) {
        perror(NULL);
    }
    exit(EXIT_FAILURE);
}