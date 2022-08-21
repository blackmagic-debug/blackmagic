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
#ifndef __USBUART_H
#define __USBUART_H

#include "general.h"

void debug_uart_run(void);
uint32_t debug_serial_fifo_send(const char *const fifo, const uint32_t fifo_begin, const uint32_t fifo_end);

#define TX_LED_ACT (1 << 0)
#define RX_LED_ACT (1 << 1)

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
/* XXX: Does the st_usbfs_v2_usb_driver work on F3 with 128 byte buffers? */
#if defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
#define USART_DMA_BUF_SHIFT 7U
#elif defined(STM32F0)
/* The st_usbfs_v2_usb_driver only works with up to 64-byte buffers on the F0 parts */
#define USART_DMA_BUF_SHIFT 6U
#endif

#define USART_DMA_BUF_SIZE  (1U << USART_DMA_BUF_SHIFT)
#define AUX_UART_BUFFER_SIZE (USART_DMA_BUF_SIZE)
#elif defined(LM4F)
#define AUX_UART_BUFFER_SIZE 128
#endif

#endif
