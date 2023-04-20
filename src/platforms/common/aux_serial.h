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
#ifndef PLATFORMS_COMMON_AUX_SERIAL_H
#define PLATFORMS_COMMON_AUX_SERIAL_H

#include <stddef.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include "usb_types.h"

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
/* XXX: Does the st_usbfs_v2_usb_driver work on F3 with 128 byte buffers? */
#if defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
#define USART_DMA_BUF_SHIFT 7U
#elif defined(STM32F0)
/* The st_usbfs_v2_usb_driver only works with up to 64-byte buffers on the F0 parts */
#define USART_DMA_BUF_SHIFT 6U
#endif

#define USART_DMA_BUF_SIZE   (1U << USART_DMA_BUF_SHIFT)
#define AUX_UART_BUFFER_SIZE (USART_DMA_BUF_SIZE)
#elif defined(LM4F)
#define AUX_UART_BUFFER_SIZE 128U
#endif

typedef struct usb_cdc_line_coding usb_cdc_line_coding_s;

void aux_serial_init(void);
void aux_serial_set_encoding(const usb_cdc_line_coding_s *coding);
void aux_serial_get_encoding(usb_cdc_line_coding_s *coding);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
typedef enum aux_serial_led {
	AUX_SERIAL_LED_TX = (1U << 0U),
	AUX_SERIAL_LED_RX = (1U << 1U)
} aux_serial_led_e;

void aux_serial_set_led(aux_serial_led_e led);
void aux_serial_clear_led(aux_serial_led_e led);

void aux_serial_switch_transmit_buffers(void);
#endif

/* Get the current transmit buffer to stage data into */
char *aux_serial_current_transmit_buffer(void);
/* Get how full the current transmit buffer is */
size_t aux_serial_transmit_buffer_fullness(void);
/* Send a number of bytes staged into the current transmit buffer */
void aux_serial_send(size_t len);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
void aux_serial_update_receive_buffer_fullness(void);
bool aux_serial_receive_buffer_empty(void);
void aux_serial_drain_receive_buffer(void);
#ifdef ENABLE_DEBUG
void aux_serial_stage_debug_buffer(void);
#endif
void aux_serial_stage_receive_buffer(void);
#endif

#endif /* PLATFORMS_COMMON_AUX_SERIAL_H */
