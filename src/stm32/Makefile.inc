CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
LIBOPENCM3 ?= /usr/local/arm-none-eabi

CFLAGS += -Istm32/include -mcpu=cortex-m3 -mthumb -msoft-float -DSTM32F1 -I$(LIBOPENCM3)/include
LDFLAGS_BOOT = -lopencm3_stm32f1 -Wl,--defsym,_stack=0x20005000 \
	-Wl,-T,stm32/blackmagic.ld -nostartfiles -lc -lnosys -Wl,-Map=mapfile \
	-mthumb -march=armv7 -mcpu=cortex-m3 -mfix-cortex-m3-ldrd -msoft-float -L$(LIBOPENCM3)/lib/stm32/f1
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8002000

SRC += 	cdcacm.c	\
	platform.c	\

all:	blackmagic.bin blackmagic_dfu.bin

blackmagic.bin:	blackmagic
	$(OBJCOPY) -O binary $^ $@

blackmagic_dfu: usbdfu.o
	$(CC) $^ -o $@ $(LDFLAGS_BOOT)

blackmagic_dfu.bin:	blackmagic_dfu
	$(OBJCOPY) -O binary $^ $@

host_clean:
	-rm blackmagic.bin blackmagic_dfu blackmagic_dfu.bin

