/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Copyright (C) 2014 Fredrik Ahlberg <fredrik@z80.se>
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

/* This file implements capture of the TRACESWO output.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

#include "general.h"
#include "cdcacm.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/nvic.h>
#include <libopencm3/lm4f/uart.h>
#include <libopencm3/usb/usbd.h>

void traceswo_init(void)
{
	periph_clock_enable(RCC_GPIOD);
	periph_clock_enable(TRACEUART_CLK);
	__asm__("nop"); __asm__("nop"); __asm__("nop");

	gpio_mode_setup(SWO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWO_PIN);
	gpio_set_af(SWO_PORT, 1, SWO_PIN); /* U2RX */

	uart_disable(TRACEUART);

	/* Setup UART parameters. */
	uart_clock_from_sysclk(TRACEUART);
	uart_set_baudrate(TRACEUART, 800000);
	uart_set_databits(TRACEUART, 8);
	uart_set_stopbits(TRACEUART, 1);
	uart_set_parity(TRACEUART, UART_PARITY_NONE);

	// Enable FIFO
	uart_enable_fifo(TRACEUART);

	// Set FIFO interrupt trigger levels to 4/8 full for RX buffer and
	// 7/8 empty (1/8 full) for TX buffer
	uart_set_fifo_trigger_levels(TRACEUART, UART_FIFO_RX_TRIG_1_2, UART_FIFO_TX_TRIG_7_8);

	uart_clear_interrupt_flag(TRACEUART, UART_INT_RX | UART_INT_RT);

	/* Enable interrupts */
	uart_enable_interrupts(TRACEUART, UART_INT_RX | UART_INT_RT);

	/* Finally enable the USART. */
	uart_enable(TRACEUART);

	nvic_set_priority(TRACEUART_IRQ, 0);
	nvic_enable_irq(TRACEUART_IRQ);

	/* Un-stall USB endpoint */
	usbd_ep_stall_set(usbdev, 0x85, 0);

	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO3);
}

void traceswo_baud(unsigned int baud)
{
	uart_set_baudrate(TRACEUART, baud);
	uart_set_databits(TRACEUART, 8);
}

#define FIFO_SIZE 256

/* RX Fifo buffer */
static volatile uint8_t buf_rx[FIFO_SIZE];
/* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
static volatile uint32_t buf_rx_in = 0;
/* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
static volatile uint32_t buf_rx_out = 0;

void trace_buf_push(void)
{
	size_t len;

	if (buf_rx_in == buf_rx_out) {
		return;
	} else if (buf_rx_in > buf_rx_out) {
		len = buf_rx_in - buf_rx_out;
	} else {
		len = FIFO_SIZE - buf_rx_out;
	}

	if (len > 64) {
		len = 64;
	}

	if (usbd_ep_write_packet(usbdev, 0x85, (uint8_t *)&buf_rx[buf_rx_out], len) == len) {
		buf_rx_out += len;
		buf_rx_out %= FIFO_SIZE;
	}
}

void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	(void) dev;
	(void) ep;
	trace_buf_push();
}

void trace_tick(void)
{
	trace_buf_push();
}

void TRACEUART_ISR(void)
{
	uint32_t flush = uart_is_interrupt_source(TRACEUART, UART_INT_RT);

	while (!uart_is_rx_fifo_empty(TRACEUART)) {
		uint32_t c = uart_recv(TRACEUART);

		/* If the next increment of rx_in would put it at the same point
		* as rx_out, the FIFO is considered full.
		*/
		if (((buf_rx_in + 1) % FIFO_SIZE) != buf_rx_out)
		{
			/* insert into FIFO */
			buf_rx[buf_rx_in++] = c;

			/* wrap out pointer */
			if (buf_rx_in >= FIFO_SIZE)
			{
				buf_rx_in = 0;
			}
		} else {
			flush = 1;
			break;
		}
	}

	if (flush) {
		/* advance fifo out pointer by amount written */
		trace_buf_push();
	}
}

