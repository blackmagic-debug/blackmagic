CROSS_COMPILE ?= arm-none-eabi-
AS = $(CROSS_COMPILE)as
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
HEXDUMP = hexdump

ifneq ($(V), 1)
Q = @
endif

CFLAGS=-Os -std=gnu99 -mcpu=cortex-m0 -mthumb -I../../../libopencm3/include
ASFLAGS=-mcpu=cortex-m3 -mthumb

all:	lmi.stub stm32l4.stub nrf51.stub efm32.stub

%.o:    %.c
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

