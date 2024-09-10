CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-DSTM32F4 -DCTX_LINK -DDFU_SERIAL_LENGTH=13 -I../deps/libopencm3/include \
	-Iplatforms/common/stm32

LDFLAGS = -lopencm3_stm32f4 \
	-Tctxlink.ld -nostartfiles -lc -lnosys \
	-Lplatforms/ctxlink \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../deps/libopencm3/lib

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
CFLAGS += -DSWO_ENCODING=$(SWO_ENCODING)

all:	blackmagic.bin

host_clean:
	-$(Q)$(RM) blackmagic.bin
