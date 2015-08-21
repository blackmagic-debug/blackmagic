CROSS_COMPILE ?= arm-none-eabi-
AS = $(CROSS_COMPILE)as
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
HEXDUMP = hexdump

ifneq ($(V), 1)
Q = @
endif

CFLAGS=-Os -std=gnu99 -mcpu=cortex-m0 -mthumb -I../libopencm3/include
ASFLAGS=-mcpu=cortex-m3 -mthumb

all:	lmi.stub stm32f4.stub stm32l4.stub nrf51.stub stm32f1.stub

stm32f1.o: stm32f1.c
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -DSTM32F1 -o $@ -c $<

stm32f4.o: stm32f4.c
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -DSTM32F4 -o $@ -c $<

stm32l4.o: stm32l4.c
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -o $@ -c $<

%.o:	%.s
	$(Q)echo "  AS      $<"
	$(Q)$(AS) $(ASFLAGS) -o $@ $<

%.bin:	%.o
	$(Q)echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

%.stub:	%.bin
	$(Q)echo "  HEXDUMP $@"
	$(Q)$(HEXDUMP) -v -e '/2 "0x%04X, "' $< > $@

.PHONY: clean

clean:
	$(Q)echo "  CLEAN"
	-$(Q)rm -f *.o *.bin *.stub

