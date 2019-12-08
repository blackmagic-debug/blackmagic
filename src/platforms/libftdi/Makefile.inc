SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DPC_HOSTED -DNO_LIBOPENCM3 -DENABLE_DEBUG
CFLAGS += $(shell pkg-config --cflags libftdi1)
LDFLAGS += $(shell pkg-config --libs libftdi1)
ifneq (, $(findstring mingw, $(SYS)))
LDFLAGS +=  -lusb-1.0 -lws2_32
else ifneq (, $(findstring cygwin, $(SYS)))
LDFLAGS +=  -lusb-1.0 -lws2_32
endif
VPATH += platforms/pc
SRC += 	timing.c cl_utils.c
CFLAGS +=-I ./target -I./platforms/pc
