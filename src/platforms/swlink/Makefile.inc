CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

OPT_FLAGS = -Os
CFLAGS += -mcpu=cortex-m3 -mthumb \
	-DSTM32F1 -DDISCOVERY_SWLINK -Dsystick_reload=$(SYSTICK_RELOAD_VALUE) -I../libopencm3/include \
	-I platforms/stm32
LDFLAGS_BOOT := $(LDFLAGS) --specs=nano.specs -lopencm3_stm32f1 \
	-Wl,-T,platforms/stm32/stlink.ld -nostartfiles -lc\
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m3 -Wl,-gc-sections \
	-L../libopencm3/lib
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8002000

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

VPATH += platforms/stm32

SRC += 	cdcacm.c	\
	usbuart.c 	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\
	traceswoasync.c	\
	platform_common.c \

all:	blackmagic.bin blackmagic_dfu.bin blackmagic_dfu.hex dfu_upgrade.bin dfu_upgrade.hex

blackmagic_dfu: usbdfu.o dfucore.o dfu_f1.o platform_common.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS_BOOT)

dfu_upgrade: dfu_upgrade.o dfucore.o dfu_f1.o platform_common.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS)

host_clean:
	-$(Q)$(RM) blackmagic.bin blackmagic_dfu blackmagic_dfu.bin blackmagic_dfu.hex

