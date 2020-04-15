TARGET=blackmagic_stlinkv2
SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DSTLINKV2 -DJTAG_HL -DENABLE_DEBUG
CFLAGS +=-I ./target -I./platforms/pc
LDFLAGS += -lusb-1.0
ifneq (, $(findstring mingw, $(SYS)))
LDFLAGS += -lws2_32
else ifneq (, $(findstring cygwin, $(SYS)))
LDFLAGS += -lws2_32
endif
VPATH += platforms/pc
SRC += 	timing.c stlinkv2.c cl_utils.c utils.c
OWN_HL = 1
PC_HOSTED = 1
