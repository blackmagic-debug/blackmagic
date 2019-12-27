CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

override SYSTICK_RELOAD_VALUE = 1050000

CFLAGS += -Istm32/include -mcpu=cortex-m4 -mthumb \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 -Wno-unused-value -Wno-unused-parameter \
	-Wimplicit-fallthrough=0 \
	-DctxLink -DSTM32F4 -Dsystick_reload=$(SYSTICK_RELOAD_VALUE) -I../libopencm3/include \
	-I./platforms/winc1500 -I./platforms/winc1500/src \
	-Iplatforms/stm32

LDFLAGS = -lopencm3_stm32f4  \
	-Wl,-T,platforms/stm32/ctxLink.ld -nostartfiles -lc -lnosys \
	-Wl,-Map=mapfile -mthumb -mcpu=cortex-m4 -Wl,-gc-sections \
	-Wl,--print-memory-usage \
	-mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	-L../libopencm3/lib

VPATH += platforms/ctxLink platforms/stm32 platforms/winc1500 platforms/winc1500/src

SRC += 	cdcacm.c	\
	traceswo.c	\
	usbuart.c	\
	serialno.c	\
	timing.c	\
	timing_stm32.c	\
	WiFi_Server.c \
	ctxLink_mode_led.c \
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
