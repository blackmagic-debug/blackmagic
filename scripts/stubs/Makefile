CROSS_COMPILE = stm32-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

all:	stm32_opterase.bin stm32_optprog.bin

%.bin:	%.S
	$(CC) -nostdlib -Wl,-Ttext,0x20000000 $<
	$(OBJCOPY) -O binary a.out $@

clean:
	-rm *.bin
