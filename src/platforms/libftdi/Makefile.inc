SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DPC_HOSTED -DNO_LIBOPENCM3 -DENABLE_DEBUG
LDFLAGS += -lftdi1
ifneq (, $(findstring mingw, $(SYS)))
LDFLAGS +=  -lusb-1.0 -lws2_32
else ifneq (, $(findstring cygwin, $(SYS)))
LDFLAGS +=  -lusb-1.0 -lws2_32
endif
VPATH += platforms/pc
SRC += 	timing.c	\
