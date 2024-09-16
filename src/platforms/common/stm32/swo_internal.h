/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
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

#ifndef PLATFORMS_COMMON_STM32_SWO_INTERNAL_H
#define PLATFORMS_COMMON_STM32_SWO_INTERNAL_H

#include "usb.h"

/*
 * Total buffer size for the dynamic buffer
 * NB: This *must* result in a value that is a power of two.
 */
#define SWO_BUFFER_SIZE (NUM_SWO_USB_PACKETS * SWO_ENDPOINT_SIZE)

/* Control variables shared between decoders */
extern bool swo_itm_decoding;

/* Dynamically-allocated data buffer, current read index, current write index, and current fill level */
extern uint8_t *swo_buffer;
extern uint16_t swo_buffer_read_index;
extern uint16_t swo_buffer_write_index;
extern _Atomic uint16_t swo_buffer_bytes_available;

/* Manchester-mode implementation functions */
void swo_manchester_init(void);
void swo_manchester_deinit(void);

/* UART-mode implementation functions */
void swo_uart_init(uint32_t baudrate);
void swo_uart_deinit(void);

#endif /* PLATFORMS_COMMON_STM32_SWO_INTERNAL_H */
