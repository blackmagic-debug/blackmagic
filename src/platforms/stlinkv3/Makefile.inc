CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

OPT_FLAGS = -Og -g
CFLAGS += -mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
	-DSTM32F7 -DDFU_SERIAL_LENGTH=25 -I../libopencm3/include \
	-I platforms/common/stm32
LDFLAGS_BOOT := $(LDFLAGS) -mfpu=fpv5-sp-d16 -mfloat-abi=hard \
	--specs=nano.specs -lopencm3_stm32f7 \
	-Wl,-T,platforms/stlinkv3/stlinkv3.ld -nostartfiles -lc \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m7 -Wl,-gc-sections \
	-L../libopencm3/lib

ifeq ($(NO_BOOTLOADER), 1)
APP_START = 0x08000000
else
APP_START = 0x08020000
endif

LDFLAGS = $(LDFLAGS_BOOT)
LDFLAGS +=  -Wl,-Ttext=$(APP_START)
CFLAGS += -DAPP_START=$(APP_START)

ifeq ($(ENABLE_DEBUG), 1)
LDFLAGS += --specs=rdimon.specs
else
LDFLAGS += --specs=nosys.specs
endif

VPATH += platforms/common/stm32

SRC += 	\
	platform.c \
	serialno.c	\
	timing.c	\
	timing_stm32.c	\
	traceswoasync_f723.c	\
	traceswodecode.c	\

.PHONY: libopencm3_stm32f7

ifeq ($(NO_BOOTLOADER), 1)
all:	libopencm3_stm32f7 blackmagic.bin
else
all:	libopencm3_stm32f7 blackmagic.bin blackmagic_dfu.bin blackmagic_dfu.hex

blackmagic_dfu.elf: usbdfu.o dfucore.o dfu_f4.o usb_f723.o serialno.o
	@echo "  LD      $@"
	$(Q)$(CC) $^ -o $@ $(LDFLAGS_BOOT)
endif

blackmagic.elf: libopencm3_stm32f7
libopencm3_stm32f7:
	$(Q)$(MAKE) $(MFLAGS) -C ../libopencm3 lib/stm32/f7

host_clean:
	-$(Q)$(RM) *.bin *elf *hex *.o

# Add local libopencm3 patched usb driver.
SRC += usb_f723.c

# Note: below are provided some customized versions of
# files from the libopencm3 libraries. It would be ideal
# if some day these go in libopencm3 master, but for the
# time being this is a convenient solution.
#
# Note that there is something very odd about linking these
# libopencm3 replacement files and libopencm3 together.
# If all of the externally visible symbols from these
# replacement files are kept (e.g., no externally visible
# functions or data objects are removed), then linking
# mysteriously succeeds. However, removing some externally
# symbols cause linking to (expectedly) fail, giving an
# error of multiple symbol definitions. The strange
# thing is why linking succeeds in the case described above.

usb_f723.o: usb_f723.c
	@echo "  CC      $<"
	$(Q)$(CC) -I../libopencm3/lib/usb/ $(CFLAGS) $(OPT_FLAGS) -c $< -o $@
