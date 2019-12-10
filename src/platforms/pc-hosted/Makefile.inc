TARGET=blackmagic_hosted
SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DPC_HOSTED -DNO_LIBOPENCM3 -DENABLE_DEBUG -I
CFLAGS += $(shell pkg-config --cflags libftdi1)
CFLAGS +=-I ./target -I./platforms/pc
LDFLAGS += $(shell pkg-config --libs libftdi1)
ifneq (, $(findstring mingw, $(SYS)))
LDFLAGS +=  -lusb-1.0 -lws2_32
CFLAGS += -Wno-cast-function-type
else ifneq (, $(findstring cygwin, $(SYS)))
LDFLAGS +=  -lusb-1.0 -lws2_32
endif
VPATH += platforms/pc
SRC += 	cl_utils.c timing.c
