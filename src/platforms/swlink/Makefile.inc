CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -mcpu=cortex-m3 -mthumb \
	-DSTM32F1  -DDFU_SERIAL_LENGTH=9 -I../libopencm3/include \
	-I platforms/common/stm32
LDFLAGS_BOOT := $(LDFLAGS) --specs=nano.specs -lopencm3_stm32f1 \
	-Wl,-T,platforms/swlink/swlink.ld -nostartfiles -lc\
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m3 -Wl,-gc-sections \
	-L../libopencm3/lib
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8002000

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

VPATH += platforms/common/stm32

SRC +=          \
	platform.c \
	serialno.c	\
	timing.c	\
	timing_stm32.c	\
	traceswodecode.c	\
	traceswoasync.c	\
	platform_common.c \

all:	blackmagic.bin blackmagic_dfu.bin blackmagic_dfu.hex

blackmagic_dfu.elf: usbdfu.o dfucore.o dfu_f1.o platform_common.o serialno.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS_BOOT)

host_clean:
	-$(Q)$(RM) blackmagic.bin blackmagic_dfu blackmagic_dfu.bin blackmagic_dfu.hex
