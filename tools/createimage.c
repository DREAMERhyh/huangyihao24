#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_FILE "./image"
#define ARGS "[--extended] [--vm] <bootblock> <executable-file> ..."

#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2)
#define BOOT_LOADER_SIG_1 0x55
#define BOOT_LOADER_SIG_2 0xaa

#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* 任务4 的 task_info_t，与 include/os/task.h 完全一致（必须保持同步，
 * 大小 16+4+4+4 = 28 字节，4 字节对齐；TASK_MAXNUM * 28 = 448 < 512，
 * 所以 AppInfo 区固定 = 1 扇区）。 */
#define TASK_NAME_LEN 16
#define TASK_MAXNUM   16
typedef struct {
    char     name[TASK_NAME_LEN];
    uint32_t start_sector;
    uint32_t sector_num;
    uint32_t vaddr_entry;
} task_info_t;
static task_info_t taskinfo[TASK_MAXNUM];

/* 镜像信息区（位于第 0 扇区尾部）。用户程序数目 @0x1f8（避开官方签名 @0x1fe）。 */
#define IMG_APP_NUM_LOC (OS_SIZE_LOC - 4)

/* 任务4 镜像布局（紧凑排列，无固定空泡）：
 *
 *   扇区 0                bootblock
 *   扇区 1 .. K           内核，K = NBYTES2SEC(内核实际大小)
 *   扇区 K+1              AppInfo 区（task_info_t 数组，固定 1 扇区）
 *   扇区 K+2 ..           各 app 按实际大小紧凑排列（app i 紧贴 app i-1）
 *
 * bootblock 从第 0 扇区尾部读出 K 与 app 数目，把 AppInfo 扇区号(1+K)与
 * app 数目经寄存器传给内核（见 arch/riscv/boot/bootblock.S、head.S）。
 */
#define APPINFO_SEC_START(k) ((k) + 1)

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
static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr);
static void write_padding(FILE *img, int *phyaddr, int new_phyaddr);
static void write_img_info(uint16_t ksectors, short tasknum, FILE *img);
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

/* 预扫描：返回 ELF 中所有 PT_LOAD 段的 p_filesz 之和，并输出入口地址 */
static uint32_t scan_elf(const char *path, uint64_t *entry_out)
{
    FILE *fp = fopen(path, "r");
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;
    uint32_t filesz_sum = 0;

    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    *entry_out = ehdr.e_entry;

    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type != PT_LOAD)
            continue;
        filesz_sum += get_filesz(phdr);
    }
    fclose(fp);
    return filesz_sum;
}

static void create_image(int nfiles, char *files[])
{
    int tasknum = nfiles - 2;
    int phyaddr = 0;
    FILE *img = NULL;
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;

    /* 每个文件的布局信息 */
    uint32_t used_bytes[TASK_MAXNUM + 2];   /* 顺序：bootblock, main, app... */
    uint64_t entry_addr[TASK_MAXNUM + 2];

    /* 预扫描一遍文件，得到每个文件实际会写进镜像的字节数 */
    for (int fidx = 0; fidx < nfiles; ++fidx)
        used_bytes[fidx] = scan_elf(files[fidx], &entry_addr[fidx]);

    /* ---- 布局计算（全部按扇区） ---- */
    int boot_sec        = 0;
    int kernel_sec      = 1;
    int kernel_sectors  = NBYTES2SEC(used_bytes[1]);            /* 内核 */
    int appinfo_sec     = APPINFO_SEC_START(kernel_sectors);    /* 1+K */
    int appinfo_sectors = NBYTES2SEC(TASK_MAXNUM * sizeof(task_info_t)); /* 1 */
    int app_sec[TASK_MAXNUM];
    int cur = appinfo_sec + appinfo_sectors;

    for (int i = 0; i < tasknum; ++i) {
        app_sec[i] = cur;
        cur += NBYTES2SEC(used_bytes[2 + i]);
    }
    int total_sectors = cur;

    printf("===== 镜像布局（紧凑排列） =====\n");

    /* ---- 写镜像 ---- */
    img = fopen(IMAGE_FILE, "w");
    assert(img != NULL);

    /* 第 0 扇区：bootblock */
    FILE *fp = fopen(files[0], "r");
    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    printf("0x%04lx: %-8s sec %2d .. %2d  (%d bytes, 1 sector)\n",
           ehdr.e_entry, files[0], boot_sec, boot_sec,
           used_bytes[0]);
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

    /* 扇区 1..K：内核 */
    fp = fopen(files[1], "r");
    assert(fp != NULL);
    read_ehdr(&ehdr, fp);
    printf("0x%04lx: %-8s sec %2d .. %2d  (%d bytes, %d sectors)\n",
           ehdr.e_entry, files[1], kernel_sec,
           kernel_sec + kernel_sectors - 1, used_bytes[1],
           kernel_sectors);
    for (int ph = 0; ph < ehdr.e_phnum; ph++) {
        read_phdr(&phdr, fp, ph, ehdr);
        if (phdr.p_type != PT_LOAD)
            continue;
        write_segment(phdr, fp, img, &phyaddr);
    }
    fclose(fp);
    write_padding(img, &phyaddr, (1 + kernel_sectors) * SECTOR_SIZE);

    /* 扇区 K+1：AppInfo 区（task_info_t 数组，槽位清零） */
    printf("-APPINFO--- %-8s sec %2d .. %2d  (%d bytes, %d sectors)\n",
           "appinfo", appinfo_sec,
           appinfo_sec + appinfo_sectors - 1,
           (int)(TASK_MAXNUM * sizeof(task_info_t)), appinfo_sectors);
    for (int i = 0; i < tasknum; ++i) {
        /* 文件名可能很长：超过槽位就报错，绝不截断 */
        if (strlen(files[2 + i]) >= TASK_NAME_LEN) {
            error("task name '%s' too long (>= %d)\n", files[2 + i],
                  TASK_NAME_LEN);
        }
        strcpy(taskinfo[i].name, files[2 + i]);
        taskinfo[i].start_sector = app_sec[i];
        taskinfo[i].sector_num   = NBYTES2SEC(used_bytes[2 + i]);
        taskinfo[i].vaddr_entry  = (uint32_t)entry_addr[2 + i];
    }
    /* 剩余槽位清零（未用槽位的 name[0]=='\0'，内核靠它识别空槽） */
    for (int i = tasknum; i < TASK_MAXNUM; ++i)
        memset(&taskinfo[i], 0, sizeof(task_info_t));
    write_appinfo(tasknum, appinfo_sec * SECTOR_SIZE, img);
    /* fwrite 不经过 phyaddr 计数：同步一次，再把该扇区尾部(448B 之后)清零 */
    phyaddr = appinfo_sec * SECTOR_SIZE +
              (int)(TASK_MAXNUM * sizeof(task_info_t));
    write_padding(img, &phyaddr, (appinfo_sec + appinfo_sectors) * SECTOR_SIZE);

    /* 扇区 K+2 起：各 app 紧凑排列（无固定空泡） */
    for (int i = 0; i < tasknum; ++i) {
        fp = fopen(files[2 + i], "r");
        assert(fp != NULL);
        read_ehdr(&ehdr, fp);
        printf("0x%04lx: %-8s sec %2d .. %2d  (%d bytes, %d sectors)\n",
               ehdr.e_entry, files[2 + i], app_sec[i],
               app_sec[i] + taskinfo[i].sector_num - 1,
               used_bytes[2 + i], taskinfo[i].sector_num);
        for (int ph = 0; ph < ehdr.e_phnum; ph++) {
            read_phdr(&phdr, fp, ph, ehdr);
            if (phdr.p_type != PT_LOAD)
                continue;
            write_segment(phdr, fp, img, &phyaddr);
        }
        fclose(fp);
        write_padding(img, &phyaddr, (app_sec[i] + taskinfo[i].sector_num) *
                                     SECTOR_SIZE);
    }

    /* 布局自检：按公式核算总扇区数，与文件实际大小必须一致 */
    int actual_total = phyaddr / SECTOR_SIZE;
    if (actual_total != total_sectors) {
        error("layout mismatch: computed %d sectors, wrote %d\n",
              total_sectors, actual_total);
    }

    /* ---- 镜像信息字段 ---- */
    write_img_info((uint16_t)kernel_sectors, (short)tasknum, img);

    printf("\n===== 布局核算 =====");
    printf("\n[layout] total sectors : %d = 1(boot) + %d(kernel) + %d(appinfo)"
           " + %d(apps)\n",
           total_sectors, kernel_sectors, appinfo_sectors,
           total_sectors - 1 - kernel_sectors - appinfo_sectors);
    printf("[layout] appinfo sector : %d (= 1 + kernel_sectors)\n",
           appinfo_sec);
    printf("[layout] app#1 sector   : %d (= 1 + kernel_sectors + %d)"
           "  (无固定空泡)\n",
           tasknum > 0 ? app_sec[0] : -1, appinfo_sectors);

    fclose(img);
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

static void write_img_info(uint16_t ksectors, short tasknum, FILE * img)
{
    /* 任务4：内核扇区数写"实际占用"（紧凑布局下 bootblock 精确读内核），
     * app 数目 @0x1f8，官方签名 @0x1fe 不动。 */
    fseek(img, IMG_APP_NUM_LOC, SEEK_SET);
    fputc(tasknum & 0xff, img);
    fputc((tasknum >> 8) & 0xff, img);

    fseek(img, OS_SIZE_LOC, SEEK_SET);
    fputc(ksectors & 0xff, img);
    fputc((ksectors >> 8) & 0xff, img);

    fseek(img, BOOT_LOADER_SIG_OFFSET, SEEK_SET);
    fputc(BOOT_LOADER_SIG_1, img);
    fputc(BOOT_LOADER_SIG_2, img);

    printf("\n[img info] kernel sectors : %d @ 0x%03x\n", ksectors, OS_SIZE_LOC);
    printf("[img info] app number     : %d @ 0x%03x\n", tasknum, IMG_APP_NUM_LOC);
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