CROSS_COMPILE ?= arm-none-eabi-
AS = $(CROSS_COMPILE)as
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
HEXDUMP = hexdump

ifneq ($(V), 1)
Q = @
endif

CFLAGS=-O0 -std=gnu99 -mcpu=cortex-m0 -mthumb -g3 -I../../../libopencm3/include
ASFLAGS=-mcpu=cortex-m3 -mthumb

all:	lmi.stub stm32f4_x8.stub stm32f4_x32.stub stm32l4.stub nrf51.stub \
	stm32f1.stub efm32.stub efr32.stub

stm32f1.o: CFLAGS += -DSTM32F1
stm32f4_x8.o: CFLAGS += -DSTM32F4
stm32f4_x32.o: CFLAGS += -DSTM32F4

%.elf: %.o
	arm-none-eabi-g++ -mcpu=cortex-m0 -mthumb -O0 -fmessage-length=0 -ffunction-sections -fdata-sections -Wall -Wextra  -g3 -T "efr32.ld" -Xlinker --gc-sections -Wl,-Map,"efr32.map" --specs=nano.specs -Wl,-wrap,__aeabi_unwind_cpp_pr0 -Wl,-wrap,__aeabi_unwind_cpp_pr1 -Wl,-wrap,__aeabi_unwind_cpp_pr2 -o $@ $<

%.o:    %.c
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -o $@ -c $<

%.o:	%.s
	$(Q)echo "  AS      $<"
	$(Q)$(AS) $(ASFLAGS) -o $@ $<

%.bin: 	%.o
	$(Q)echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $< $@

%.bin:	%.elf
	$(Q)echo "  OBJCOPY ELF $@"
	$(Q)$(OBJCOPY) -O binary $< $@

%.stub:	%.bin
	$(Q)echo "  HEXDUMP $@"
	$(Q)$(HEXDUMP) -v -e '/2 "0x%04X, "' $< > $@

.PHONY: clean

clean:
	$(Q)echo "  CLEAN"
	-$(Q)rm -f *.o *.bin *.stub

