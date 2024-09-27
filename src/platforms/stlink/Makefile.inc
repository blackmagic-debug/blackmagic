CROSS_COMPILE ?= arm-none-eabi-
ST_BOOTLOADER ?=
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

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
    # If SWO_ENCODING has not been given, default it to including both modes
	ifndef SWO_ENCODING
		SWO_ENCODING = 3
	endif

    # Include the base implementation and decoder
	SRC +=               \
		swo.c            \
		swo_itm_decode.c

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
	CFLAGS += -DSWO_ENCODING=$(SWO_ENCODING)
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

SRC +=               \
	platform.c       \
	serialno.c       \
	timing.c         \
	timing_stm32.c   \
	stlink_common.c

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
