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
SRC += timing.c cl_utils.c utils.c
PC_HOSTED = 1
