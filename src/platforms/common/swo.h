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

#if !defined(NO_LIBOPENCM3)
#include <libopencm3/usb/usbd.h>
#endif

/* Default line rate, used as default for a request without baudrate */
#define SWO_DEFAULT_BAUD 2250000U

typedef enum swo_coding {
	swo_none,
	swo_manchester,
	swo_nrz_uart,
} swo_coding_e;

extern swo_coding_e swo_current_mode;

/* Initialisation and deinitialisation functions (ties into command.c) */
void swo_init(swo_coding_e swo_mode, uint32_t baudrate, uint32_t itm_stream_bitmask);
void swo_deinit(bool deallocate);

#if !defined(NO_LIBOPENCM3)

/* UART mode baudate functions */
uint32_t swo_uart_get_baudrate(void);
void bmd_usart_set_baudrate(uint32_t usart, uint32_t baud_rate);

/* USB callback for the raw data endpoint to ask for a new buffer of data */
void swo_send_buffer(usbd_device *dev, uint8_t ep);

/* Set a bitmask of SWO ITM streams to be decoded */
void swo_itm_decode_set_mask(uint32_t mask);

/* Decode a new block of ITM data from SWO */
uint16_t swo_itm_decode(const uint8_t *data, uint16_t len);

#endif /* !NO_LIBOPENCM3 */

#endif /* PLATFORMS_COMMON_SWO_H */
