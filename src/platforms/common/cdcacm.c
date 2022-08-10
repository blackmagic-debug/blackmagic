/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements a the USB Communications Device Class - Abstract
 * Control Model (CDC-ACM) as defined in CDC PSTN subclass 1.2.
 * A Device Firmware Upgrade (DFU 1.1) class interface is provided for
 * field firmware upgrade.
 *
 * The device's unique id is used as the USB serial number string.
 *
 * Endpoint Usage
 *
 *     0 Control Endpoint
 * IN  1 GDB CDC DATA
 * OUT 1 GDB CDC DATA
 * IN  2 GDB CDC CTR
 * IN  3 UART CDC DATA
 * OUT 3 UART CDC DATA
 * OUT 4 UART CDC CTRL
 * In  5 Trace Capture
 *
 */

#include "general.h"
#include "gdb_if.h"
#include "cdcacm.h"
#if defined(PLATFORM_HAS_TRACESWO)
#	include "traceswo.h"
#endif
#include "usbuart.h"
#include "serialno.h"
#include "version.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/cm3/scb.h>

#include "usb_descriptors.h"

usbd_device * usbdev;

static int configured;
static int cdcacm_gdb_dtr = 1;

static void cdcacm_set_modem_state(usbd_device *dev, int iface, bool dsr, bool dcd);

char serial_no[DFU_SERIAL_LENGTH];

static void dfu_detach_complete(usbd_device *dev, struct usb_setup_data *req)
{
	(void)dev;
	(void)req;

	platform_request_boot();

	/* Reset core to enter bootloader */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
	scb_reset_core();
#endif
}

static enum usbd_request_return_codes  cdcacm_control_request(usbd_device *dev,
		struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
		void (**complete)(usbd_device *dev, struct usb_setup_data *req))
{
	(void)dev;
	(void)complete;
	(void)buf;
	(void)len;

	switch(req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		cdcacm_set_modem_state(dev, req->wIndex, true, true);
        switch(req->wIndex) {
			case UART_IF_NO:
			    #ifdef USBUSART_DTR_PIN
				gpio_set_val(USBUSART_PORT, USBUSART_DTR_PIN, !(req->wValue & 1));
				#endif
				#ifdef USBUSART_RTS_PIN
				gpio_set_val(USBUSART_PORT, USBUSART_RTS_PIN, !((req->wValue >> 1) & 1));
				#endif
				return USBD_REQ_HANDLED;
			case GDB_IF_NO:
				cdcacm_gdb_dtr = req->wValue & 1;
				return USBD_REQ_HANDLED;
			default:
				return USBD_REQ_NOTSUPP;
		}
	case USB_CDC_REQ_SET_LINE_CODING:
		if(*len < sizeof(struct usb_cdc_line_coding))
			return USBD_REQ_NOTSUPP;

		switch(req->wIndex) {
		case UART_IF_NO:
			usbuart_set_line_coding((struct usb_cdc_line_coding*)*buf);
			return USBD_REQ_HANDLED;
		case GDB_IF_NO:
			return USBD_REQ_HANDLED; /* Ignore on GDB Port */
		default:
			return USBD_REQ_NOTSUPP;
		}
	case DFU_GETSTATUS:
		if(req->wIndex == DFU_IF_NO) {
			(*buf)[0] = DFU_STATUS_OK;
			(*buf)[1] = 0;
			(*buf)[2] = 0;
			(*buf)[3] = 0;
			(*buf)[4] = STATE_APP_IDLE;
			(*buf)[5] = 0;	/* iString not used here */
			*len = 6;

			return USBD_REQ_HANDLED;
		}
		return USBD_REQ_NOTSUPP;
	case DFU_DETACH:
		if(req->wIndex == DFU_IF_NO) {
			*complete = dfu_detach_complete;
			return USBD_REQ_HANDLED;
		}
		return USBD_REQ_NOTSUPP;
	}
	return USBD_REQ_NOTSUPP;
}

int cdcacm_get_config(void)
{
	return configured;
}

int cdcacm_get_dtr(void)
{
	return cdcacm_gdb_dtr;
}

static void cdcacm_set_modem_state(usbd_device *dev, int iface, bool dsr, bool dcd)
{
	char buf[10];
	struct usb_cdc_notification *notif = (void*)buf;
	/* We echo signals back to host as notification */
	notif->bmRequestType = 0xA1;
	notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
	notif->wValue = 0;
	notif->wIndex = iface;
	notif->wLength = 2;
	buf[8] = (dsr ? 2 : 0) | (dcd ? 1 : 0);
	buf[9] = 0;
	/* FIXME: Remove magic numbers */
	usbd_ep_write_packet(dev, 0x82 + iface, buf, 10);
}

static void cdcacm_set_config(usbd_device *dev, uint16_t wValue)
{
	configured = wValue;

	/* GDB interface */
#if defined(STM32F4) || defined(LM4F)
	usbd_ep_setup(dev, CDCACM_GDB_ENDPOINT, USB_ENDPOINT_ATTR_BULK,
	              CDCACM_PACKET_SIZE, gdb_usb_out_cb);
#else
	usbd_ep_setup(dev, CDCACM_GDB_ENDPOINT, USB_ENDPOINT_ATTR_BULK,
	              CDCACM_PACKET_SIZE, NULL);
#endif
	usbd_ep_setup(dev, CDCACM_GDB_ENDPOINT | USB_REQ_TYPE_IN,
				  USB_ENDPOINT_ATTR_BULK, CDCACM_PACKET_SIZE, NULL);
	usbd_ep_setup(dev, (CDCACM_GDB_ENDPOINT + 1) | USB_REQ_TYPE_IN,
				  USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

	/* Serial interface */
	usbd_ep_setup(dev, CDCACM_UART_ENDPOINT, USB_ENDPOINT_ATTR_BULK,
	              CDCACM_PACKET_SIZE / 2, usbuart_usb_out_cb);
	usbd_ep_setup(dev, CDCACM_UART_ENDPOINT | USB_REQ_TYPE_IN,
				  USB_ENDPOINT_ATTR_BULK,
	              CDCACM_PACKET_SIZE, usbuart_usb_in_cb);
	usbd_ep_setup(dev, (CDCACM_UART_ENDPOINT + 1) | USB_REQ_TYPE_IN,
				  USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

#if defined(PLATFORM_HAS_TRACESWO)
	/* Trace interface */
	usbd_ep_setup(dev, TRACE_ENDPOINT | USB_REQ_TYPE_IN, USB_ENDPOINT_ATTR_BULK,
					64, trace_buf_drain);
#endif

	usbd_register_control_callback(dev,
			USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
			USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
			cdcacm_control_request);

	/* Notify the host that DCD is asserted.
	 * Allows the use of /dev/tty* devices on *BSD/MacOS
	 */
	cdcacm_set_modem_state(dev, GDB_IF_NO, true, true);
	cdcacm_set_modem_state(dev, UART_IF_NO, true, true);
}

/* We need a special large control buffer for this device: */
static uint8_t usbd_control_buffer[256];

void blackmagic_usb_init(void)
{
	serial_no_read(serial_no);

	usbdev = usbd_init(&USB_DRIVER, &dev_desc, &config, usb_strings, sizeof(usb_strings) / sizeof(char *),
		usbd_control_buffer, sizeof(usbd_control_buffer));

	usbd_register_set_config_callback(usbdev, cdcacm_set_config);

	nvic_set_priority(USB_IRQ, IRQ_PRI_USB);
	nvic_enable_irq(USB_IRQ);
}
