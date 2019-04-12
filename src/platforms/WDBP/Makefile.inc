CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 -Wno-unused-value -Wno-unused-parameter \
	-Wimplicit-fallthrough=0 \
	-DWDBP -DSTM32F4 -DF4DISCOVERY -I../libopencm3/include \
	-I./platforms/winc1500 -I./platforms/winc1500/src \
	-Iplatforms/stm32

LDFLAGS = -lopencm3_stm32f4 -Wl,--defsym,_stack=0x20006000 \
	-Wl,-T,platforms/stm32/f4discovery.ld -nostartfiles -lc -lnosys \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../libopencm3/lib

VPATH += platforms/stm32 platforms/winc1500 platforms/winc1500/src

SRC += 	cdcacm.c	\
	traceswo.c	\
	usbuart.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\
	Wifi_Server.c \
	wdbp_mode_led.c \
	winc1500_driver_stub_api.c \
	wf_asic.c \
	wf_hif.c \
	wf_ota.c \
	wf_socket.c \
	wf_spi.c \
	wf_spi_flash.c \
	wf_utils.c \
	wf_wifi.c \

all:	blackmagic.bin

host_clean:
	-$(Q)$(RM) blackmagic.bin
