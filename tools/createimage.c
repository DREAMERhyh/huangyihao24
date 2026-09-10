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

/* 任务3 的镜像布局：bootblock 独占第 0 扇区，内核与每个 app 各占固定扇区数。
 * 注意：这两个宏必须与 include/os/task.h 里的同名宏保持一致 */
#define KERNEL_FIXED_SECTORS 32
#define APP_FIXED_SECTORS    15

/* 镜像信息区（位于第 0 扇区尾部）。用户程序数目放在 0x1f8 而不是 0x1fe，
 * 因为 0x1fe/0x1ff 已被官方签名 0x55 0xaa 占用 */
#define IMG_APP_NUM_LOC      (OS_SIZE_LOC - 4)

/* TODO: [p1-task4] design your own task_info_t */
typedef struct {

} task_info_t;

#define TASK_MAXNUM 16
static task_info_t taskinfo[TASK_MAXNUM];

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
static uint64_t get_entrypoint(Elf64_Ehdr ehdr);
static uint32_t get_filesz(Elf64_Phdr phdr);
static uint32_t get_memsz(Elf64_Phdr phdr);
static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr);
static void write_padding(FILE *img, int *phyaddr, int new_phyaddr);
static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE *img);

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

/* TODO: [p1-task4] assign your task_info_t somewhere in 'create_image' */
static void create_image(int nfiles, char *files[])
{
    int tasknum = nfiles - 2;
    int nbytes_kernel = 0;
    int phyaddr = 0;
    FILE *fp = NULL, *img = NULL;
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;

    /* open the image file */
    img = fopen(IMAGE_FILE, "w");
    assert(img != NULL);

    /* for each input file */
    for (int fidx = 0; fidx < nfiles; ++fidx) {

        int taskidx = fidx - 2;
        int file_start = phyaddr;   /* 本文件写入镜像的起始字节偏移 */

        /* open input file */
        fp = fopen(*files, "r");
        assert(fp != NULL);

        /* read ELF header */
        read_ehdr(&ehdr, fp);
        printf("0x%04lx: %s\n", ehdr.e_entry, *files);

        /* for each program header */
        for (int ph = 0; ph < ehdr.e_phnum; ph++) {

            /* read program header */
            read_phdr(&phdr, fp, ph, ehdr);

            if (phdr.p_type != PT_LOAD) continue;

            /* write segment to the image */
            write_segment(phdr, fp, img, &phyaddr);

            /* update nbytes_kernel */
            if (strcmp(*files, "main") == 0) {
                nbytes_kernel += get_filesz(phdr);
            }
        }

        /* write padding bytes */
        /**
         * 任务3：bootblock 独占第 0 扇区，内核与每个 app 各占固定扇区数，
         * 这样 loader 可以直接用 taskid 算出扇区号。
         * TODO: [p1-task4] only padding bootblock is allowed!
         */
        int allocated_sectors = 1;

        if (strcmp(*files, "main") == 0) {
            allocated_sectors = KERNEL_FIXED_SECTORS;
        } else if (taskidx >= 0) {
            allocated_sectors = APP_FIXED_SECTORS;
        }

        /* 该文件实际占用的扇区数（只统计真正写进镜像的字节） */
        int actual_sectors = NBYTES2SEC(phyaddr - file_start);

        printf("\t\tactual %d sector(s) / allocated %d sector(s)\n",
               actual_sectors, allocated_sectors);

        if (actual_sectors > allocated_sectors) {
            error("%s: need %d sector(s), but only %d allocated\n",
                  *files, actual_sectors, allocated_sectors);
        }

        write_padding(img, &phyaddr, file_start + allocated_sectors * SECTOR_SIZE);

        fclose(fp);
        files++;
    }
    write_img_info(nbytes_kernel, taskinfo, tasknum, img);

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

static uint64_t get_entrypoint(Elf64_Ehdr ehdr)
{
    return ehdr.e_entry;
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

static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE * img)
{
    // 任务3：把镜像信息写进第 0 扇区（bootblock 扇区）的尾部
    //
    //   偏移 0x1f8 : 用户程序数目（2 字节，小端）
    //   偏移 0x1fc : 内核占用的扇区数（2 字节，小端）—— bootblock 用 lh 从这里读
    //   偏移 0x1fe : 官方签名 0x55 0xaa
    //
    // 内核扇区数写"实际占用"而不是"分配"，因为 bootblock 按此值精确读入内核；
    // Task 4 改成紧凑排列后同样成立。
    // 用户程序数目放在 0x1f8 而不是 0x1fe，是为了避开官方签名。
    // TODO: [p1-task4] write task_info_t array (AppInfo) for loading by name

    uint16_t ksectors = (uint16_t)NBYTES2SEC(nbytes_kernel);

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
