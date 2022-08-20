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

void usbuart_set_led_state(uint8_t ledn, bool state);
void debug_uart_run(void);

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
#define RX_FIFO_SIZE (USART_DMA_BUF_SIZE)
#define TX_BUF_SIZE (USART_DMA_BUF_SIZE)

/* RX Fifo buffer with space for copy fn overrun */
extern char buf_rx[RX_FIFO_SIZE + sizeof(uint64_t)];
/* RX Fifo out pointer, writes assumed to be atomic */
extern uint8_t buf_rx_out;
/* RX usb transfer complete */
extern bool rx_usb_trfr_cplt;

#ifdef ENABLE_DEBUG
/* Debug Fifo buffer with space for copy fn overrun */
extern char usb_dbg_buf[RX_FIFO_SIZE + sizeof(uint64_t)];
/* Debug Fifo in pointer */
extern uint8_t usb_dbg_in;
/* Debug Fifo out pointer */
extern uint8_t usb_dbg_out;
#endif
#elif defined(LM4F)
#define FIFO_SIZE 128

/* RX Fifo buffer */
extern char buf_rx[FIFO_SIZE];
/* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
extern uint8_t buf_rx_in;
/* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
extern uint8_t buf_rx_out;
#endif

#endif
