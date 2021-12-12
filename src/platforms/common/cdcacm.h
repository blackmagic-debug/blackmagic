/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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
 */
#ifndef __CDCACM_H
#define __CDCACM_H

#include <libopencm3/usb/usbd.h>

#if defined(USB_HS)
# define CDCACM_PACKET_SIZE    512
#else
# define CDCACM_PACKET_SIZE     64
#endif

#if !defined(MAX_BINTERVAL)
# define MAX_BINTERVAL 255
#endif

#define TRACE_ENDPOINT_SIZE CDCACM_PACKET_SIZE

/* Use platform provided value if given. */
#if !defined(TRACE_ENDPOINT_SIZE)
# define TRACE_ENDPOINT_SIZE CDCACM_PACKET_SIZE
#endif

#define CDCACM_GDB_ENDPOINT	1
#define CDCACM_UART_ENDPOINT	3
#define TRACE_ENDPOINT			5
#define CDCACM_SLCAN_ENDPOINT	6

extern usbd_device *usbdev;

void cdcacm_init(void);
/* Returns current usb configuration, or 0 if not configured. */
int cdcacm_get_config(void);
int cdcacm_get_dtr(void);

#endif
