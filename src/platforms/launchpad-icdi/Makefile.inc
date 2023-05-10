CROSS_COMPILE ?= arm-none-eabi-
SERIAL_NO ?= 1
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

INCLUDES = -I../libopencm3/include

CPU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += $(INCLUDES) $(CPU_FLAGS) -DSERIAL_NO=$(SERIAL_NO) -DTARGET_IS_BLIZZARD_RB1 -DLM4F -DPART_TM4C123GH6PM
CFLAGS += -DDFU_SERIAL_LENGTH=9

LINKER_SCRIPT="platforms/launchpad-icdi/launchpad-icdi.ld"
LDFLAGS = -nostartfiles -lc $(CPU_FLAGS) -nodefaultlibs -T$(LINKER_SCRIPT) -Wl,--gc-sections \
	-L../libopencm3/lib -lopencm3_lm4f -lnosys -lm -lgcc

VPATH += platforms/common/tm4c

SRC +=          \
	platform.c \
	timing.c        \
	traceswo.o

all: blackmagic.bin
