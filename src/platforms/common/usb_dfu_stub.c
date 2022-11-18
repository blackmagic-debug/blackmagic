/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>
#include <libopencm3/cm3/scb.h>

#include "general.h"
#include "usb_dfu_stub.h"
#include "usb_types.h"

static void dfu_detach_complete(usbd_device *const dev, usb_setup_data_s *const req)
{
	(void)dev;
	(void)req;
	platform_request_boot();

	/* Reset core to enter bootloader */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
	scb_reset_core();
#endif
}

static usbd_request_return_codes_e dfu_control_request(usbd_device *const dev, usb_setup_data_s *req, uint8_t **buf,
	uint16_t *len, void (**complete)(usbd_device *dev, usb_setup_data_s *req))
{
	(void)dev;
	/* Is the request for the DFU interface? */
	if (req->wIndex != DFU_IF_NO)
		return USBD_REQ_NEXT_CALLBACK;

	switch (req->bRequest) {
	case DFU_GETSTATUS:
		(*buf)[0] = DFU_STATUS_OK;
		(*buf)[1] = 0;
		(*buf)[2] = 0;
		(*buf)[3] = 0;
		(*buf)[4] = STATE_APP_IDLE;
		(*buf)[5] = 0; /* iString not used here */
		*len = 6;

		return USBD_REQ_HANDLED;
	case DFU_DETACH:
		*complete = dfu_detach_complete;
		return USBD_REQ_HANDLED;
	}
	/* If the request isn't one of the two above, we don't care as this is a DFU stub. */
	return USBD_REQ_NOTSUPP;
}

void dfu_set_config(usbd_device *const dev, const uint16_t value)
{
	(void)value;
	usbd_register_control_callback(dev, USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, dfu_control_request);
}
