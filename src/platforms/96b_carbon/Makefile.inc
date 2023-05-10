CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-DSTM32F4 -D_96B_CARBON -I../libopencm3/include \
	-Iplatforms/common/stm32 -DDFU_SERIAL_LENGTH=9

LDFLAGS = -lopencm3_stm32f4 -Wl,--defsym,_stack=0x20006000 \
	-Wl,-T,platforms/96b_carbon/96b_carbon.ld -nostartfiles -lc -lnosys \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../libopencm3/lib

VPATH += platforms/common/stm32

SRC +=               \
	platform.c \
	traceswodecode.c \
	traceswo.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c

all:	blackmagic.bin

host_clean:
	-$(Q)$(RM) blackmagic.bin
