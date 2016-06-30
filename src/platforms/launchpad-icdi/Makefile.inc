CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

INCLUDES = -I../libopencm3/include

CPU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += $(INCLUDES) $(CPU_FLAGS) -DTARGET_IS_BLIZZARD_RB1 -DLM4F -DPART_TM4C123GH6PM

LINKER_SCRIPT="platforms/tm4c/tm4c.ld"
LDFLAGS = -nostartfiles -lc $(CPU_FLAGS) -nodefaultlibs -T$(LINKER_SCRIPT) -Wl,--gc-sections \
	-L../libopencm3/lib -lopencm3_lm4f -lnosys -lm -lgcc
	
VPATH += platforms/tm4c

SRC +=	cdcacm.c	\
	usbuart.c	\
	timing.c        \
	traceswo.o

all: blackmagic.bin

