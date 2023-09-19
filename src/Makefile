PROBE_HOST ?= native
PLATFORM_DIR = platforms/$(PROBE_HOST)
VPATH += $(PLATFORM_DIR) target
ENABLE_DEBUG ?=

SYS = $(shell $(CC) -dumpmachine)

ifneq ($(V), 1)
MAKEFLAGS += --no-print-dir
Q := @
endif

CFLAGS += -Wall -Wextra -Werror -Wreturn-type \
	-Wno-char-subscripts \
	-std=c11 -g3 -MD -I./target \
	-I. -Iinclude -I$(PLATFORM_DIR)

ifeq (filter, macosx darwin, $(SYS))
CFLAGS += -Wmaybe-uninitialized -Wstringop-overflow -Wunsafe-loop-optimizations
endif

ifeq ($(ENABLE_DEBUG), 1)
CFLAGS += -DENABLE_DEBUG
endif

SRC =              \
	adiv5.c        \
	adiv5_jtag.c   \
	adiv5_swd.c    \
	command.c      \
	cortex.c       \
	cortexa.c      \
	cortexm.c      \
	crc32.c        \
	efm32.c        \
	exception.c    \
	gdb_if.c       \
	gdb_main.c     \
	gdb_hostio.c   \
	gdb_packet.c   \
	gdb_reg.c      \
	hex_utils.c    \
	hc32l110.c     \
	imxrt.c        \
	jtag_devs.c    \
	jtag_scan.c    \
	lmi.c          \
	lpc_common.c   \
	lpc11xx.c      \
	lpc17xx.c      \
	lpc15xx.c      \
	lpc40xx.c      \
	lpc43xx.c      \
	lpc546xx.c     \
	lpc55xx.c      \
	kinetis.c      \
	main.c         \
	maths_utils.c  \
	morse.c        \
	msp432e4.c     \
	msp432p4.c     \
	nrf51.c        \
	nrf91.c        \
	nxpke04.c      \
	remote.c       \
	rp.c           \
	sam3x.c        \
	sam4l.c        \
	samd.c         \
	samx5x.c       \
	sfdp.c         \
	spi.c          \
	stm32f1.c      \
	ch32f1.c       \
	stm32f4.c      \
	stm32h5.c      \
	stm32h7.c      \
	stm32mp15.c    \
	stm32l0.c      \
	stm32l4.c      \
	stm32g0.c      \
	renesas.c      \
	target.c       \
	target_flash.c \
	target_probe.c

ifeq (,$(filter all_platforms,$(MAKECMDGOALS)))
include $(PLATFORM_DIR)/Makefile.inc
endif

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
CFLAGS += -DPC_HOSTED=0
include platforms/common/Makefile.inc
endif

ifeq ($(ENABLE_RTT), 1)
CFLAGS += -DENABLE_RTT
SRC += rtt.c rtt_if.c
endif

ifdef RTT_IDENT
CFLAGS += -DRTT_IDENT=$(RTT_IDENT)
endif

ifeq ($(ADVERTISE_NOACKMODE), 1)
CFLAGS += -DADVERTISE_NOACKMODE=1
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

.PHONY:	clean host_clean all_platforms clang-format FORCE


GIT_VERSION := $(shell if [ -e "../.git" ]; then git describe --always --dirty --tags; fi)

clean:	host_clean
	$(Q)echo "  CLEAN"
	-$(Q)$(RM) *.o *.d *.elf *~ $(TARGET) $(HOSTFILES)
	-$(Q)$(RM) platforms/*/*.o platforms/*/*.d mapfile ftd2xx.dll
ifeq ($(GIT_VERSION),)
	@echo Git not found, not deleting include/version.h
else
	-$(Q)$(RM) include/version.h
endif

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
		if [ "$$DIRNAME" = "platforms/common" ]; then \
			continue; \
		fi; \
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
		echo " GEN      $@"; \
		echo '$(VERSION_HEADER)' > $@; \
	fi
endif

clang-format:
	$(Q)clang-format -i *.c */*.c */*/*.c *.h */*.h */*/*.h

ifeq (,$(filter clean,$(MAKECMDGOALS)))
-include *.d
endif
