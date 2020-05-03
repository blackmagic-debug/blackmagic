CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

INCLUDES = -I../libopencm3/include

CPU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += $(INCLUDES) $(CPU_FLAGS) -DLM4F -DPART_TM4C123GH6PM

LINKER_SCRIPT="platforms/tm4c/tm4c.ld"
LDFLAGS = -nostartfiles -lc $(CPU_FLAGS) -nodefaultlibs -T$(LINKER_SCRIPT) -Wl,--gc-sections \
	-L../libopencm3/lib -lopencm3_lm4f -lnosys -lm -lgcc

VPATH += platforms/tm4c

SRC +=	cdcacm.c	\
	usbuart.c	\
	timing.c	\
	traceswo.o

ifeq ($(PROBE_HOST_VARIANT), target)
CFLAGS += -DLAUNCHPAD_TARGET
else
CFLAGS += -DLAUNCHPAD_ICDI
endif

ifeq ($(ENABLE_DEBUG),1)
ifeq ($(PROBE_HOST_VARIANT), target)
SRC += debuguart.c
else
$(warning Debug not supported on Launchpad ICDI)
endif
endif

all: blackmagic.bin

