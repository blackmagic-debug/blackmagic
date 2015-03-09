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

#ifdef WIN32
#   include <lusb0_usb.h>
#else
#   include <usb.h>
#endif

#include "dfu.h"

/* DFU Requests: Refer to Table 3.2 */
#define DFU_DETACH	0x00
#define DFU_DNLOAD	0x01
#define DFU_UPLOAD	0x02
#define DFU_GETSTATUS	0x03
#define DFU_CLRSTATUS	0x04
#define DFU_GETSTATE	0x05
#define DFU_ABORT	0x06

#define USB_DEFAULT_TIMEOUT 1000
#define DFU_DETACH_TIMEOUT 1000

int dfu_detach(usb_dev_handle *dev, uint16_t iface, uint16_t wTimeout)
{
	return usb_control_msg(dev, 
			USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_DETACH, wTimeout, iface, NULL, 0, 
			USB_DEFAULT_TIMEOUT);
}

int dfu_dnload(usb_dev_handle *dev, uint16_t iface, 
		 uint16_t wBlockNum, void *data, uint16_t size)
{
	return usb_control_msg(dev, 
			USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_DNLOAD, wBlockNum, iface, data, size, 
			USB_DEFAULT_TIMEOUT);
}

int dfu_upload(usb_dev_handle *dev, uint16_t iface, 
		 uint16_t wBlockNum, void *data, uint16_t size)
{
	return usb_control_msg(dev, 
			USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_DNLOAD, wBlockNum, iface, data, size, 
			USB_DEFAULT_TIMEOUT);
}

int dfu_getstatus(usb_dev_handle *dev, uint16_t iface, dfu_status *status)
{
	return usb_control_msg(dev, 
			USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_GETSTATUS, 0, iface, (void*)status, sizeof(dfu_status), 
			USB_DEFAULT_TIMEOUT);
}

int dfu_clrstatus(usb_dev_handle *dev, uint16_t iface)
{
	return usb_control_msg(dev, 
			USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_CLRSTATUS, 0, iface, NULL, 0, USB_DEFAULT_TIMEOUT);
}

int dfu_getstate(usb_dev_handle *dev, uint16_t iface)
{
	int i;
	uint8_t state;
	do {
		i = usb_control_msg(dev,
			USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_GETSTATE, 0, iface, (char*)&state, 1,
			USB_DEFAULT_TIMEOUT);
	} while(i == 0);

	if (i > 0)
		return state;
	else
		return i;
}

int dfu_abort(usb_dev_handle *dev, uint16_t iface)
{
	return usb_control_msg(dev, 
			USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			DFU_ABORT, 0, iface, NULL, 0, USB_DEFAULT_TIMEOUT);
}


int dfu_makeidle(usb_dev_handle *dev, uint16_t iface)
{
	int i;
	dfu_status status;

	for(i = 0; i < 3; i++) {
		if(dfu_getstatus(dev, iface, &status) < 0) {
			dfu_clrstatus(dev, iface);
			continue;
		}

		i--;
		
		switch(status.bState) {
		    case STATE_DFU_IDLE:
			return 0;

		    case STATE_DFU_DOWNLOAD_SYNC:
		    case STATE_DFU_DOWNLOAD_IDLE:
		    case STATE_DFU_MANIFEST_SYNC:
		    case STATE_DFU_UPLOAD_IDLE:
		    case STATE_DFU_DOWNLOAD_BUSY:
		    case STATE_DFU_MANIFEST:
			dfu_abort(dev, iface);
			continue;

		    case STATE_DFU_ERROR:
			dfu_clrstatus(dev, iface);
			continue;

		    case STATE_APP_IDLE:
			dfu_detach(dev, iface, DFU_DETACH_TIMEOUT);
			continue;

		    case STATE_APP_DETACH:
		    case STATE_DFU_MANIFEST_WAIT_RESET:
			usb_reset(dev);
			return -1;

		    default:
			return -1;
		}

	}

	return -1;
}


