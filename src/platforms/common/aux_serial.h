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
#ifndef AUX_SERIAL_H
#define AUX_SERIAL_H

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

void aux_serial_init(void);
void aux_serial_set_encoding(struct usb_cdc_line_coding *coding);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
void aux_serial_switch_transmit_buffers(void);
char *aux_serial_current_transmit_buffer(void);
#endif

#endif /*AUX_SERIAL_H*/
