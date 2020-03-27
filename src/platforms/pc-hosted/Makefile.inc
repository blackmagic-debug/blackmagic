TARGET=blackmagic_hosted
SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DPC_HOSTED -DNO_LIBOPENCM3 -DENABLE_DEBUG
CFLAGS +=-I ./target -I./platforms/pc
ifneq (, $(findstring mingw, $(SYS)))
LDFLAGS +=  -lws2_32
else ifneq (, $(findstring cygwin, $(SYS)))
LDFLAGS +=  -lws2_32
endif
VPATH += platforms/pc
SRC += 	cl_utils.c timing.c utils.c
