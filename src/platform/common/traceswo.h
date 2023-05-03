/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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

#ifndef PLATFORMS_COMMON_TRACESWO_H
#define PLATFORMS_COMMON_TRACESWO_H

#include <libopencm3/usb/usbd.h>

#if defined TRACESWO_PROTOCOL && TRACESWO_PROTOCOL == 2
/* Default line rate, used as default for a request without baudrate */
#define SWO_DEFAULT_BAUD 2250000U
void traceswo_init(uint32_t baudrate, uint32_t swo_chan_bitmask);
#else
void traceswo_init(uint32_t swo_chan_bitmask);
#endif

void trace_buf_drain(usbd_device *dev, uint8_t ep);

/* Set bitmask of SWO channels to be decoded */
void traceswo_setmask(uint32_t mask);

/* Print decoded SWO packet on USB serial */
uint16_t traceswo_decode(usbd_device *usbd_dev, uint8_t addr, const void *buf, uint16_t len);

#endif /* PLATFORMS_COMMON_TRACESWO_H */
