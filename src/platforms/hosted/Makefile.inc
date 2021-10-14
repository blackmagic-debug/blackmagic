CC ?= gcc
SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DENABLE_DEBUG -DPLATFORM_HAS_DEBUG
CFLAGS +=-I ./target -I./platforms/pc

# Define HOSTED_BMP_ONLY to '0' in order to build the hosted blackmagic
# executable with support for other probes beside BMP. Default HOSTED_BMP_ONLY
# == 1 on Windows makes linking against the libftdi and libusb libraries
# unnecessary.
# This can be useful to minimize external dependencies, and make building on
# windows systems easier and is default now.
ifneq (, $(findstring linux, $(SYS)))
HOSTED_BMP_ONLY ?= 0
else
HOSTED_BMP_ONLY ?= 1
endif
CFLAGS  +=  -DHOSTED_BMP_ONLY=$(HOSTED_BMP_ONLY)

ifneq (, $(findstring linux, $(SYS)))
SRC += serial_unix.c
HIDAPILIB = hidapi-hidraw
ifeq ($(ASAN), 1)
CFLAGS += -fsanitize=address  -Wno-format-truncation
LDFLAGS += -lasan
endif
else ifneq (, $(findstring mingw, $(SYS)))
# Build for windows versions Vista, and above, where the
# 'SetupDiGetDevicePropertyW()' function is available
CFLAGS += -D_WIN32_WINNT=0x600
SRC += 	 serial_win.c
LDFLAGS += -lws2_32
LDFLAGS += -lsetupapi
else ifneq (, $(findstring cygwin, $(SYS)))
# Build for windows versions Vista, and above, where the
# 'SetupDiGetDevicePropertyW()' function is available
CFLAGS += -D_WIN32_WINNT=0x600
SRC += 	 serial_win.c
LDFLAGS += -lws2_32
LDFLAGS += -lsetupapi
#https://github.com/dmlc/xgboost/issues/1945 indicates macosx as indicator
else ifneq (filter, macosx darwin, $(SYS))
SRC += serial_unix.c
LDFLAGS += -lhidapi
LDFLAGS += -framework CoreFoundation
CFLAGS += -Ihidapi/hidapi
HIDAPILIB = hidapi
endif

ifneq ($(HOSTED_BMP_ONLY), 1)
 $(shell pkg-config --exists libftdi1)
 ifneq ($(.SHELLSTATUS), 0)
  $(error Please install libftdi1 dependency or set HOSTED_BMP_ONLY to 1)
 endif
 LDFLAGS += -lusb-1.0
 CFLAGS += $(shell pkg-config --cflags libftdi1)
 LDFLAGS += $(shell pkg-config --libs libftdi1)
 CFLAGS += -Wno-missing-field-initializers
endif

ifneq ($(HOSTED_BMP_ONLY), 1)
 CFLAGS  +=  -DCMSIS_DAP
 SRC += cmsis_dap.c dap.c
 ifneq (, $(findstring mingw, $(SYS)))
  SRC += hid.c
 else
  $(shell pkg-config --exists $(HIDAPILIB))
  ifneq ($(.SHELLSTATUS), 0)
   $(error Please install $(HIDAPILIB) dependency or set HOSTED_BMP_ONLY to 1)
  endif
  CFLAGS += $(shell pkg-config --cflags $(HIDAPILIB))
  LDFLAGS += $(shell pkg-config --libs $(HIDAPILIB))
 endif
endif

VPATH += platforms/pc
SRC += timing.c cl_utils.c utils.c jtag_devs.c
SRC += bmp_remote.c remote_swdptap.c remote_jtagtap.c
ifneq ($(HOSTED_BMP_ONLY), 1)
SRC += bmp_libusb.c stlinkv2.c
SRC += ftdi_bmp.c libftdi_swdptap.c libftdi_jtagtap.c
SRC += jlink.c jlink_adiv5_swdp.c jlink_jtagtap.c
else
SRC += bmp_serial.c
endif
PC_HOSTED = 1
