CROSS_COMPILE ?= arm-none-eabi-
SERIAL_NO ?= 1
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

INCLUDES = -I../deps/libopencm3/include

CPU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += $(INCLUDES) $(CPU_FLAGS) -DSERIAL_NO=$(SERIAL_NO) -DTARGET_IS_BLIZZARD_RB1 -DLM4F -DPART_TM4C123GH6PM
CFLAGS += -DDFU_SERIAL_LENGTH=9

LDFLAGS = -lopencm3_lm4f -Lplatforms/launchpad-icdi \
	-Tlaunchpad-icdi.ld -nostartfiles -nodefaultlibs -lc \
	-lnosys -lm -lgcc -Wl,--gc-sections $(CPU_FLAGS) \
	-L../deps/libopencm3/lib

VPATH += platforms/common/tm4c

SRC +=          \
	platform.c \
	timing.c        \
	traceswo.o

all: blackmagic.bin
