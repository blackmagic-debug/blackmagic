CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-DSTM32F3 -I../libopencm3/include \
	-DDFU_SERIAL_LENGTH=13 -Iplatforms/stm32

LDFLAGS =  --specs=nano.specs -lopencm3_stm32f3 \
	-Wl,-T,platforms/stm32/stm32f303xc.ld -nostartfiles -lc -lnosys \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../libopencm3/lib

VPATH += platforms/stm32

SRC += 	cdcacm.c	\
	traceswodecode.c\
	traceswo.c	\
	usbuart.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\

all:	blackmagic.bin
blackmagic.elf: libopencm3_stm32f3

libopencm3_stm32f3:
	$(Q)$(MAKE) $(MFLAGS) -C ../libopencm3 lib/stm32/f3

host_clean:
	-$(Q)$(RM) blackmagic.bin

.PHONY: libopencm3_stm32f3
