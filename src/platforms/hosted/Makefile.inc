ifeq ($(origin CC),default)
    CC := gcc
endif
SYS := $(shell $(CC) -dumpmachine)
CFLAGS += -DENABLE_DEBUG -DPLATFORM_HAS_DEBUG
CFLAGS +=-I ./target

# Clang requires some special handling here: -gnu means MinGW
# while -msvc means Clang/CL. We don't currently support the latter
# and we need the former to fit the formula of `x86_64-w64-mingw32`
# and friends so we correctly trigger the MinGW block below.
ifeq ($(SYS),x86_64-w64-windows-gnu)
    SYS := x86_64-clang64-mingw
endif

# HOSTED_BMP_ONLY, which defaults to 1 on Windows + MacOS and 0 on Linux,
# defines whether to build Black Magic Debug App for the Black Magic Firmware
# only, or whether to include support for other adaptor types including FTDI,
# and CMSIS-DAP compatible dongles.
#
# The benefit of only building for the Black Magic Firmware is a reduced linking
# footprint. Specifically no requirement to link against libftdi1, libusb1, or
# hidapi (hidapi-hidraw on Linux). This makes building on Windows in particular easier.
ifneq (,$(findstring linux, $(SYS)))
    HOSTED_BMP_ONLY ?= 0
else
    HOSTED_BMP_ONLY ?= 1
endif
CFLAGS += -DHOSTED_BMP_ONLY=$(HOSTED_BMP_ONLY)

ifeq ($(ASAN), 1)
    CFLAGS += -fsanitize=address
    ifeq (, $(findstring darwin,$(SYS)))
        CFLAGS += -Wno-format-truncation
    endif
    LDFLAGS += -fsanitize=address
    OPT_FLAGS += -g
endif

HIDAPILIB = hidapi
ifneq (,$(findstring linux,$(SYS)))
    SRC += serial_unix.c
    HIDAPILIB = hidapi-hidraw
else ifneq (,$(findstring mingw,$(SYS)))
    FLAVOUR = $(shell which $(CC))
    ifneq (,$(findstring ucrt64,$(FLAVOUR)))
        CFLAGS += -mcrtdll=ucrt
        LDFLAGS += -mcrtdll=ucrt
    else ifneq (,$(findstring clang,$(FLAVOUR)))
    else
        CFLAGS += -mcrtdll=msvcrt
        LDFLAGS += -mcrtdll=msvcrt
    endif

    # Build for windows versions Vista, and above, where the
    # 'SetupDiGetDevicePropertyW()' function is available
    CFLAGS += -D_WIN32_WINNT=0x600
    SRC += serial_win.c
    LDFLAGS += -lws2_32 -lsetupapi
else ifneq (,$(findstring cygwin,$(SYS)))
    # Build for windows versions Vista, and above, where the
    # 'SetupDiGetDevicePropertyW()' function is available
    CFLAGS += -D_WIN32_WINNT=0x600
    SRC += serial_win.c
    LDFLAGS += -lws2_32 -lsetupapi
# https://github.com/dmlc/xgboost/issues/1945 indicates macosx as indicator
else ifneq (filter, macosx darwin, $(SYS))
    SRC += serial_unix.c
    LDFLAGS += -framework CoreFoundation
    CFLAGS += -I /opt/homebrew/include -I /opt/homebrew/include/libusb-1.0
endif

# If we're on Windows, pick the correct FTD2xx implementation
ifeq ($(OS), Windows_NT)
    ifneq ($(HOSTED_BMP_ONLY), 1)
        VPATH += platforms/hosted/windows
        CFLAGS += -I../3rdparty/ftdi -Iplatforms/hosted/windows
        ifeq ($(PROCESSOR_ARCHITECTURE), AMD64)
            LDFLAGS += ../3rdparty/ftdi/amd64/ftd2xx.lib
            FTDI_DLL = ../3rdparty/ftdi/amd64/ftd2xx.dll
        else
            LDFLAGS += ../3rdparty/ftdi/i386/ftd2xx.lib
            FTDI_DLL = ../3rdparty/ftdi/i386/ftd2xx.dll
        endif
    endif
endif

ifneq ($(HOSTED_BMP_ONLY), 1)
    ifneq ($(shell pkg-config --exists libusb-1.0; echo $$?), 0)
        $(error Please install libusb-1.0 dependency or set HOSTED_BMP_ONLY to 1)
    endif
    ifneq ($(OS), Windows_NT)
        ifneq ($(shell pkg-config --exists libftdi1; echo $$?), 0)
            $(error Please install libftdi1 dependency or set HOSTED_BMP_ONLY to 1)
        endif
        CFLAGS += $(shell pkg-config --cflags libftdi1)
        LDFLAGS += $(shell pkg-config --libs libftdi1)
    endif
    CFLAGS += $(shell pkg-config --cflags libusb-1.0)
    LDFLAGS += $(shell pkg-config --libs libusb-1.0)
    CFLAGS += -Wno-missing-field-initializers
endif

ifneq ($(HOSTED_BMP_ONLY), 1)
    CFLAGS += -DCMSIS_DAP
    SRC += cmsis_dap.c dap.c dap_command.c dap_swd.c dap_jtag.c
    ifneq ($(shell pkg-config --exists $(HIDAPILIB); echo $$?), 0)
        $(error Please install $(HIDAPILIB) dependency or set HOSTED_BMP_ONLY to 1)
    endif
    CFLAGS += $(shell pkg-config --cflags $(HIDAPILIB))
    LDFLAGS += $(shell pkg-config --libs $(HIDAPILIB))
endif

VPATH += platforms/hosted/remote

SRC += platform.c
SRC += timing.c cli.c utils.c probe_info.c debug.c
SRC += protocol_v0.c protocol_v0_swd.c protocol_v0_jtag.c protocol_v0_adiv5.c
SRC += protocol_v1.c protocol_v1_adiv5.c protocol_v2.c
SRC += protocol_v3.c protocol_v3_adiv5.c
SRC += bmp_remote.c
ifneq ($(HOSTED_BMP_ONLY), 1)
    ifeq ($(OS), Windows_NT)
        SRC += ftd2xx.dll ftdi.c
    endif
    SRC += bmp_libusb.c stlinkv2.c stlinkv2_jtag.c stlinkv2_swd.c
    SRC += ftdi_bmp.c ftdi_jtag.c ftdi_swd.c
    SRC += jlink.c jlink_jtag.c jlink_swd.c
else
    SRC += bmp_serial.c
endif
PC_HOSTED = 1

all: blackmagic

ifeq ($(OS), Windows_NT)
.PHONY: ftd2xx.dll

ftd2xx.dll:
	cp $(FTDI_DLL) ../src
endif
host_clean:
	-$(Q)$(RM) blackmagic
