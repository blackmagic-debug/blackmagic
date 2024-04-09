CROSS_COMPILE ?= arm-none-eabi-
BMD_BOOTLOADER ?=
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS +=                           \
	-Istm32/include                 \
	-mcpu=cortex-m4                 \
	-mthumb                         \
	-mfloat-abi=hard                \
	-mfpu=fpv4-sp-d16               \
	-DSTM32F4                       \
	-DDFU_SERIAL_LENGTH=13          \
	-I../deps/libopencm3/include    \
	-Iplatforms/common/stm32        \
	-Iplatforms/common/blackpill-f4

LDFLAGS_BOOT :=                     \
	-lopencm3_stm32f4               \
	-Lplatforms/common/blackpill-f4 \
	-T$(LINKER_SCRIPT)              \
	-nostartfiles                   \
	-lc                             \
	-Wl,-Map=mapfile                \
	-mthumb                         \
	-mcpu=cortex-m4                 \
	-Wl,-gc-sections                \
	-mfloat-abi=hard                \
	-mfpu=fpv4-sp-d16               \
	-L../deps/libopencm3/lib

ifeq ($(BMD_BOOTLOADER), 1)
$(info  Load address 0x08004000 for BMPBootloader)
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8004000
CFLAGS += -DAPP_START=0x08004000 -DBMD_BOOTLOADER
else
LDFLAGS = $(LDFLAGS_BOOT)
endif

ifdef ALTERNATIVE_PINOUT
CFLAGS += -DALTERNATIVE_PINOUT=$(ALTERNATIVE_PINOUT)
endif

ifdef SHIELD
CFLAGS += -DSHIELD=$(SHIELD)
endif

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

VPATH +=                          \
	platforms/common/stm32        \
	platforms/common/blackpill-f4

SRC +=               \
	blackpill-f4.c   \
	traceswodecode.c \
	serialno.c       \
	timing.c         \
	timing_stm32.c

ifeq ($(TRACESWO_PROTOCOL), 1)
SRC += traceswo.c
CFLAGS += -DTRACESWO_PROTOCOL=1
else
SRC += traceswoasync.c
CFLAGS += -DTRACESWO_PROTOCOL=2
endif

ifneq ($(BMD_BOOTLOADER), 1)
all:	blackmagic.bin
else
all:	blackmagic.bin  blackmagic_dfu.bin blackmagic_dfu.hex
blackmagic_dfu: usbdfu.o dfucore.o dfu_f4.o serialno.o
	$(CC) $^ -o $@ $(LDFLAGS_BOOT) --specs=nosys.specs

blackmagic_dfu.bin:    blackmagic_dfu
	$(OBJCOPY) -O binary $^ $@

blackmagic_dfu.hex:    blackmagic_dfu
	$(OBJCOPY) -O ihex $^ $@
endif
host_clean:
	-$(Q)$(RM) blackmagic.bin
