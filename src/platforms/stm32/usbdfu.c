/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#if defined(STM32F1)
#include <libopencm3/stm32/f1/flash.h>
#elif defined(STM32F2)
#include <libopencm3/stm32/f2/flash.h>
#elif defined(STM32F4)
#include <libopencm3/stm32/f4/flash.h>
#else
#warning "Unhandled STM32 family"
#endif
#include <libopencm3/cm3/scb.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>

/* Commands sent with wBlockNum == 0 as per ST implementation. */
#define CMD_SETADDR	0x21
#define CMD_ERASE	0x41

#define FLASH_OBP_RDP 0x1FFFF800
#define FLASH_OBP_WRP10 0x1FFFF808

#define FLASH_OBP_RDP_KEY 0x5aa5

usbd_device *usbdev;
/* We need a special large control buffer for this device: */
u8 usbd_control_buffer[1024];

#if defined (STM32_CAN)
#define FLASHBLOCKSIZE 2048
#else
#define FLASHBLOCKSIZE 1024
#endif

#if defined(DISCOVERY_STLINK)
u32 led2_state = 0;
#endif

static u32 max_address;
#if defined (STM32F4)
#define APP_ADDRESS	0x08010000
static u32 sector_addr[] = {0x8000000, 0x8004000, 0x8008000, 0x800c000,
                            0x8010000, 0x8020000, 0x8040000, 0x8060000,
                            0x8080000, 0x80a0000, 0x80c0000, 0x80e0000,
                            0x8100000, 0};
u16 sector_erase_time[12]= {500, 500, 500, 500,
                            1100,
                            2600, 2600, 2600, 2600, 2600, 2600, 2600};
u8 sector_num = 0xff;
/* Find the sector number for a given address*/
void get_sector_num(u32 addr)
{
	int i = 0;
	while(sector_addr[i+1]) {
		if (addr < sector_addr[i+1])
			break;
		i++;
		}
	if (!sector_addr[i])
		return;
	sector_num = i;
}

void check_and_do_sector_erase(u32 addr)
{
	if(addr == sector_addr[sector_num]) {
		flash_erase_sector((sector_num & 0x1f)<<3, FLASH_PROGRAM_X32);
	}
}
#else
#define APP_ADDRESS	0x08002000
static uint32_t last_erased_page=0xffffffff;
void check_and_do_sector_erase(u32 sector)
{
	sector &= (~(FLASHBLOCKSIZE-1));
	if (sector != last_erased_page) {
		flash_erase_page(sector);
		last_erased_page = sector;
	}
}
#endif

static enum dfu_state usbdfu_state = STATE_DFU_IDLE;

static char *get_dev_unique_id(char *serial_no);

static struct {
	u8 buf[sizeof(usbd_control_buffer)];
	u16 len;
	u32 addr;
	u16 blocknum;
} prog;

const struct usb_device_descriptor dev = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = 0x1D50,
        .idProduct = 0x6017,
        .bcdDevice = 0x0100,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};

const struct usb_dfu_descriptor dfu_function = {
	.bLength = sizeof(struct usb_dfu_descriptor),
	.bDescriptorType = DFU_FUNCTIONAL,
	.bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
	.wDetachTimeout = 255,
	.wTransferSize = 1024,
	.bcdDFUVersion = 0x011A,
};

const struct usb_interface_descriptor iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xFE, /* Device Firmware Upgrade */
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 2,

	/* The ST Microelectronics DfuSe application needs this string.
	 * The format isn't documented... */
	.iInterface = 4,

	.extra = &dfu_function,
	.extralen = sizeof(dfu_function),
};

const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = &iface,
}};

const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0xC0,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static char serial_no[9];

static const char *usb_strings[] = {
	"Black Sphere Technologies",
#if defined(BLACKMAGIC)
	"Black Magic Probe (Upgrade)",
#elif defined(DISCOVERY_STLINK)
	"Black Magic (Upgrade) for STLink/Discovery",
#elif defined(STM32_CAN)
	"Black Magic (Upgrade) for STM32_CAN",
#elif defined(F4DISCOVERY)
	"Black Magic (Upgrade) for F4DISCOVERY",
#else
#warning "Unhandled board"
#endif
	serial_no,
	/* This string is used by ST Microelectronics' DfuSe utility */
#if defined(BLACKMAGIC)
	"@Internal Flash   /0x08000000/8*001Ka,120*001Kg"
#elif defined(DISCOVERY_STLINK)
	"@Internal Flash   /0x08000000/8*001Ka,56*001Kg"
#elif defined(STM32_CAN)
	"@Internal Flash   /0x08000000/4*002Ka,124*002Kg"
#elif defined(F4DISCOVERY)
	"@Internal Flash   /0x08000000/1*016Ka,3*016Kg,1*064Kg,7*128Kg"
#else
#warning "Unhandled board"
#endif
};

static u8 usbdfu_getstatus(u32 *bwPollTimeout)
{
	switch(usbdfu_state) {
	case STATE_DFU_DNLOAD_SYNC:
		usbdfu_state = STATE_DFU_DNBUSY;
#if defined(STM32F4)
                /* Programming 256 word with 100 us(max) per word*/
		*bwPollTimeout = 26;
		/* Erase for big pages on STM2/4 needs "long" time
                   Try not to hit USB timeouts*/
		if ((prog.blocknum == 0) && (prog.buf[0] == CMD_ERASE)) {
			u32 addr = *(u32 *)(prog.buf + 1);
			get_sector_num(addr);
			if(addr == sector_addr[sector_num])
				*bwPollTimeout = sector_erase_time[sector_num];
		}
#else
		*bwPollTimeout = 100;
#endif
		return DFU_STATUS_OK;

	case STATE_DFU_MANIFEST_SYNC:
		/* Device will reset when read is complete */
		usbdfu_state = STATE_DFU_MANIFEST;
		return DFU_STATUS_OK;

	default:
		return DFU_STATUS_OK;
	}
}

static void
usbdfu_getstatus_complete(usbd_device *dev, struct usb_setup_data *req)
{
	int i;
	(void)req;

	switch(usbdfu_state) {
	case STATE_DFU_DNBUSY:

		flash_unlock();
		if(prog.blocknum == 0) {
			u32 addr = *(u32 *)(prog.buf + 1);
			if (addr < APP_ADDRESS ||
			    (addr >= max_address)) {
				flash_lock();
				usbd_ep_stall_set(dev, 0, 1);
				return;
			}
			switch(prog.buf[0]) {
			case CMD_ERASE:
				check_and_do_sector_erase(addr);
			case CMD_SETADDR:
				prog.addr = addr;
			}
		} else {
			u32 baseaddr = prog.addr +
				((prog.blocknum - 2) *
					dfu_function.wTransferSize);
#if defined (STM32F4)
			for(i = 0; i < prog.len; i += 4)
				flash_program_word(baseaddr + i,
					*(u32*)(prog.buf+i),
					FLASH_PROGRAM_X32);
#else
			for(i = 0; i < prog.len; i += 2)
				flash_program_half_word(baseaddr + i,
						*(u16*)(prog.buf+i));
#endif
		}
		flash_lock();

		/* We jump straight to dfuDNLOAD-IDLE,
		 * skipping dfuDNLOAD-SYNC
		 */
		usbdfu_state = STATE_DFU_DNLOAD_IDLE;
		return;

	case STATE_DFU_MANIFEST:
#if defined (DISCOVERY_STLINK)
		/* Disconnect USB cable by resetting USB Device
                   and pulling USB_DP low*/
		rcc_peripheral_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
		rcc_peripheral_clear_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
		rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
		rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
		gpio_clear(GPIOA, GPIO12);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
#else
        /* USB device must detach, we just reset... */
#endif
		scb_reset_system();
		return; /* Will never return */
	default:
		return;
	}
}

static int usbdfu_control_request(usbd_device *dev,
		struct usb_setup_data *req, u8 **buf, u16 *len,
		void (**complete)(usbd_device *dev, struct usb_setup_data *req))
{
	(void)dev;

	if((req->bmRequestType & 0x7F) != 0x21)
		return 0; /* Only accept class request */

	switch(req->bRequest) {
	case DFU_DNLOAD:
		if((len == NULL) || (*len == 0)) {
			usbdfu_state = STATE_DFU_MANIFEST_SYNC;
			return 1;
		} else {
			/* Copy download data for use on GET_STATUS */
			prog.blocknum = req->wValue;
			prog.len = *len;
			memcpy(prog.buf, *buf, *len);
			usbdfu_state = STATE_DFU_DNLOAD_SYNC;
			return 1;
		}
	case DFU_CLRSTATUS:
		/* Clear error and return to dfuIDLE */
		if(usbdfu_state == STATE_DFU_ERROR)
			usbdfu_state = STATE_DFU_IDLE;
		return 1;
	case DFU_ABORT:
		/* Abort returns to dfuIDLE state */
		usbdfu_state = STATE_DFU_IDLE;
		return 1;
	case DFU_UPLOAD:
		/* Upload not supported for now */
		return 0;
	case DFU_GETSTATUS: {
		u32 bwPollTimeout = 0; /* 24-bit integer in DFU class spec */

		(*buf)[0] = usbdfu_getstatus(&bwPollTimeout);
		(*buf)[1] = bwPollTimeout & 0xFF;
		(*buf)[2] = (bwPollTimeout >> 8) & 0xFF;
		(*buf)[3] = (bwPollTimeout >> 16) & 0xFF;
		(*buf)[4] = usbdfu_state;
		(*buf)[5] = 0;	/* iString not used here */
		*len = 6;

		*complete = usbdfu_getstatus_complete;

		return 1;
		}
	case DFU_GETSTATE:
		/* Return state with no state transision */
		*buf[0] = usbdfu_state;
		*len = 1;
		return 1;
	}

	return 0;
}

int main(void)
{
#if defined (DISCOVERY_STLINK)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPCEN);
	if(!gpio_get(GPIOC, GPIO13)) {
#elif defined (STM32_CAN)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	if(!gpio_get(GPIOA, GPIO0)) {
#elif defined (F4DISCOVERY)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPAEN);
	if(!gpio_get(GPIOA, GPIO0)) {
#else
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	if(gpio_get(GPIOB, GPIO12)) {
#endif
		/* Boot the application if it's valid */
#if defined (STM32F4)
		/* Vector table may be anywhere in 128 kByte RAM
                   CCM not handled*/
		if((*(volatile u32*)APP_ADDRESS & 0x2FFC0000) == 0x20000000) {
#else
		if((*(volatile u32*)APP_ADDRESS & 0x2FFE0000) == 0x20000000) {
#endif
			/* Set vector table base address */
			SCB_VTOR = APP_ADDRESS & 0x1FFFFF; /* Max 2 MByte Flash*/
			/* Initialise master stack pointer */
			asm volatile ("msr msp, %0"::"g"
					(*(volatile u32*)APP_ADDRESS));
			/* Jump to application */
			(*(void(**)())(APP_ADDRESS + 4))();
		}
	}

#if defined (STM32F4)
        /* don' touch option bits for now */
#else
	if ((FLASH_WRPR & 0x03) != 0x00) {
		flash_unlock();
		FLASH_CR = 0;
		flash_erase_option_bytes();
		flash_program_option_bytes(FLASH_OBP_RDP, FLASH_OBP_RDP_KEY);
		/* CL Device: Protect 2 bits with (2 * 2k pages each)*/
		/* MD Device: Protect 2 bits with (4 * 1k pages each)*/
		flash_program_option_bytes(FLASH_OBP_WRP10, 0x03FC);
	}
#endif

#if defined (DISCOVERY_STLINK)
	/* Just in case: Disconnect USB cable by resetting USB Device
	and pulling USB_DP low*/
	rcc_peripheral_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_clear_reset(&RCC_APB1RSTR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
#endif
#if defined (F4DISCOVERY)
        rcc_clock_setup_hse_3v3(&hse_8mhz_3v3[CLOCK_3V3_168MHZ]);
#else
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
#endif

#if defined(DISCOVERY_STLINK)
#elif defined(F4DISCOVERY)
#elif defined (STM32_CAN)
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_AHBENR, RCC_AHBENR_OTGFSEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO0);
#else
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
        rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 0, GPIO8);

	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO11);
#endif
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8);
	systick_set_reload(2100000);
	systick_interrupt_enable();
	systick_counter_enable();
	get_dev_unique_id(serial_no);

#if defined(STM32_CAN)
	usbdev = usbd_init(&stm32f107_usb_driver,
				&dev, &config, usb_strings, 4);
#elif defined(F4DISCOVERY)
	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_IOPDEN);
	gpio_clear(GPIOD, GPIO12 | GPIO13 | GPIO14 |GPIO15);
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			GPIO12 | GPIO13 | GPIO14 |GPIO15);

	rcc_peripheral_enable_clock(&RCC_AHB2ENR, RCC_AHB2ENR_OTGFSEN);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
		GPIO9 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);

	usbdev = usbd_init(&stm32f107_usb_driver,
				&dev, &config, usb_strings, 4);
#else
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT, GPIO2 | GPIO10);

	usbdev = usbd_init(&stm32f103_usb_driver,
				&dev, &config, usb_strings, 4);
#endif
	usbd_set_control_buffer_size(usbdev, sizeof(usbd_control_buffer));
	usbd_register_control_callback(usbdev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				usbdfu_control_request);

#if defined(BLACKMAGIG)
	gpio_set(GPIOA, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);
#endif

	while (1)
		usbd_poll(usbdev);
}

static char *get_dev_unique_id(char *s)
{
#if defined(STM32F4) || defined(STM32F2)
#define UNIQUE_SERIAL_R 0x1FFF7A10
#define FLASH_SIZE_R    0x1fff7A22
#elif defined(STM32F3)
#define UNIQUE_SERIAL_R 0x1FFFF7AC
#define FLASH_SIZE_R    0x1fff77cc
#elif defined(STM32L1)
#define UNIQUE_SERIAL_R 0x1ff80050
#define FLASH_SIZE_R    0x1FF8004C
#else
#define UNIQUE_SERIAL_R 0x1FFFF7E8;
#define FLASH_SIZE_R    0x1ffff7e0
#endif
        volatile uint32_t *unique_id_p = (volatile uint32_t *)UNIQUE_SERIAL_R;
	uint32_t unique_id = *unique_id_p +
			*(unique_id_p + 1) +
			*(unique_id_p + 2);
        int i;

        /* Calculated the upper flash limit from the exported data
           in theparameter block*/
        max_address = (*(u32 *) FLASH_SIZE_R) <<10;
        /* Fetch serial number from chip's unique ID */
        for(i = 0; i < 8; i++) {
                s[7-i] = ((unique_id >> (4*i)) & 0xF) + '0';
        }
        for(i = 0; i < 8; i++)
                if(s[i] > '9')
                        s[i] += 'A' - '9' - 1;
	s[8] = 0;

	return s;
}

void sys_tick_handler()
{
#if defined(DISCOVERY_STLINK)
	if (led2_state & 1)
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO9);
	else
		gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
		GPIO_CNF_INPUT_ANALOG, GPIO9);
	led2_state++;
#elif defined (F4DISCOVERY)
	gpio_toggle(GPIOD, GPIO12);  /* Green LED on/off */
#elif defined(STM32_CAN)
	gpio_toggle(GPIOB, GPIO0);  /* LED2 on/off */
#else
	gpio_toggle(GPIOB, GPIO11); /* LED2 on/off */
#endif
}

