CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m3 -mthumb \
	-DSTM32F1 -DBLACKMAGIC -I../deps/libopencm3/include \
	-Iplatforms/common/stm32 -DDFU_SERIAL_LENGTH=9

LDFLAGS_BOOT := $(LDFLAGS) --specs=nano.specs -lopencm3_stm32f1 \
	-Wl,-T,platforms/ctxLink/ctxLink.ld -nostartfiles -lc \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m3 -Wl,-gc-sections \
	-L../deps/libopencm3/lib
LDFLAGS = $(LDFLAGS_BOOT)

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

VPATH += platforms/common/stm32

SRC +=               \
	platform.c \
	traceswodecode.c \
	traceswo.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\

all:	blackmagic.bin

host_clean:
	-$(Q)$(RM) blackmagic.bin blackmagic_dfu blackmagic_dfu.bin blackmagic_dfu.hex
