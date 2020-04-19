SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DENABLE_DEBUG -DPLATFORM_HAS_DEBUG
CFLAGS +=-I ./target -I./platforms/pc
LDFLAGS += -lusb-1.0
ifneq (, $(findstring mingw, $(SYS)))
SRC += 	 serial_win.c
LDFLAGS += -lws2_32
else ifneq (, $(findstring cygwin, $(SYS)))
SRC += 	 serial_win.c
LDFLAGS += -lws2_32
else
SRC += serial_unix.c
endif
VPATH += platforms/pc
SRC += timing.c cl_utils.c utils.c libusb_utils.c
SRC += stlinkv2.c
SRC += bmp_remote.c remote_swdptap.c remote_jtagtap.c
PC_HOSTED = 1
