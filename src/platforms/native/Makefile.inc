CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m3 -mthumb \
	-DSTM32F1 -DBLACKMAGIC -I../deps/libopencm3/include \
	-Iplatforms/common/stm32 -DDFU_SERIAL_LENGTH=9 -DSWO_ENCODING=1

LDFLAGS_BOOT := $(LDFLAGS) -lopencm3_stm32f1 -Lplatforms/native \
	-Tnative.ld --specs=nano.specs -nostartfiles -lc \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m3 -Wl,-gc-sections \
	-L../deps/libopencm3/lib
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8002000

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

VPATH += platforms/common/stm32

SRC +=               \
	platform.c       \
	serialno.c       \
	timing.c         \
	timing_stm32.c   \
	swo.c            \
	swo_itm_decode.c

# If SWO_ENCODING has not been given, default it to including both modes
ifndef SWO_ENCODING
	SWO_ENCODING = 3
endif

# Include the files for either: Manchester-only (1), UART-only (2) or both modes (3)
ifeq ($(SWO_ENCODING), 1)
	SRC += swo_manchester.c
else ifeq ($(SWO_ENCODING), 2)
	SRC += swo_uart.c
else ifeq ($(SWO_ENCODING), 3)
	SRC +=               \
		swo_manchester.c \
		swo_uart.c
else # If we got some other value, that's an error so report it
	$(error Invalid value for SWO encoding, must be one of 1, 2, or 3)
endif
CFLAGS += -DTRACESWO_PROTOCOL=$(SWO_ENCODING)

all:	blackmagic.bin blackmagic_dfu.bin blackmagic_dfu.hex

blackmagic_dfu.elf: usbdfu.o dfucore.o dfu_f1.o serialno.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS_BOOT)

host_clean:
	-$(Q)$(RM) blackmagic.bin blackmagic_dfu blackmagic_dfu.bin blackmagic_dfu.hex
