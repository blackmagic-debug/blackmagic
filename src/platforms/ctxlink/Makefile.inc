CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-DSTM32F4 -DCTX_LINK -DDFU_SERIAL_LENGTH=13 -I../deps/libopencm3/include \
	-Iplatforms/common/stm32

LDFLAGS = -lopencm3_stm32f4 \
	-Tctxlink.ld -nostartfiles -lc -lnosys \
	-Lplatforms/ctxlink \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../deps/libopencm3/lib

VPATH += platforms/common/stm32

SRC +=               \
	platform.c \
	traceswodecode.c \
	serialno.c	\
	timing.c	\
	timing_stm32.c

ifeq ($(TRACESWO_PROTOCOL), 1)
SRC += traceswo.c
CFLAGS += -DTRACESWO_PROTOCOL=1
else
SRC += traceswoasync.c
CFLAGS += -DTRACESWO_PROTOCOL=2
endif

all:	blackmagic.bin

host_clean:
	-$(Q)$(RM) blackmagic.bin
