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

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/cortex.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

#include "general.h"
#include "usbuart.h"
#include "usb.h"
#include "aux_serial.h"

#ifdef DMA_STREAM0
#define DMA_CGIF DMA_ISR_FLAGS
#else
#define DMA_CGIF DMA_IFCR_CGIF_BIT
#endif

/* RX Fifo buffer with space for copy fn overrun */
char buf_rx[RX_FIFO_SIZE + sizeof(uint64_t)];
/* RX Fifo out pointer, writes assumed to be atomic */
uint8_t buf_rx_out;

#ifdef ENABLE_DEBUG
/* Debug Fifo buffer with space for copy fn overrun */
char usb_dbg_buf[RX_FIFO_SIZE + sizeof(uint64_t)];
/* Debug Fifo in pointer */
uint8_t usb_dbg_in;
/* Debug Fifo out pointer */
uint8_t usb_dbg_out;
#endif

/*
 * Update led state atomically respecting RX anb TX states.
 */
void usbuart_set_led_state(uint8_t ledn, bool state)
{
	CM_ATOMIC_CONTEXT();

	static uint8_t led_state = 0;

	if (state)
	{
		led_state |= ledn;
		gpio_set(LED_PORT_UART, LED_UART);
	}
	else
	{
		led_state &= ~ledn;
		if (!led_state)
			gpio_clear(LED_PORT_UART, LED_UART);
	}
}
