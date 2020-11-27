SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DENABLE_DEBUG -DPLATFORM_HAS_DEBUG
CFLAGS +=-I ./target -I./platforms/pc

ifneq (, $(findstring linux, $(SYS)))
SRC += serial_unix.c
ifeq ($(ASAN), 1)
CFLAGS += -fsanitize=address
LDFLAGS += -lasan
endif
else ifneq (, $(findstring mingw, $(SYS)))
SRC += 	 serial_win.c
LDFLAGS += -lws2_32
LDFLAGS += -lsetupapi
else ifneq (, $(findstring cygwin, $(SYS)))
SRC += 	 serial_win.c
LDFLAGS += -lws2_32
LDFLAGS += -lsetupapi
#https://github.com/dmlc/xgboost/issues/1945 indicates macosx as indicator
else ifneq (filter, macosx darwin, $(SYS))
SRC += serial_unix.c
LDFLAGS += -lhidapi
LDFLAGS += -framework CoreFoundation
CFLAGS += -Ihidapi/hidapi
endif

LDFLAGS += -lusb-1.0
CFLAGS += $(shell pkg-config --cflags libftdi1)
LDFLAGS += $(shell pkg-config --libs libftdi1)
CFLAGS += -Wno-missing-field-initializers

ifneq (, $(findstring mingw, $(SYS)))
 SRC += cmsis_dap.c dap.c hid.c
 CFLAGS  +=  -DCMSIS_DAP
else
 ifeq ($(shell pkg-config --exists hidapi-libusb && echo 0), 0)
  CFLAGS += $(shell pkg-config --cflags hidapi-libusb)
  LDFLAGS += $(shell pkg-config --libs hidapi-libusb)
  CFLAGS  +=  -DCMSIS_DAP
  SRC += cmsis_dap.c dap.c
 endif
endif

VPATH += platforms/pc
SRC += timing.c cl_utils.c utils.c libusb_utils.c
SRC += stlinkv2.c
SRC += bmp_remote.c remote_swdptap.c remote_jtagtap.c
SRC += ftdi_bmp.c libftdi_swdptap.c libftdi_jtagtap.c
SRC += jlink.c jlink_adiv5_swdp.c jlink_jtagtap.c
PC_HOSTED = 1
