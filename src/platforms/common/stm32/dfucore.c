/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include "general.h"
#include "platform.h"
#include "version.h"
#include "serialno.h"
#include <string.h>

#include <libopencm3/stm32/desig.h>

#if defined(STM32F1HD)
#define DFU_IFACE_STRING        "@Internal Flash   /0x08000000/4*002Ka,000*002Kg"
#define DFU_IFACE_STRING_OFFSET 38
#define DFU_IFACE_PAGESIZE      2
#elif defined(STM32F1)
#define DFU_IFACE_STRING        "@Internal Flash   /0x08000000/8*001Ka,000*001Kg"
#define DFU_IFACE_STRING_OFFSET 38
#define DFU_IFACE_PAGESIZE      1
#elif defined(STM32F4) || defined(STM32F7)
#define DFU_IFACE_PAGESIZE 128
#if APP_START == 0x08020000
#define DFU_IFACE_STRING_OFFSET 62
#define DFU_IFACE_STRING        "@Internal Flash   /0x08000000/1*016Ka,3*016Ka,1*064Ka,1*128Kg,002*128Kg"
#elif APP_START == 0x08004000
#define DFU_IFACE_STRING_OFFSET 54
#define DFU_IFACE_STRING        "@Internal Flash   /0x08000000/1*016Ka,3*016Kg,1*064Kg,000*128Kg"
#endif
#endif
#include <libopencm3/stm32/flash.h>

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>

#include "usbdfu.h"
#include "usb_types.h"

usbd_device *usbdev;
/* We need a special large control buffer for this device: */
uint8_t usbd_control_buffer[1024];

static uint32_t max_address;

static dfu_state_e usbdfu_state = STATE_DFU_IDLE;

static void get_dev_unique_id(void);

static struct {
	uint8_t buf[sizeof(usbd_control_buffer)];
	uint16_t len;
	uint32_t addr;
	uint16_t blocknum;
} prog;

static uint8_t current_error;

const usb_device_descriptor_s dev_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x1d50,
	.idProduct = 0x6017,
	.bcdDevice = 0x0100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

const usb_dfu_descriptor_s dfu_function = {
	.bLength = sizeof(usb_dfu_descriptor_s),
	.bDescriptorType = DFU_FUNCTIONAL,
	.bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_CAN_UPLOAD | USB_DFU_WILL_DETACH,
	.wDetachTimeout = 255,
	.wTransferSize = 1024,
	.bcdDFUVersion = 0x011a,
};

const usb_interface_descriptor_s iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xfe, /* Device Firmware Upgrade */
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 2,

	/* The ST Microelectronics DfuSe application needs this string.
	 * The format isn't documented... */
	.iInterface = 4,

	.extra = &dfu_function,
	.extralen = sizeof(dfu_function),
};

const usb_interface_s ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = &iface,
}};

const usb_config_descriptor_s config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0xc0,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

#define BOARD_IDENT_DFU(BOARD_TYPE) "Black Magic Probe DFU " PLATFORM_IDENT "" FIRMWARE_VERSION

/* Because of shenanigans below, this intentionally creates a runtime copy of the string. */
static char if_string[] = DFU_IFACE_STRING;

static const char *const usb_strings[] = {
	"Black Magic Debug",
	BOARD_IDENT_DFU(PLATFORM_IDENT),
	serial_no,
	/* This string is used by ST Microelectronics' DfuSe utility */
	if_string,
};

static uint32_t get_le32(const void *vp)
{
	const uint8_t *p = vp;
	return ((uint32_t)p[3] << 24U) + ((uint32_t)p[2] << 16U) + (p[1] << 8U) + p[0];
}

static uint8_t usbdfu_getstatus(uint32_t *poll_timeout)
{
	switch (usbdfu_state) {
	case STATE_DFU_DNLOAD_SYNC:
		usbdfu_state = STATE_DFU_DNBUSY;
		*poll_timeout = dfu_poll_timeout(prog.buf[0], get_le32(prog.buf + 1U), prog.blocknum);
		return DFU_STATUS_OK;

	case STATE_DFU_MANIFEST_SYNC:
		/* Device will reset when read is complete */
		usbdfu_state = STATE_DFU_MANIFEST;
		return DFU_STATUS_OK;
	case STATE_DFU_ERROR:
		return current_error;
	default:
		return DFU_STATUS_OK;
	}
}

static void usbdfu_getstatus_complete(usbd_device *dev, usb_setup_data_s *req)
{
	(void)req;
	(void)dev;

	switch (usbdfu_state) {
	case STATE_DFU_DNBUSY:

		flash_unlock();
		if (prog.blocknum == 0) {
			const uint32_t addr = get_le32(prog.buf + 1U);
			switch (prog.buf[0]) {
			case CMD_ERASE:
				if (addr < app_address || addr >= max_address) {
					usbdfu_state = STATE_DFU_ERROR;
					flash_lock();
					return;
				}
				dfu_check_and_do_sector_erase(addr);
			}
		} else {
			const uint32_t baseaddr = prog.addr + ((prog.blocknum - 2U) * dfu_function.wTransferSize);
			dfu_flash_program_buffer(baseaddr, prog.buf, prog.len);
		}
		flash_lock();

		/* We jump straight to dfuDNLOAD-IDLE, skipping dfuDNLOAD-SYNC */
		usbdfu_state = STATE_DFU_DNLOAD_IDLE;
		return;

	case STATE_DFU_MANIFEST:
		dfu_detach();
		return; /* Will never return */
	default:
		return;
	}
}

static usbd_request_return_codes_e usbdfu_control_request(usbd_device *dev, usb_setup_data_s *req, uint8_t **buf,
	uint16_t *len, void (**complete)(usbd_device *dev, usb_setup_data_s *req))
{
	uint8_t *const data = *buf;

	if ((req->bmRequestType & 0x7fU) != 0x21U)
		return USBD_REQ_NOTSUPP; /* Only accept class request */

	switch (req->bRequest) {
	case DFU_DNLOAD:
		if (len == NULL || *len == 0) {
			usbdfu_state = STATE_DFU_MANIFEST_SYNC;
			return USBD_REQ_HANDLED;
		} else {
			/* Copy download data for use on GET_STATUS */
			prog.blocknum = req->wValue;
			prog.len = *len;
			memcpy(prog.buf, data, *len);
			if (req->wValue == 0 && prog.buf[0] == CMD_SETADDR) {
				uint32_t addr = get_le32(prog.buf + 1U);
				if (addr < app_address || addr >= max_address) {
					current_error = DFU_STATUS_ERR_TARGET;
					usbdfu_state = STATE_DFU_ERROR;
					return USBD_REQ_HANDLED;
				}
				prog.addr = addr;
			}
			usbdfu_state = STATE_DFU_DNLOAD_SYNC;
			return USBD_REQ_HANDLED;
		}
	case DFU_CLRSTATUS:
		/* Clear error and return to dfuIDLE */
		if (usbdfu_state == STATE_DFU_ERROR)
			usbdfu_state = STATE_DFU_IDLE;
		return USBD_REQ_HANDLED;
	case DFU_ABORT:
		/* Abort returns to dfuIDLE state */
		usbdfu_state = STATE_DFU_IDLE;
		return USBD_REQ_HANDLED;
	case DFU_UPLOAD:
		if (usbdfu_state == STATE_DFU_IDLE || usbdfu_state == STATE_DFU_DNLOAD_IDLE ||
			usbdfu_state == STATE_DFU_UPLOAD_IDLE) {
			prog.blocknum = req->wValue;
			usbdfu_state = STATE_DFU_UPLOAD_IDLE;
			if (prog.blocknum > 1U) {
				const uintptr_t baseaddr = prog.addr + ((prog.blocknum - 2U) * dfu_function.wTransferSize);
				memcpy(data, (void *)baseaddr, *len);
			}
			return USBD_REQ_HANDLED;
		} else {
			usbd_ep_stall_set(dev, 0, 1);
			return USBD_REQ_NOTSUPP;
		}
	case DFU_GETSTATUS: {
		uint32_t poll_timeout = 0; /* 24-bit integer in DFU class spec */

		data[0] = usbdfu_getstatus(&poll_timeout);
		data[1] = poll_timeout & 0xffU;
		data[2] = (poll_timeout >> 8U) & 0xffU;
		data[3] = (poll_timeout >> 16U) & 0xffU;
		data[4] = usbdfu_state;
		data[5] = 0; /* iString not used here */
		*len = 6;

		*complete = usbdfu_getstatus_complete;
		return USBD_REQ_HANDLED;
	}
	case DFU_GETSTATE:
		/* Return state with no state transition */
		data[0] = usbdfu_state;
		*len = 1;
		return USBD_REQ_HANDLED;
	}

	return USBD_REQ_NOTSUPP;
}

void dfu_init(const usbd_driver *driver)
{
	get_dev_unique_id();

	usbdev = usbd_init(driver, &dev_desc, &config, usb_strings, 4, usbd_control_buffer, sizeof(usbd_control_buffer));

	usbd_register_control_callback(usbdev, USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, usbdfu_control_request);
}

void dfu_main(void)
{
	while (true)
		usbd_poll(usbdev);
}

#if defined(DFU_IFACE_STRING_OFFSET)
static void set_dfu_iface_string(uint32_t size)
{
	char *const p = if_string + DFU_IFACE_STRING_OFFSET;
#if DFU_IFACE_PAGESIZE > 1
	size /= DFU_IFACE_PAGESIZE;
#endif
	/*
	 * We do not want the whole printf library in the bootloader.
	 * Fill the size digits by hand.
	 */
	if (size >= 999) {
		p[0] = '9';
		p[1] = '9';
		p[2] = '9';
		return;
	}
	p[2] = (char)(48U + (size % 10U));
	size /= 10U;
	p[1] = (char)(48U + (size % 10U));
	size /= 10U;
	p[0] = (char)(48U + size);
}
#else
#define set_dfu_iface_string(x)
#endif

static void get_dev_unique_id(void)
{
	/* Calculated the upper flash limit from the exported data in the parameter block*/
	uint32_t fuse_flash_size = desig_get_flash_size();
	/* Handle F103x8 as F103xB. */
	if (fuse_flash_size == 0x40U)
		fuse_flash_size = 0x80U;
	set_dfu_iface_string(fuse_flash_size - 8U);
	max_address = FLASH_BASE + (fuse_flash_size << 10U);
	read_serial_number();
}
