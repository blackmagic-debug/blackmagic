CC = arm-cortexm3-eabi-gcc
OBJCOPY = arm-cortexm3-eabi-objcopy

CFLAGS += -Istm32/include
LDFLAGS_BOOT = -lopencm3_stm32 -Wl,--defsym,_stack=0x20005000 \
	-Wl,-T,stm32/blackmagic.ld -nostartfiles -lc -lnosys -Wl,-Map=mapfile
LDFLAGS = $(LDFLAGS_BOOT) -Wl,-Ttext=0x8002000

SRC += 	cdcacm.c	\
	platform.c	\

all:	blackmagic.bin blackmagic_dfu.bin

blackmagic.bin:	blackmagic
	$(OBJCOPY) -O binary $^ $@

blackmagic_dfu: usbdfu.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_BOOT)

blackmagic_dfu.bin:	blackmagic_dfu
	$(OBJCOPY) -O binary $^ $@

host_clean:
	-rm blackmagic.bin blackmagic_dfu blackmagic_dfu.bin

