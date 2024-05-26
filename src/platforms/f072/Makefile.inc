CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m0 -mthumb \
	-DSTM32F0 -I../deps/libopencm3/include \
	-DDFU_SERIAL_LENGTH=13 -Iplatforms/common/stm32

LDFLAGS = -lopencm3_stm32f0 -Lplatforms/f072 \
	-Tstm32f07xzb.ld --specs=nano.specs \
	-nostartfiles -lc -lnosys -Wl,-Map=mapfile -mthumb \
	-mcpu=cortex-m0 -Wl,-gc-sections -L../deps/libopencm3/lib

VPATH += platforms/common/stm32

SRC +=               \
	platform.c \
	traceswodecode.c \
	traceswo.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\

all:	blackmagic.bin
blackmagic.elf: libopencm3_stm32f0

libopencm3_stm32f0:
	$(Q)$(MAKE) $(MFLAGS) -C ../deps/libopencm3 lib/stm32/f0

host_clean:
	-$(Q)$(RM) blackmagic.bin

.PHONY: libopencm3_stm32f0
