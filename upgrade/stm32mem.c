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
#include <string.h>

#ifdef WIN32
#   include <windows.h>
#   include <lusb0_usb.h>
#else
#   include <unistd.h>
#   include <usb.h>
#endif

#include "dfu.h"
#include "stm32mem.h"

#define STM32_CMD_GETCOMMANDS		0x00
#define STM32_CMD_SETADDRESSPOINTER	0x21
#define STM32_CMD_ERASE			0x41

static int stm32_download(usb_dev_handle *dev, uint16_t iface, 
			  uint16_t wBlockNum, void *data, int size)
{
	dfu_status status;
	int i;

	if((i = dfu_dnload(dev, iface, wBlockNum, data, size)) < 0) return i;
	while(1) {
		if((i = dfu_getstatus(dev, iface, &status)) < 0) return i;
		switch(status.bState) {
		case STATE_DFU_DOWNLOAD_BUSY:
#ifdef WIN32
			Sleep(status.bwPollTimeout);
#else
			usleep(status.bwPollTimeout * 1000);
#endif
			break;
		case STATE_DFU_DOWNLOAD_IDLE:
			return 0;
		default:	
			return -1;
		}
	}
}

int stm32_mem_erase(usb_dev_handle *dev, uint16_t iface, uint32_t addr)
{
	uint8_t request[5];

	request[0] = STM32_CMD_ERASE;
	memcpy(request+1, &addr, sizeof(addr));

	return stm32_download(dev, iface, 0, request, sizeof(request));
}

int stm32_mem_write(usb_dev_handle *dev, uint16_t iface, void *data, int size)
{
	return stm32_download(dev, iface, 2, data, size);
}

int stm32_mem_manifest(usb_dev_handle *dev, uint16_t iface)
{
	dfu_status status;
	int i;

	if((i = dfu_dnload(dev, iface, 0, NULL, 0)) < 0) return i;
	while(1) {
		if((i = dfu_getstatus(dev, iface, &status)) < 0) return 0;
#ifdef WIN32
		Sleep(status.bwPollTimeout);
#else
		usleep(status.bwPollTimeout * 1000);
#endif
		switch(status.bState) {
		case STATE_DFU_MANIFEST:
			return 0;
		default:	
			return -1;
		}
	}
}

