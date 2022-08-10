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

#include <libopencm3/usb/cdc.h>

static int configured;

static bool gdb_uart_dtr = true;

static void cdcacm_set_modem_state(usbd_device *dev, uint16_t iface, uint8_t ep);

static enum usbd_request_return_codes gdb_uart_control_request(usbd_device *dev, struct usb_setup_data *req,
	uint8_t **buf, uint16_t *const len, void (**complete)(usbd_device *dev, struct usb_setup_data *req))
{
	(void)buf;
	(void)complete;
	/* Is the request for the GDB UART interface? */
	if (req->wIndex != GDB_IF_NO)
		return USBD_REQ_NEXT_CALLBACK;

	switch (req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		cdcacm_set_modem_state(dev, req->wIndex, CDCACM_GDB_ENDPOINT);
		gdb_uart_dtr = req->wValue & 1;
		return USBD_REQ_HANDLED;
	case USB_CDC_REQ_SET_LINE_CODING:
		if (*len < sizeof(struct usb_cdc_line_coding))
			return USBD_REQ_NOTSUPP;
		return USBD_REQ_HANDLED; /* Ignore on GDB Port */
	}
	return USBD_REQ_NOTSUPP;
}

static enum usbd_request_return_codes debug_uart_control_request(usbd_device *dev, struct usb_setup_data *req,
	uint8_t **buf, uint16_t *const len, void (**complete)(usbd_device *dev, struct usb_setup_data *req))
{
	(void)complete;
	/* Is the request for the physical/debug UART interface? */
	if (req->wIndex != UART_IF_NO)
		return USBD_REQ_NEXT_CALLBACK;

	switch (req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		cdcacm_set_modem_state(dev, req->wIndex, CDCACM_UART_ENDPOINT);
#ifdef USBUSART_DTR_PIN
		gpio_set_val(USBUSART_PORT, USBUSART_DTR_PIN, !(req->wValue & 1));
#endif
#ifdef USBUSART_RTS_PIN
		gpio_set_val(USBUSART_PORT, USBUSART_RTS_PIN, !((req->wValue >> 1) & 1));
#endif
		return USBD_REQ_HANDLED;
	case USB_CDC_REQ_SET_LINE_CODING:
		if (*len < sizeof(struct usb_cdc_line_coding))
			return USBD_REQ_NOTSUPP;
		usbuart_set_line_coding((struct usb_cdc_line_coding *)*buf);
		return USBD_REQ_HANDLED;
	}
	return USBD_REQ_NOTSUPP;
}

int cdcacm_get_config(void)
{
	return configured;
}

bool gdb_uart_get_dtr(void)
{
	return gdb_uart_dtr;
}

static void cdcacm_set_modem_state(usbd_device *dev, const uint16_t iface, const uint8_t ep)
{
	uint8_t buf[10];
	struct usb_cdc_notification *notif = (void*)buf;
	/* We echo signals back to host as notification */
	notif->bmRequestType = 0xA1;
	notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
	notif->wValue = 0;
	notif->wIndex = iface;
	notif->wLength = 2;
	buf[8] = 3U;
	buf[9] = 0U;
	/* FIXME: Remove magic numbers */
	usbd_ep_write_packet(dev, ep, buf, sizeof(buf));
}

void cdcacm_set_config(usbd_device *dev, uint16_t wValue)
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

	usbd_register_control_callback(dev, USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, debug_uart_control_request);
	usbd_register_control_callback(dev, USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, gdb_uart_control_request);

	/* Notify the host that DCD is asserted.
	 * Allows the use of /dev/tty* devices on *BSD/MacOS
	 */
	cdcacm_set_modem_state(dev, GDB_IF_NO, CDCACM_GDB_ENDPOINT);
	cdcacm_set_modem_state(dev, UART_IF_NO, CDCACM_UART_ENDPOINT);
}
