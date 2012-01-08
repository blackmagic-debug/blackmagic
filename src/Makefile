ifndef PROBE_HOST
PROBE_HOST = stm32
endif

VPATH += $(PROBE_HOST)

BUILDDATE := `date +"%Y%m%d"`

CFLAGS += -Wall -Wextra -Wno-pointer-sign -Wno-char-subscripts\
	-O2 -std=gnu99 -g3 -DBUILDDATE=\"$(BUILDDATE)\"\
	-I. -Iinclude -I$(PROBE_HOST) \
	-DVERSION_SUFFIX=\"`../scripts/setlocalversion`\" -MD

SRC = 	gdb_if.c	\
	gdb_packet.c	\
	gdb_main.c	\
	hex_utils.c	\
	jtagtap.c	\
	swdptap.c	\
	adiv5.c		\
	adiv5_swdp.c	\
	cortexm3.c	\
	stm32_tgt.c	\
	nxp_tgt.c	\
	main.c		\
	platform.c	\
	command.c	\
	jtag_scan.c	\
	adiv5_jtagdp.c	\
	lmi.c		\
	arm7tdmi.c	\
	stm32f4.c	\
	crc32.c		\

include $(PROBE_HOST)/Makefile.inc

OBJ = $(SRC:.c=.o)

blackmagic: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY:	clean host_clean

clean:	host_clean
	$(RM) *.o *~ blackmagic $(HOSTFILES)

-include *.d

