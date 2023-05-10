CROSS_COMPILE ?= arm-none-eabi-
BMP_BOOTLOADER ?=
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-DSTM32F4 -I../libopencm3/include \
	-Iplatforms/common/stm32

LINKER_SCRIPT=platforms/f4discovery/f4discovery.ld

LDFLAGS_BOOT = -lopencm3_stm32f4 \
	-Wl,-T,$(LINKER_SCRIPT) -nostartfiles -lc -lnosys \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../libopencm3/lib

ifeq ($(BMP_BOOTLOADER), 1)
$(info  Load address 0x08004000 for BMPBootloader)
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8004000
CFLAGS += -DDFU_SERIAL_LENGTH=9
else
LDFLAGS += $(LDFLAGS_BOOT)
CFLAGS += -DDFU_SERIAL_LENGTH=13
endif

VPATH += platforms/common/stm32

SRC +=               \
	platform.c \
	traceswodecode.c \
	traceswo.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\

ifneq ($(BMP_BOOTLOADER), 1)
all:	blackmagic.bin
else
all:	blackmagic.bin  blackmagic_dfu.bin blackmagic_dfu.hex
blackmagic_dfu.elf: usbdfu.o dfucore.o dfu_f4.o serialno.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS_BOOT)
endif

host_clean:
	-$(Q)$(RM) blackmagic.bin
