/** @defgroup usb_control_file Generic USB Control Requests

@ingroup USB

@brief <b>Generic USB Control Requests</b>

@version 1.0.0

@author @htmlonly &copy; @endhtmlonly 2010
Gareth McMullin <gareth@blacksphere.co.nz>

@date 10 March 2013

LGPL License Terms @ref lgpl_license
*/

/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 * Portions (C) 2020-2021 Stoyan Shopov <stoyan.shopov@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**@{*/

#include <stdlib.h>
#include <libopencm3/usb/usbd.h>
#include "usb_private.h"

/*
 * According to the USB 2.0 specification, section 8.5.3, when a control
 * transfer is stalled, the pipe becomes idle. We provide one utility to stall
 * a transaction to reduce boilerplate code.
 */
static void stall_transaction(usbd_device *usbd_dev)
{
	usbd_ep_stall_set(usbd_dev, 0, 1);
	usbd_dev->control_state.state = IDLE;
}

/**
 * If we're replying with _some_ data, but less than the host is expecting,
 * then we normally just do a short transfer.  But if it's short, but a
 * multiple of the endpoint max packet size, we need an explicit ZLP.
 * @param len how much data we want to transfer
 * @param wLength how much the host asked for
 * @param ep_size
 * @return
 */
static bool needs_zlp(uint16_t len, uint16_t wLength, uint8_t ep_size)
{
	if (len < wLength) {
		if (len && (len % ep_size == 0)) {
			return true;
		}
	}
	return false;
}

/* Register application callback function for handling USB control requests. */
int usbd_register_control_callback(usbd_device *usbd_dev, uint8_t type,
				   uint8_t type_mask,
				   usbd_control_callback callback)
{
	int i;

	for (i = 0; i < MAX_USER_CONTROL_CALLBACK; i++) {
		if (usbd_dev->user_control_callback[i].cb) {
			continue;
		}

		usbd_dev->user_control_callback[i].type = type;
		usbd_dev->user_control_callback[i].type_mask = type_mask;
		usbd_dev->user_control_callback[i].cb = callback;
		return 0;
	}

	return -1;
}

static void usb_control_send_chunk(usbd_device *usbd_dev)
{
	if (usbd_dev->desc->bMaxPacketSize0 <
			usbd_dev->control_state.ctrl_len) {
		/* Data stage, normal transmission */
		usbd_ep_write_packet(usbd_dev, 0,
				     usbd_dev->control_state.ctrl_buf,
				     usbd_dev->desc->bMaxPacketSize0);
		usbd_dev->control_state.state = DATA_IN;
		usbd_dev->control_state.ctrl_buf +=
			usbd_dev->desc->bMaxPacketSize0;
		usbd_dev->control_state.ctrl_len -=
			usbd_dev->desc->bMaxPacketSize0;
	} else {
		/* Data stage, end of transmission */
		usbd_ep_write_packet(usbd_dev, 0,
				     usbd_dev->control_state.ctrl_buf,
				     usbd_dev->control_state.ctrl_len);

		usbd_dev->control_state.state =
			usbd_dev->control_state.needs_zlp ?
			DATA_IN : LAST_DATA_IN;
		usbd_dev->control_state.needs_zlp = false;
		usbd_dev->control_state.ctrl_len = 0;
		usbd_dev->control_state.ctrl_buf = NULL;
	}
}

static int usb_control_recv_chunk(usbd_device *usbd_dev)
{
	uint16_t packetsize = MIN(usbd_dev->desc->bMaxPacketSize0,
			usbd_dev->control_state.req.wLength -
			usbd_dev->control_state.ctrl_len);
	uint16_t size = usbd_ep_read_packet(usbd_dev, 0,
				       usbd_dev->control_state.ctrl_buf +
				       usbd_dev->control_state.ctrl_len,
				       packetsize);

	if (size != packetsize) {
		stall_transaction(usbd_dev);
		return -1;
	}

	usbd_dev->control_state.ctrl_len += size;

	return packetsize;
}

static enum usbd_request_return_codes
usb_control_request_dispatch(usbd_device *usbd_dev,
			     struct usb_setup_data *req)
{
	int i, result = 0;
	struct user_control_callback *cb = usbd_dev->user_control_callback;

	/* Call user command hook function. */
	for (i = 0; i < MAX_USER_CONTROL_CALLBACK; i++) {
		if (cb[i].cb == NULL) {
			break;
		}

		if ((req->bmRequestType & cb[i].type_mask) == cb[i].type) {
			result = cb[i].cb(usbd_dev, req,
					  &(usbd_dev->control_state.ctrl_buf),
					  &(usbd_dev->control_state.ctrl_len),
					  &(usbd_dev->control_state.complete));
			if (result == USBD_REQ_HANDLED ||
			    result == USBD_REQ_NOTSUPP) {
				return result;
			}
		}
	}

	/* Try standard request if not already handled. */
	return _usbd_standard_request(usbd_dev, req,
				      &(usbd_dev->control_state.ctrl_buf),
				      &(usbd_dev->control_state.ctrl_len));
}

/* Handle commands and read requests. */
static void usb_control_setup_read(usbd_device *usbd_dev,
		struct usb_setup_data *req)
{
	usbd_dev->control_state.ctrl_buf = usbd_dev->ctrl_buf;
	usbd_dev->control_state.ctrl_len = req->wLength;

	if (usb_control_request_dispatch(usbd_dev, req)) {
		if (req->wLength) {
			usbd_dev->control_state.needs_zlp =
				needs_zlp(usbd_dev->control_state.ctrl_len,
					req->wLength,
					usbd_dev->desc->bMaxPacketSize0);
			/* Go to data out stage if handled. */
			usb_control_send_chunk(usbd_dev);
		} else {
			/* Go to status stage if handled. */
			usbd_ep_write_packet(usbd_dev, 0, NULL, 0);
			usbd_dev->control_state.state = STATUS_IN;
		}
	} else {
		/* Stall endpoint on failure. */
		stall_transaction(usbd_dev);
	}
}

static void usb_control_setup_write(usbd_device *usbd_dev,
				    struct usb_setup_data *req)
{
	if (req->wLength > usbd_dev->ctrl_buf_len) {
		stall_transaction(usbd_dev);
		return;
	}

	/* Buffer into which to write received data. */
	usbd_dev->control_state.ctrl_buf = usbd_dev->ctrl_buf;
	usbd_dev->control_state.ctrl_len = 0;
	/* Wait for DATA OUT stage. */
	if (req->wLength > usbd_dev->desc->bMaxPacketSize0) {
		usbd_dev->control_state.state = DATA_OUT;
	} else {
		usbd_dev->control_state.state = LAST_DATA_OUT;
	}

	usbd_ep_nak_set(usbd_dev, 0, 0);
}

/* Do not appear to belong to the API, so are omitted from docs */
/**@}*/

void _usbd_control_setup(usbd_device *usbd_dev, uint8_t ea)
{
	struct usb_setup_data *req = &usbd_dev->control_state.req;
	(void)ea;

	usbd_dev->control_state.complete = NULL;

	usbd_ep_nak_set(usbd_dev, 0, 1);

	/* Note: this is the only difference from the original
	 * libopencm3 source code, the setup packet is read here,
	 * and in the original sources it is expected that the
	 * packet has already been read into the
	 * usbd_dev->control_state.req buffer. */
	if (usbd_ep_read_packet(usbd_dev, 0, req, 8) != 8) {
		stall_transaction(usbd_dev);
		return;
	}
	if (req->wLength == 0) {
		usb_control_setup_read(usbd_dev, req);
	} else if (req->bmRequestType & 0x80) {
		usb_control_setup_read(usbd_dev, req);
	} else {
		usb_control_setup_write(usbd_dev, req);
	}
}

void _usbd_control_out(usbd_device *usbd_dev, uint8_t ea)
{
	(void)ea;

	switch (usbd_dev->control_state.state) {
	case DATA_OUT:
		if (usb_control_recv_chunk(usbd_dev) < 0) {
			break;
		}
		if ((usbd_dev->control_state.req.wLength -
					usbd_dev->control_state.ctrl_len) <=
					usbd_dev->desc->bMaxPacketSize0) {
			usbd_dev->control_state.state = LAST_DATA_OUT;
		}
		break;
	case LAST_DATA_OUT:
		if (usb_control_recv_chunk(usbd_dev) < 0) {
			break;
		}
		/*
		 * We have now received the full data payload.
		 * Invoke callback to process.
		 */
		if (usb_control_request_dispatch(usbd_dev,
					&(usbd_dev->control_state.req))) {
			/* Go to status stage on success. */
			usbd_ep_write_packet(usbd_dev, 0, NULL, 0);
			usbd_dev->control_state.state = STATUS_IN;
		} else {
			stall_transaction(usbd_dev);
		}
		break;
	case STATUS_OUT:
		usbd_ep_read_packet(usbd_dev, 0, NULL, 0);
		usbd_dev->control_state.state = IDLE;
		if (usbd_dev->control_state.complete) {
			usbd_dev->control_state.complete(usbd_dev,
					&(usbd_dev->control_state.req));
		}
		usbd_dev->control_state.complete = NULL;
		break;
	default:
		stall_transaction(usbd_dev);
	}
}

void _usbd_control_in(usbd_device *usbd_dev, uint8_t ea)
{
	(void)ea;
	struct usb_setup_data *req = &(usbd_dev->control_state.req);

	switch (usbd_dev->control_state.state) {
	case DATA_IN:
		usb_control_send_chunk(usbd_dev);
		break;
	case LAST_DATA_IN:
		usbd_dev->control_state.state = STATUS_OUT;
		usbd_ep_nak_set(usbd_dev, 0, 0);
		break;
	case STATUS_IN:
		if (usbd_dev->control_state.complete) {
			usbd_dev->control_state.complete(usbd_dev,
					&(usbd_dev->control_state.req));
		}

		/* Exception: Handle SET ADDRESS function here... */
		if ((req->bmRequestType == 0) &&
		    (req->bRequest == USB_REQ_SET_ADDRESS)) {
			usbd_dev->driver->set_address(usbd_dev, req->wValue);
		}
		usbd_dev->control_state.state = IDLE;
		break;
	default:
		stall_transaction(usbd_dev);
	}
}
