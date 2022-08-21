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

#include <stddef.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

void aux_serial_init(void);
void aux_serial_set_encoding(struct usb_cdc_line_coding *coding);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
void usbuart_set_led_state(uint8_t ledn, bool state);

void aux_serial_switch_transmit_buffers(void);
#endif

/* Get the current transmit buffer to stage data into */
char *aux_serial_current_transmit_buffer(void);
/* Get how full the current transmit buffer is */
size_t aux_serial_transmit_buffer_fullness(void);
/* Send a number of bytes staged into the current transmit bufer */
void aux_serial_send(size_t len);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
void aux_serial_update_receive_buffer_fullness(void);
bool aux_serial_receive_buffer_empty(void);
void aux_serial_drain_receive_buffer(void);
#ifdef ENABLE_DEBUG
void aux_serial_stage_debug_buffer(void);
#endif
void aux_serial_stage_receive_buffer(void);
#endif

#endif /*AUX_SERIAL_H*/
