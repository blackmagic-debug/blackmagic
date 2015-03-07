PROBE_HOST ?= native
PLATFORM_DIR = platforms/$(PROBE_HOST)
VPATH += platforms/common $(PLATFORM_DIR)

ifneq ($(V), 1)
MAKEFLAGS += --no-print-dir
Q := @
endif

BUILDDATE := `date +"%Y%m%d"`

CFLAGS += -Wall -Wextra -Wno-pointer-sign -Wno-char-subscripts\
	-Wno-sign-compare \
	-O2 -std=gnu99 -g3 -DBUILDDATE=\"$(BUILDDATE)\"\
	-I. -Iinclude -Iplatforms/common -I$(PLATFORM_DIR) \
	-DVERSION_SUFFIX=\"`../scripts/setlocalversion`\" -MD

SRC =			\
	adiv5.c		\
	adiv5_jtagdp.c	\
	adiv5_swdp.c	\
	arm7tdmi.c	\
	command.c	\
	cortexm.c	\
	crc32.c		\
	gdb_if.c	\
	gdb_main.c	\
	gdb_packet.c	\
	hex_utils.c	\
	jtag_scan.c	\
	jtagtap.c	\
	lmi.c		\
	lpc11xx.c	\
	lpc43xx.c	\
	kinetis.c	\
	main.c		\
	morse.c		\
	nrf51.c		\
	platform.c	\
	sam3x.c		\
	samd.c		\
	stm32f1.c	\
	stm32f4.c	\
	stm32l1.c	\
	swdptap.c	\
	target.c	\

include $(PLATFORM_DIR)/Makefile.inc

OBJ = $(SRC:.c=.o)

blackmagic: $(OBJ)
	@echo "  LD      $@"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

%.o:	%.c
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

%.bin:	%
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O binary $^ $@

%.hex:	%
	@echo "  OBJCOPY $@"
	$(Q)$(OBJCOPY) -O ihex $^ $@

.PHONY:	clean host_clean all_platforms

clean:	host_clean
	$(Q)echo "  CLEAN"
	-$(Q)$(RM) -f *.o *.d *~ blackmagic $(HOSTFILES)
	-$(Q)$(RM) -f platforms/*/*.o platforms/*/*.d mapfile

all_platforms:
	$(Q)set -e ;\
	for i in platforms/*/Makefile.inc ; do \
		export DIRNAME=`dirname $$i` ;\
		export PROBE_HOST=`basename $$DIRNAME` ;\
		export CFLAGS=-Werror ;\
		echo "Building for hardware platform: $$PROBE_HOST" ;\
		$(MAKE) $(MAKEFLAGS) clean ;\
		$(MAKE) $(MAKEFLAGS);\
		if [ -f blackmagic.bin ]; then \
			mv blackmagic.bin blackmagic-$$PROBE_HOST.bin ;\
		fi ;\
	done

-include *.d

