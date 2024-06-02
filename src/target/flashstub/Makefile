CROSS_COMPILE ?= arm-none-eabi-
AS = $(CROSS_COMPILE)as
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
HEXDUMP = hexdump

ifneq ($(V), 1)
Q = @
endif

CFLAGS=-std=c11 -Os -mcpu=cortex-m0 -mthumb -I../../../deps/libopencm3/include -ffreestanding
ASFLAGS=-mcpu=cortex-m3 -mthumb

all:	lmi.stub stm32l4.stub efm32.stub rp.stub

lmi.o: CFLAGS += -mcpu=cortex-m3
rp.o: CFLAGS += -Oz -mcpu=cortex-m0plus -nostartfiles -nodefaultlibs -nostdlib -T rp.ld
rp.o: rp.c
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -o $@ $<

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
