# -----------------------------------------------------------------------
# Project Information
# -----------------------------------------------------------------------

PROJECT_IDX	= 4

# -----------------------------------------------------------------------
# Host Linux Variables
# -----------------------------------------------------------------------

SHELL       = /bin/sh
DISK        = /dev/sdb
TTYUSB1     = /dev/ttyUSB1
DIR_OSLAB   = $(HOME)/OSLab-RISC-V
DIR_QEMU    = $(DIR_OSLAB)/qemu
DIR_UBOOT   = $(DIR_OSLAB)/u-boot

# -----------------------------------------------------------------------
# Build and Debug Tools
# -----------------------------------------------------------------------

HOST_CC         = gcc
CROSS_PREFIX    = riscv64-unknown-linux-gnu-
CC              = $(CROSS_PREFIX)gcc
AR              = $(CROSS_PREFIX)ar
OBJDUMP         = $(CROSS_PREFIX)objdump
GDB             = $(CROSS_PREFIX)gdb
QEMU            = $(DIR_QEMU)/riscv64-softmmu/qemu-system-riscv64
UBOOT           = $(DIR_UBOOT)/u-boot
MINICOM         = minicom

# -----------------------------------------------------------------------
# Build/Debug Flags and Variables
# -----------------------------------------------------------------------

CFLAGS          = -O2 -std=gnu11 -fno-builtin -nostdlib -nostdinc -Wall -mcmodel=medany -ggdb3 -fno-pie

# -----------------------------------------------------------------------
# 任务5：细粒度锁开关（两份内核镜像）
#   make                                 → 大内核锁版（默认）image
#   make FINE_LOCK=1                     → 细粒度锁版 image
#   ★ 切换开关必须先 make clean：.o 的依赖不包含 CFLAGS，增量 make
#     不会因开关变化重编（会拿着旧 .o 链出错误版本的 image）。
#   两版镜像的产出与对比流程见 .scratch/p3-scenarios.md §3。
# -----------------------------------------------------------------------
FINE_LOCK       ?= 0
ifeq ($(FINE_LOCK),1)
CFLAGS          += -DFINE_GRAIN_LOCK
endif

BOOT_INCLUDE    = -I$(DIR_ARCH)/include
BOOT_CFLAGS     = $(CFLAGS) $(BOOT_INCLUDE) -no-pie -Wl,--build-id=none -Wl,--defsym=TEXT_START=$(BOOTLOADER_ENTRYPOINT) -T riscv.lds

KERNEL_INCLUDE  = -I$(DIR_ARCH)/include -Iinclude -Idrivers
KERNEL_CFLAGS   = $(CFLAGS) $(KERNEL_INCLUDE) -no-pie -Wl,--build-id=none -Wl,--defsym=TEXT_START=$(KERNEL_ENTRYPOINT) -T riscv.lds

USER_INCLUDE    = -I$(DIR_TINYLIBC)/include
USER_CFLAGS     = $(CFLAGS) $(USER_INCLUDE)
USER_LDFLAGS    = -L$(DIR_BUILD) -ltinyc -no-pie -Wl,--build-id=none

QEMU_LOG_FILE   = $(DIR_OSLAB)/oslab-log.txt
QEMU_OPTS       = -nographic -machine virt -m 256M -kernel $(UBOOT) -bios none \
                     -drive if=none,format=raw,id=image,file=${ELF_IMAGE} \
                     -device virtio-blk-device,drive=image \
                     -monitor telnet::45454,server,nowait -serial mon:stdio \
                     -D $(QEMU_LOG_FILE) -d oslab
QEMU_DEBUG_OPT  = -s -S
QEMU_SMP_OPT	= -smp 2

# -----------------------------------------------------------------------
# UCAS-OS Entrypoints and Variables
# -----------------------------------------------------------------------

DIR_ARCH        = ./arch/riscv
DIR_BUILD       = ./build
DIR_DRIVERS     = ./drivers
DIR_INIT        = ./init
DIR_KERNEL      = ./kernel
DIR_LIBS        = ./libs
DIR_TINYLIBC    = ./tiny_libc
DIR_TEST        = ./test
DIR_TEST_PROJ   = $(DIR_TEST)/test_project$(PROJECT_IDX)

BOOTLOADER_ENTRYPOINT   = 0x50200000
KERNEL_ENTRYPOINT       = 0xffffffc050202000
# P4 C-core：所有用户程序统一链接到 0x10000（虚拟地址空间由页表隔离）
USER_ENTRYPOINT         = 0x10000

# -----------------------------------------------------------------------
# UCAS-OS Kernel Source Files
# -----------------------------------------------------------------------

SRC_BOOT    = $(wildcard $(DIR_ARCH)/boot/*.S)
SRC_ARCH    = $(wildcard $(DIR_ARCH)/kernel/*.S)
SRC_BIOS    = $(wildcard $(DIR_ARCH)/bios/*.c)
SRC_DRIVER  = $(wildcard $(DIR_DRIVERS)/*.c)
SRC_INIT    = $(wildcard $(DIR_INIT)/*.c)
SRC_KERNEL  = $(wildcard $(DIR_KERNEL)/*/*.c)
SRC_LIBS    = $(wildcard $(DIR_LIBS)/*.c)
SRC_START   = $(wildcard $(DIR_ARCH)/kernel/*.c)

SRC_MAIN    = $(SRC_ARCH) $(SRC_START) $(SRC_INIT) $(SRC_BIOS) $(SRC_DRIVER) $(SRC_KERNEL) $(SRC_LIBS) 

ELF_BOOT    = $(DIR_BUILD)/bootblock
ELF_MAIN    = $(DIR_BUILD)/main
ELF_IMAGE   = $(DIR_BUILD)/image

# -----------------------------------------------------------------------
# UCAS-OS User Source Files
# -----------------------------------------------------------------------

SRC_CRT0    = $(wildcard $(DIR_ARCH)/crt0/*.S)
OBJ_CRT0    = $(DIR_BUILD)/$(notdir $(SRC_CRT0:.S=.o))

SRC_LIBC    = $(wildcard ./tiny_libc/*.c)
OBJ_LIBC    = $(patsubst %.c, %.o, $(foreach file, $(SRC_LIBC), $(DIR_BUILD)/$(notdir $(file))))
LIB_TINYC   = $(DIR_BUILD)/libtinyc.a

# P4：shell 在 test/ 下；P4 的所有测试程序都在 test/test_project4/。
# 所有 app 统一链接到 USER_ENTRYPOINT（0x10000），各自有独立页表，不再需要窗口。
SRC_SHELL   = $(DIR_TEST)/shell.c
SRC_USER    = $(sort $(SRC_SHELL) $(wildcard $(DIR_TEST_PROJ)/*.c))
ELF_USER    = $(patsubst %.c, %, $(foreach file, $(SRC_USER), $(DIR_BUILD)/$(notdir $(file))))

# -----------------------------------------------------------------------
# Host Linux Tools Source Files
# -----------------------------------------------------------------------

SRC_CREATEIMAGE = ./tools/createimage.c
ELF_CREATEIMAGE = $(DIR_BUILD)/$(notdir $(SRC_CREATEIMAGE:.c=))

# -----------------------------------------------------------------------
# Top-level Rules
# -----------------------------------------------------------------------

all: dirs elf image asm # floppy

dirs:
	@mkdir -p $(DIR_BUILD)

clean:
	rm -rf $(DIR_BUILD)

# 任务5：两张镜像分别写入 SD 卡（等价于单文件 image 的连续扇区），方案说明见
# log/task5-设计提案.md §4。本机无板卡、一切到 QEMU 层，采用方案 A（单 dd，
# 与 QEMU 用同一个 image，一致性验证最简单）。
# 方案 B（上板时切换）：kernel.img 写 $(DISK)3 首部；apps.img 写到偏移
#   (1 + 内核扇区数 + 1) 扇区，即 seek=((0x1fc 处读到的 K)+2)（dd 默认 bs=512）。
floppy:
	sudo fdisk -l $(DISK)
	sudo dd if=$(DIR_BUILD)/image of=$(DISK)3 conv=notrunc

asm: $(ELF_BOOT) $(ELF_MAIN) $(ELF_USER)
	for elffile in $^; do $(OBJDUMP) -d $$elffile > $(notdir $$elffile).txt; done

gdb:
	$(GDB) $(ELF_MAIN) -ex "target remote:1234"

run:
	$(QEMU) $(QEMU_OPTS)

run-smp:
	$(QEMU) $(QEMU_OPTS) $(QEMU_SMP_OPT)

debug:
	$(QEMU) $(QEMU_OPTS) $(QEMU_DEBUG_OPT)

debug-smp:
	$(QEMU) $(QEMU_OPTS) $(QEMU_SMP_OPT) $(QEMU_DEBUG_OPT)

minicom:
	sudo $(MINICOM) -D $(TTYUSB1)

.PHONY: all dirs clean floppy asm gdb run debug viewlog minicom

# -----------------------------------------------------------------------
# UCAS-OS Rules
# -----------------------------------------------------------------------

$(ELF_BOOT): $(SRC_BOOT) riscv.lds
	$(CC) $(BOOT_CFLAGS) -o $@ $(SRC_BOOT) -e main

$(ELF_MAIN): $(SRC_MAIN) riscv.lds
	$(CC) $(KERNEL_CFLAGS) -o $@ $(SRC_MAIN) -e _boot

$(OBJ_CRT0): $(SRC_CRT0)
	$(CC) $(USER_CFLAGS) -I$(DIR_ARCH)/include -c $< -o $@

$(LIB_TINYC): $(OBJ_LIBC)
	$(AR) rcs $@ $^

$(DIR_BUILD)/%.o: $(DIR_TINYLIBC)/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(DIR_BUILD)/%: $(DIR_TEST_PROJ)/%.c $(OBJ_CRT0) $(LIB_TINYC) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< $(USER_LDFLAGS) -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds

$(DIR_BUILD)/%: $(DIR_TEST)/%.c $(OBJ_CRT0) $(LIB_TINYC) riscv.lds
	$(CC) $(USER_CFLAGS) -o $@ $(OBJ_CRT0) $< $(USER_LDFLAGS) -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds

elf: $(ELF_BOOT) $(ELF_MAIN) $(LIB_TINYC) $(ELF_USER)

.PHONY: elf

# -----------------------------------------------------------------------
# Host Linux Rules
# -----------------------------------------------------------------------

$(ELF_CREATEIMAGE): $(SRC_CREATEIMAGE)
	$(HOST_CC) $(SRC_CREATEIMAGE) -o $@ -ggdb -Wall

# 任务5：createimage 产出 kernel.img 与 apps.img 两个文件；
# `cat` 拼接成 build/image（QEMU 用，等价于两文件分别写入 SD 卡连续扇区）。
# dirs 放普通依赖首位：make 按书写顺序构建（非并行），确保 build/ 先建好
#   ——createimage 的链接/bootblock 的输出都依赖 build/ 存在（make clean
#   后直接 make image 踩过 "No such file or directory"）。
# 调用与参数里显式排除 dirs/createimage（不用 $< 取首个依赖——dirs 进列表
#   后 $< 会变成 dirs，踩过 "跑成 ./dirs"）。
image: dirs $(ELF_CREATEIMAGE) $(ELF_BOOT) $(ELF_MAIN) $(ELF_USER)
	cd $(DIR_BUILD) && ./createimage --extended $(filter-out dirs createimage, $(^F))
	cd $(DIR_BUILD) && cat kernel.img apps.img > image
	# P4：QEMU/板上的 swap 区在 32MB 处，镜像后面补 64MB 零空间
	cd $(DIR_BUILD) && dd if=/dev/zero of=image oflag=append conv=notrunc bs=64M count=1

.PHONY: image
