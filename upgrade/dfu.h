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
#ifndef __DFU_H
#define __DFU_H

#include <stdint.h>

#include <usb.h>

/* DFU states as returned by DFU_GETSTATE and DFU_GETSTATUS request in bState field.
 * Refer to Section 6.1.2
 * Refer to Figure A.1 for state diagram 
 */
#define STATE_APP_IDLE                0x00
#define STATE_APP_DETACH              0x01
#define STATE_DFU_IDLE                0x02
#define STATE_DFU_DOWNLOAD_SYNC       0x03
#define STATE_DFU_DOWNLOAD_BUSY       0x04
#define STATE_DFU_DOWNLOAD_IDLE       0x05
#define STATE_DFU_MANIFEST_SYNC       0x06
#define STATE_DFU_MANIFEST            0x07
#define STATE_DFU_MANIFEST_WAIT_RESET 0x08
#define STATE_DFU_UPLOAD_IDLE         0x09
#define STATE_DFU_ERROR               0x0a

/* DFU status codes as returned by DFU_GETSTATUS request in bStatus field.
 * Refer to Section 6.1.2 */
#define DFU_STATUS_OK                 0x00
#define DFU_STATUS_ERROR_TARGET       0x01
#define DFU_STATUS_ERROR_FILE         0x02
#define DFU_STATUS_ERROR_WRITE        0x03
#define DFU_STATUS_ERROR_ERASE        0x04
#define DFU_STATUS_ERROR_CHECK_ERASED 0x05
#define DFU_STATUS_ERROR_PROG         0x06
#define DFU_STATUS_ERROR_VERIFY       0x07
#define DFU_STATUS_ERROR_ADDRESS      0x08
#define DFU_STATUS_ERROR_NOTDONE      0x09
#define DFU_STATUS_ERROR_FIRMWARE     0x0a
#define DFU_STATUS_ERROR_VENDOR       0x0b
#define DFU_STATUS_ERROR_USBR         0x0c
#define DFU_STATUS_ERROR_POR          0x0d
#define DFU_STATUS_ERROR_UNKNOWN      0x0e
#define DFU_STATUS_ERROR_STALLEDPKT   0x0f

/* Device status structure returned by DFU_GETSTATUS request.
 * Refer to Section 6.1.2 */
typedef struct dfu_status {
	uint8_t bStatus;
	uint32_t bwPollTimeout:24;
	uint8_t bState;
	uint8_t iString;
} __attribute__((packed)) dfu_status;


int dfu_detach(usb_dev_handle *dev, uint16_t iface, uint16_t wTimeout);
int dfu_dnload(usb_dev_handle *dev, uint16_t iface, 
		 uint16_t wBlockNum, void *data, uint16_t size);
int dfu_upload(usb_dev_handle *dev, uint16_t iface, 
		 uint16_t wBlockNum, void *data, uint16_t size);
int dfu_getstatus(usb_dev_handle *dev, uint16_t iface, dfu_status *status);
int dfu_clrstatus(usb_dev_handle *dev, uint16_t iface);
int dfu_getstate(usb_dev_handle *dev, uint16_t iface);
int dfu_abort(usb_dev_handle *dev, uint16_t iface);

int dfu_makeidle(usb_dev_handle *dev, uint16_t iface);


#endif

