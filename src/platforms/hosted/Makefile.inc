CC ?= gcc
SYS = $(shell $(CC) -dumpmachine)
CFLAGS += -DENABLE_DEBUG -DPLATFORM_HAS_DEBUG
CFLAGS +=-I ./target

# HOSTED_BMP_ONLY, which defaults to 1 on Windows + MacOS and 0 on Linux,
# defines whether to build Black Magic Debug App for the Black Magic Firmware
# only, or whether to include support for other adaptor types including FTDI,
# and CMSIS-DAP compatible dongles.
#
# The benefit of only building for the Black Magic Firmware is a reduced linking
# footprint. Specifically no requirement to link against libftdi1, libusb1, or
# hidapi (hidapi-hidraw on Linux). This makes building on Windows in particular easier.
ifneq (, $(findstring linux, $(SYS)))
    HOSTED_BMP_ONLY ?= 0
else
    HOSTED_BMP_ONLY ?= 1
endif
CFLAGS += -DHOSTED_BMP_ONLY=$(HOSTED_BMP_ONLY)

ifeq ($(ASAN), 1)
    CFLAGS += -fsanitize=address
    ifeq (, $(findstring darwin, $(SYS)))
        CFLAGS += -Wno-format-truncation
    endif
    LDFLAGS += -fsanitize=address
endif

HIDAPILIB = hidapi
ifneq (, $(findstring linux, $(SYS)))
    SRC += serial_unix.c
    HIDAPILIB = hidapi-hidraw
else ifneq (, $(findstring mingw, $(SYS)))
    # Build for windows versions Vista, and above, where the
    # 'SetupDiGetDevicePropertyW()' function is available
    CFLAGS += -D_WIN32_WINNT=0x600
    SRC += serial_win.c
    LDFLAGS += -lws2_32
    LDFLAGS += -lsetupapi
else ifneq (, $(findstring cygwin, $(SYS)))
    # Build for windows versions Vista, and above, where the
    # 'SetupDiGetDevicePropertyW()' function is available
    CFLAGS += -D_WIN32_WINNT=0x600
    SRC += serial_win.c
    LDFLAGS += -lws2_32
    LDFLAGS += -lsetupapi
# https://github.com/dmlc/xgboost/issues/1945 indicates macosx as indicator
else ifneq (filter, macosx darwin, $(SYS))
    SRC += serial_unix.c
    LDFLAGS += -framework CoreFoundation
    CFLAGS += -I /opt/homebrew/include -I /opt/homebrew/include/libusb-1.0
endif

ifneq ($(HOSTED_BMP_ONLY), 1)
    ifneq ($(shell pkg-config --exists libusb-1.0; echo $$?), 0)
        $(error Please install libusb-1.0 dependency or set HOSTED_BMP_ONLY to 1)
    endif
    ifneq ($(shell pkg-config --exists libftdi1; echo $$?), 0)
        $(error Please install libftdi1 dependency or set HOSTED_BMP_ONLY to 1)
    endif
    CFLAGS += $(shell pkg-config --cflags libusb-1.0) $(shell pkg-config --cflags libftdi1)
    LDFLAGS += $(shell pkg-config --libs libusb-1.0) $(shell pkg-config --libs libftdi1)
    CFLAGS += -Wno-missing-field-initializers
endif

ifneq ($(HOSTED_BMP_ONLY), 1)
    CFLAGS  +=  -DCMSIS_DAP
    SRC += cmsis_dap.c dap.c
    ifneq ($(shell pkg-config --exists $(HIDAPILIB); echo $$?), 0)
        $(error Please install $(HIDAPILIB) dependency or set HOSTED_BMP_ONLY to 1)
    endif
    CFLAGS += $(shell pkg-config --cflags $(HIDAPILIB))
    LDFLAGS += $(shell pkg-config --libs $(HIDAPILIB))
endif

SRC += timing.c cli.c utils.c
SRC += bmp_remote.c remote_swdptap.c remote_jtagtap.c
ifneq ($(HOSTED_BMP_ONLY), 1)
    SRC += bmp_libusb.c stlinkv2.c
    SRC += ftdi_bmp.c libftdi_swdptap.c libftdi_jtagtap.c
    SRC += jlink.c jlink_adiv5_swdp.c jlink_jtagtap.c
else
    SRC += bmp_serial.c
endif
PC_HOSTED = 1

all: blackmagic

host_clean:
	-$(Q)$(RM) blackmagic
