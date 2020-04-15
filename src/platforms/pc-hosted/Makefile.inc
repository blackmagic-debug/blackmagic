TARGET=blackmagic_hosted
SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DENABLE_DEBUG
CFLAGS +=-I ./target -I./platforms/pc
ifneq (, $(findstring mingw, $(SYS)))
SRC += 	 serial_win.c
LDFLAGS +=  -lws2_32
else ifneq (, $(findstring cygwin, $(SYS)))
SRC += 	 serial_win.c
LDFLAGS +=  -lws2_32
else
SRC += serial_unix.c
endif
VPATH += platforms/pc
SRC += 	cl_utils.c timing.c utils.c
PC_HOSTED = 1
