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

#include <libopencm3/usb/usbd.h>

#include "general.h"

void usbuart_usb_out_cb(usbd_device *dev, uint8_t ep);
void usbuart_usb_in_cb(usbd_device *dev, uint8_t ep);

void usbuart_set_led_state(uint8_t ledn, bool state);
void debug_uart_send_rx_packet(void);
void debug_uart_run(void);

#define TX_LED_ACT (1 << 0)
#define RX_LED_ACT (1 << 1)

/* F072 with st_usbfs_v2_usb_drive drops characters at the 64 byte boundary!*/
#if !defined(USART_DMA_BUF_SIZE)
# define USART_DMA_BUF_SIZE 128
#endif
#define RX_FIFO_SIZE (USART_DMA_BUF_SIZE)
#define TX_BUF_SIZE (USART_DMA_BUF_SIZE)

/* TX transfer complete */
extern bool tx_trfr_cplt;
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

#endif
