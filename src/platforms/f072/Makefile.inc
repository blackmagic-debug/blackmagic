CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m0 -mthumb \
	-DSTM32F0 -I../libopencm3/include \
	-DDFU_SERIAL_LENGTH=13 -Iplatforms/common/stm32

LDFLAGS = --specs=nano.specs -lopencm3_stm32f0 \
	-Wl,-T,platforms/f072/stm32f07xzb.ld \
	-nostartfiles -lc -lnosys -Wl,-Map=mapfile -mthumb \
	-mcpu=cortex-m0 -Wl,-gc-sections -L../libopencm3/lib

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
	$(Q)$(MAKE) $(MFLAGS) -C ../libopencm3 lib/stm32/f0

host_clean:
	-$(Q)$(RM) blackmagic.bin

.PHONY: libopencm3_stm32f0
