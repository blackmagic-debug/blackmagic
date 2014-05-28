PROBE_HOST ?= native
PLATFORM_DIR = platforms/$(PROBE_HOST)
VPATH += $(PLATFORM_DIR)

BUILDDATE := `date +"%Y%m%d"`

CFLAGS += -Wall -Wextra -Wno-pointer-sign -Wno-char-subscripts\
	-Wno-sign-compare \
	-O2 -std=gnu99 -g3 -DBUILDDATE=\"$(BUILDDATE)\"\
	-I. -Iinclude -I$(PLATFORM_DIR) \
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
	main.c		\
	nrf51.c		\
	platform.c	\
	sam3x.c		\
	stm32f1.c	\
	stm32f4.c	\
	stm32l1.c	\
	swdptap.c	\
	target.c	\

include $(PLATFORM_DIR)/Makefile.inc

OBJ = $(SRC:.c=.o)

blackmagic: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY:	clean host_clean

clean:	host_clean
	-$(RM) *.o *.d *~ blackmagic $(HOSTFILES)
	-$(RM) platforms/*/*.o platforms/*/*.d mapfile

-include *.d

