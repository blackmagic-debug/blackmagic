PROBE_HOST ?= native
PLATFORM_DIR = platforms/$(PROBE_HOST)
VPATH += $(PLATFORM_DIR) target
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
	atxmega.c \
	avr_jtagdp.c \
	avr_pdi.c \
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
	jtag_devs.c	\
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
	sam3x.c		\
	sam4l.c		\
	samd.c		\
	samx5x.c	\
	stm32f1.c	\
	ch32f1.c	\
	stm32f4.c	\
	stm32h7.c	\
	stm32l0.c	\
	stm32l4.c	\
	stm32g0.c	\
	target.c	\


include $(PLATFORM_DIR)/Makefile.inc

ifneq ($(PC_HOSTED),1)
# Output memory usage information
LDFLAGS += -Wl,--print-memory-usage
endif

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

ifeq ($(ENABLE_RTT), 1)
CFLAGS += -DENABLE_RTT
SRC += rtt.c rtt_if.c
endif

ifdef RTT_IDENT
CFLAGS += -DRTT_IDENT=$(RTT_IDENT)
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
	$(Q)if [ ! -f ../libopencm3/Makefile ]; then \
		echo "Initialising git submodules..." ;\
		git submodule init ;\
		git submodule update ;\
	fi
	$(Q)$(MAKE) $(MFLAGS) -C ../libopencm3 lib/stm32/f1 lib/stm32/f4 lib/lm4f
	$(Q)set -e ;\
	mkdir -p artifacts/$(shell git describe --always --dirty --tags) ;\
	echo "<html><body><ul>" > artifacts/index.html ;\
	$(MAKE) clean ;\
	for i in platforms/*/Makefile.inc ; do \
		export DIRNAME=`dirname $$i` ;\
		export PROBE_HOST=`basename $$DIRNAME` ;\
		export CFLAGS=-Werror ;\
		echo "Building for hardware platform: $$PROBE_HOST" ;\
		$(MAKE);\
		if [ -f blackmagic ]; then \
			mv blackmagic artifacts/blackmagic-$$PROBE_HOST ;\
			echo "<li><a href='blackmagic-$$PROBE_HOST'>$$PROBE_HOST</a></li>"\
				>> artifacts/index.html ;\
		fi ;\
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
		$(MAKE) clean ;\
	done ;\
	echo "</ul></body></html>" >> artifacts/index.html ;\
	cp artifacts/blackmagic* artifacts/$(shell git describe --always --dirty --tags)

command.c: include/version.h

GIT_VERSION := $(shell git describe --always --dirty --tags)
VERSION_HEADER := \#define FIRMWARE_VERSION "$(GIT_VERSION)"

include/version.h: FORCE
	@# If git isn't found then GIT_VERSION will be an empty string.
ifeq ($(GIT_VERSION),)
	@echo Git not found, assuming up to date include/version.h
else
	@# Note that when we echo the version to the header file, echo writes a final newline
	@# to the file. This is fine and probably makes the file more human-readable, but
	@# also means we have to account for that newline in this comparison.
	$(Q)if [ ! -f $@ ] || [ "$$(cat $@)" != "$$(echo '$(VERSION_HEADER)\n')" ]; then \
		echo " GEN $@"; \
		echo '$(VERSION_HEADER)' > $@; \
	fi
endif

-include *.d
