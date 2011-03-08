ifndef HOST
HOST = stm32
endif

VPATH += $(HOST)

BUILDDATE := `date +"%Y%m%d"`

CFLAGS += -Wall -Wextra -Wno-pointer-sign -Wno-char-subscripts\
	-Os -std=gnu99 -g3 -DBUILDDATE=\"$(BUILDDATE)\"\
	-I. -Iinclude -I$(HOST) \
	-DVERSION_SUFFIX=\"`../scripts/setlocalversion`\"

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
	main.c		\
	platform.c	\
	command.c	\
	jtag_scan.c	\
	adiv5_jtagdp.c	\
	lmi.c		\

include $(HOST)/Makefile.inc

OBJ = $(SRC:.c=.o)

blackmagic: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY:	clean host_clean

clean:	host_clean
	$(RM) *.o *~ blackmagic $(HOSTFILES)

