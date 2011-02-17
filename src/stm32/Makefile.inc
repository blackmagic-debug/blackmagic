CROSS_COMPILE ?= arm-cortexm3-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m3 -mthumb -msoft-float
LDFLAGS_BOOT = -lopencm3_stm32 -Wl,--defsym,_stack=0x20005000 \
	-Wl,-T,stm32/blackmagic.ld -nostartfiles -lc -lnosys -Wl,-Map=mapfile \
	-mthumb -march=armv7 -mfix-cortex-m3-ldrd -msoft-float
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

