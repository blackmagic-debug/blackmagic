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

#ifndef PLATFORMS_COMMON_USB_SERIAL_H
#define PLATFORMS_COMMON_USB_SERIAL_H

#include <stdint.h>
#include <stdbool.h>
#include "usb.h"

void usb_serial_set_config(usbd_device *dev, uint16_t value);

bool gdb_serial_get_dtr(void);

void debug_serial_run(void);
uint32_t debug_serial_fifo_send(const char *fifo, uint32_t fifo_begin, uint32_t fifo_end);

#ifdef ENABLE_RTT
void debug_serial_receive_callback(usbd_device *dev, uint8_t ep);
#endif

#endif /* PLATFORMS_COMMON_USB_SERIAL_H */
