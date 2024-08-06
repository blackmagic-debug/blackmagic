CROSS_COMPILE ?= arm-none-eabi-
ST_BOOTLOADER ?=
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

OPT_FLAGS = -Os
CFLAGS += -mcpu=cortex-m3 -mthumb -DSTM32F1 -I../deps/libopencm3/include \
	-I platforms/common/stm32
LDFLAGS_BOOT := $(LDFLAGS) -lopencm3_stm32f1 -Lplatforms/stlink \
	-Tstlink.ld --specs=nano.specs -nostartfiles -lc \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m3 -Wl,-gc-sections \
	-L../deps/libopencm3/lib
ifeq ($(ST_BOOTLOADER), 1)
$(info  Load address 0x08004000 for original ST-LinkV2 Bootloader)
CFLAGS += -DST_BOOTLOADER -DDFU_SERIAL_LENGTH=25
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8004000
else
CFLAGS += -DDFU_SERIAL_LENGTH=9
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8002000
endif

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

ifeq ($(SWIM_AS_UART), 1)
SWIM_NRST_AS_UART=1
$(warning Deprecation: SWIM_AS_UART has been renamed to SWIM_NRST_AS_UART; it might be ignored in the future)
endif

ifeq ($(SWIM_NRST_AS_UART), 1)
CFLAGS += -DSWIM_NRST_AS_UART=1
else
ifeq ($(TRACESWO_PROTOCOL), 1)
SRC += traceswo.c
CFLAGS += -DTRACESWO_PROTOCOL=1
else
SRC += traceswoasync.c
CFLAGS += -DTRACESWO_PROTOCOL=2
endif
endif

ifeq ($(BLUEPILL), 1)
CFLAGS += -DBLUEPILL=1
endif

ifeq ($(STLINK_FORCE_CLONE), 1)
CFLAGS += -DSTLINK_FORCE_CLONE=1
endif

ifeq ($(STLINK_V2_ISOL), 1)
CFLAGS += -DSTLINK_V2_ISOL=1
endif

VPATH += platforms/common/stm32

SRC +=          \
	platform.c \
	serialno.c	\
	timing.c	\
	timing_stm32.c	\
	traceswodecode.c	\
	stlink_common.c \

ifeq ($(ST_BOOTLOADER), 1)
all:	blackmagic.bin
else
all:	blackmagic.bin blackmagic_dfu.bin blackmagic_dfu.hex
endif
blackmagic_dfu.elf: usbdfu.o dfucore.o dfu_f1.o stlink_common.o serialno.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS_BOOT)

host_clean:
	-$(Q)$(RM) *.bin *elf *hex
