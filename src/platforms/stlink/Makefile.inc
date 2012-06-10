CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -mcpu=cortex-m3 -mthumb -DSTM32F1
LDFLAGS = -lopencm3_stm32f1 -Wl,--defsym,_stack=0x20005000 \
	-Wl,-T,platforms/stm32/blackmagic.ld -nostartfiles -lc -lnosys \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m3 -Wl,-gc-sections

VPATH += platforms/stm32

SRC += 	cdcacm.c	\
	platform.c	\

all:	blackmagic.bin

blackmagic.bin:	blackmagic
	$(OBJCOPY) -O binary $^ $@

host_clean:
	-rm blackmagic.bin

