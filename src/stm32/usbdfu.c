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
#include <libopencm3/stm32/systick.h>
#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/f1/gpio.h>
#include <libopencm3/stm32/f1/flash.h>
#include <libopencm3/stm32/f1/scb.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>

#define APP_ADDRESS	0x08002000

/* Commands sent with wBlockNum == 0 as per ST implementation. */
#define CMD_SETADDR	0x21 
#define CMD_ERASE	0x41 

/* We need a special large control buffer for this device: */
u8 usbd_control_buffer[1024];

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
        .idVendor = 0x0483,
        .idProduct = 0xDF11,
        .bcdDevice = 0x0200,
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

static char serial_no[25];

static const char *usb_strings[] = {
	"x",
	"Black Sphere Technologies",
	"Black Magic Probe (Upgrade)",
	serial_no,
	/* This string is used by ST Microelectronics' DfuSe utility */
	"@Internal Flash   /0x08000000/8*001Ka,120*001Kg"
};

static u8 usbdfu_getstatus(u32 *bwPollTimeout)
{
	switch(usbdfu_state) {
	case STATE_DFU_DNLOAD_SYNC:
		usbdfu_state = STATE_DFU_DNBUSY; 
		*bwPollTimeout = 100;
		return DFU_STATUS_OK;

	case STATE_DFU_MANIFEST_SYNC:
		/* Device will reset when read is complete */
		usbdfu_state = STATE_DFU_MANIFEST;
		return DFU_STATUS_OK;

	default:
		return DFU_STATUS_OK;
	}
}

static void usbdfu_getstatus_complete(struct usb_setup_data *req)
{
	int i;
	(void)req;

	switch(usbdfu_state) {
	case STATE_DFU_DNBUSY:

		flash_unlock();
		if(prog.blocknum == 0) {
			switch(prog.buf[0]) {
			case CMD_ERASE:
				flash_erase_page(*(u32*)(prog.buf+1));
			case CMD_SETADDR:
				prog.addr = *(u32*)(prog.buf+1);
			}
		} else {
			u32 baseaddr = prog.addr + 
				((prog.blocknum - 2) * 
					dfu_function.wTransferSize);
			for(i = 0; i < prog.len; i += 2) 
				flash_program_half_word(baseaddr + i, 
						*(u16*)(prog.buf+i));
		}
		flash_lock();

		/* We jump straight to dfuDNLOAD-IDLE, 
		 * skipping dfuDNLOAD-SYNC 
		 */
		usbdfu_state = STATE_DFU_DNLOAD_IDLE;
		return;

	case STATE_DFU_MANIFEST:
		/* USB device must detach, we just reset... */
		scb_reset_system();
		return; /* Will never return */
	default:
		return;
	}
}

static int usbdfu_control_request(struct usb_setup_data *req, u8 **buf, 
		u16 *len, void (**complete)(struct usb_setup_data *req))
{

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
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);
	if(gpio_get(GPIOB, GPIO12)) {
		/* Boot the application if it's valid */
		if((*(volatile u32*)APP_ADDRESS & 0x2FFE0000) == 0x20000000) {
			/* Set vector table base address */
			SCB_VTOR = APP_ADDRESS & 0xFFFF;
			/* Initialise master stack pointer */
			asm volatile ("msr msp, %0"::"g"
					(*(volatile u32*)APP_ADDRESS));
			/* Jump to application */
			(*(void(**)())(APP_ADDRESS + 4))();
		}
	}

	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);

	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 0, GPIO8);

	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO11);
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8); 
	systick_set_reload(900000);
	systick_interrupt_enable();
	systick_counter_enable();
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, 
			GPIO_CNF_INPUT_FLOAT, GPIO2 | GPIO10);

	get_dev_unique_id(serial_no);

	usbd_init(&stm32f103_usb_driver, &dev, &config, usb_strings);
	usbd_set_control_buffer_size(sizeof(usbd_control_buffer));
	usbd_register_control_callback(
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				usbdfu_control_request);

	gpio_set(GPIOA, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);

	while (1) 
		usbd_poll();
}

static char *get_dev_unique_id(char *s) 
{
        volatile uint8_t *unique_id = (volatile uint8_t *)0x1FFFF7E8;
        int i;

        /* Fetch serial number from chip's unique ID */
        for(i = 0; i < 24; i+=2) {
                s[i] = ((*unique_id >> 4) & 0xF) + '0';
                s[i+1] = (*unique_id++ & 0xF) + '0';
        }
        for(i = 0; i < 24; i++) 
                if(s[i] > '9') 
                        s[i] += 'A' - '9' - 1;

	return s;
}

void sys_tick_handler()
{
	gpio_toggle(GPIOB, GPIO11); /* LED2 on/off */
}

