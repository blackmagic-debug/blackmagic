/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

#ifndef PLATFORMS_COMMON_SWO_H
#define PLATFORMS_COMMON_SWO_H

#include <libopencm3/usb/usbd.h>

#if defined TRACESWO_PROTOCOL && TRACESWO_PROTOCOL == 2
/* Default line rate, used as default for a request without baudrate */
#define SWO_DEFAULT_BAUD 2250000U
void traceswo_init(uint32_t baudrate, uint32_t swo_chan_bitmask);
void traceswo_deinit(void);

uint32_t traceswo_get_baudrate(void);
void bmd_usart_set_baudrate(uint32_t usart, uint32_t baud_rate);
#else
void swo_manchester_init(uint32_t itm_stream_bitmask);
void swo_manchester_deinit(void);
#endif

void swo_send_buffer(usbd_device *dev, uint8_t ep);

/* Set a bitmask of SWO ITM streams to be decoded */
void swo_itm_decode_set_mask(uint32_t mask);

/* Decode a new block of ITM data from SWO */
uint16_t swo_itm_decode(usbd_device *usbd_dev, uint8_t ep, const uint8_t *data, uint16_t len);

#endif /* PLATFORMS_COMMON_SWO_H */
