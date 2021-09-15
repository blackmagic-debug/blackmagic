PROBE_HOST ?= native
PLATFORM_DIR = platforms/$(PROBE_HOST)
VPATH += $(PLATFORM_DIR) target arch/riscv
ENABLE_DEBUG ?=

ifneq ($(V), 1)
MAKEFLAGS += --no-print-dir
Q := @
endif

CFLAGS += -Wall -Wextra -Werror -Wno-char-subscripts \
	-std=gnu99 -g3 -MD -I./target \
	-I. -Iinclude -I$(PLATFORM_DIR)

ifeq ($(ENABLE_DEBUG), 1)
CFLAGS += -DENABLE_DEBUG
endif

SRC =			\
	adiv5.c		\
	adiv5_jtagdp.c	\
	adiv5_swdp.c	\
	command.c	\
	cortexa.c	\
	cortexm.c	\
	crc32.c		\
	efm32.c		\
	exception.c	\
	gdb_if.c	\
	gdb_main.c	\
	gdb_hostio.c	\
	gdb_packet.c	\
	hex_utils.c	\
	jtag_scan.c	\
	lmi.c		\
	lpc_common.c	\
	lpc11xx.c	\
	lpc17xx.c	\
	lpc15xx.c	\
	lpc43xx.c	\
	lpc546xx.c	\
	kinetis.c	\
	main.c		\
	morse.c		\
	msp432.c	\
	nrf51.c		\
	nxpke04.c	\
	platform.c	\
	remote.c	\
	rp.c		\
	rvdbg013.c	\
	rvdbg013_jtag.c \
	sam3x.c		\
	sam4l.c		\
	samd.c		\
	samx5x.c	\
	stm32f1.c	\
	stm32f4.c	\
	stm32h7.c	\
	stm32l0.c	\
	stm32l4.c	\
	stm32g0.c	\
	target.c	\

include $(PLATFORM_DIR)/Makefile.inc

OPT_FLAGS ?= -Os
CFLAGS += $(OPT_FLAGS)
LDFLAGS += $(OPT_FLAGS)

ifndef TARGET
ifdef PC_HOSTED
TARGET = blackmagic
else
TARGET = blackmagic.elf
endif
endif

ifdef NO_OWN_LL
SRC += jtagtap_generic.c swdptap_generic.c
endif

ifdef PC_HOSTED
CFLAGS += -DPC_HOSTED=1
else
SRC += swdptap.c jtagtap.c
CFLAGS += -DPC_HOSTED=0
VPATH += platforms/common
CFLAGS += -Iplatforms/common
endif

OBJ = $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(SRC)))

$(TARGET): include/version.h $(OBJ)
	@echo "  LD      $@"
	$(Q)$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o:	%.c
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

%.o:	%.S
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

ifndef PC_HOSTED
%.bin:	%.elf
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $^ $@

%.hex:	%.elf
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O ihex $^ $@
endif

.PHONY:	clean host_clean all_platforms FORCE

clean:	host_clean
	$(Q)echo "  CLEAN"
	-$(Q)$(RM) *.o *.d *.elf *~ $(TARGET) $(HOSTFILES)
	-$(Q)$(RM) platforms/*/*.o platforms/*/*.d mapfile include/version.h

all_platforms:
	$(Q)set -e ;\
	mkdir -p artifacts/$(shell git describe --always --dirty --tags) ;\
	echo "<html><body><ul>" > artifacts/index.html ;\
	for i in platforms/*/Makefile.inc ; do \
		export DIRNAME=`dirname $$i` ;\
		export PROBE_HOST=`basename $$DIRNAME` ;\
		export CFLAGS=-Werror ;\
		echo "Building for hardware platform: $$PROBE_HOST" ;\
		$(MAKE) clean ;\
		$(MAKE);\
		if [ -f blackmagic.bin ]; then \
			mv blackmagic.bin artifacts/blackmagic-$$PROBE_HOST.bin ;\
			echo "<li><a href='blackmagic-$$PROBE_HOST.bin'>$$PROBE_HOST</a></li>"\
				>> artifacts/index.html ;\
		fi ;\
		if [ -f blackmagic_dfu.bin ]; then \
			mv blackmagic_dfu.bin artifacts/blackmagic_dfu-$$PROBE_HOST.bin ;\
			echo "<li><a href='blackmagic_dfu-$$PROBE_HOST.bin'>$$PROBE_HOST DFU</a></li>"\
				>> artifacts/index.html ;\
		fi ;\
	done ;\
	echo "</ul></body></html>" >> artifacts/index.html ;\
	cp artifacts/*.bin artifacts/$(shell git describe --always --dirty --tags)

command.c: include/version.h

include/version.h: FORCE
	$(Q)echo " GIT include/version.h"
	$(Q)echo "#define FIRMWARE_VERSION \"$(shell git describe --always --dirty --tags)\"" > $@
-include *.d
