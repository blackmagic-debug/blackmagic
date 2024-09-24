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

/*
 * This file implements capture of the Trace/SWO output using async signalling.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "swo.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/nvic.h>
#include <libopencm3/lm4f/uart.h>

void swo_init(const swo_coding_e swo_mode, const uint32_t baudrate, const uint32_t itm_stream_bitmask)
{
	/* Neither mode switching nor ITM decoding is implemented on this platform (yet) */
	(void)swo_mode;
	(void)itm_stream_bitmask;

	/* Ensure required peripherals are spun up */
	/* TODO: Move this into platform_init()! */
	periph_clock_enable(RCC_GPIOD);
	periph_clock_enable(SWO_UART_CLK);
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");

	/* Reconfigure the GPIO over to UART mode */
	gpio_mode_setup(SWO_UART_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWO_UART_RX_PIN);
	gpio_set_af(SWO_UART_PORT, SWO_UART_PIN_AF, SWO_UART_RX_PIN);

	/* Set up the UART for 8N1 at the requested baud rate */
	uart_clock_from_sysclk(SWO_UART);
	uart_set_baudrate(SWO_UART, baudrate);
	uart_set_databits(SWO_UART, 8U);
	uart_set_stopbits(SWO_UART, 1U);
	uart_set_parity(SWO_UART, UART_PARITY_NONE);

	/* Make use of the hardware FIFO for some additional buffering (up to 8 bytes) */
	uart_enable_fifo(SWO_UART);

	/* Configure the FIFO interrupts for ½ full (RX) and ⅞ empty (TX) */
	uart_set_fifo_trigger_levels(SWO_UART, UART_FIFO_RX_TRIG_1_2, UART_FIFO_TX_TRIG_7_8);

	/* Clear and enable the RX and RX timeout interrupts */
	uart_clear_interrupt_flag(SWO_UART, UART_INT_RX | UART_INT_RT);
	uart_enable_interrupts(SWO_UART, UART_INT_RX | UART_INT_RT);

	/* Actually enable the interrupts */
	nvic_set_priority(SWO_UART_IRQ, IRQ_PRI_SWO_UART);
	nvic_enable_irq(SWO_UART_IRQ);

	/* Un-stall USB endpoint */
	usbd_ep_stall_set(usbdev, USB_REQ_TYPE_IN | SWO_ENDPOINT, 0U);

	/* Finally enable the USART. */
	uart_enable(SWO_UART);

	/* XXX: What is this even reconfiguring?! */
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO3);
}

void swo_deinit(const bool deallocate)
{
	(void)deallocate;
	/* Disable the UART */
	uart_disable(SWO_UART);
	/* Put the GPIO back into normal service as a GPIO */
	gpio_mode_setup(SWO_UART_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWO_UART_RX_PIN);
	gpio_set_af(SWO_UART_PORT, 0U, SWO_UART_RX_PIN);
}

uint32_t swo_uart_get_baudrate(void)
{
	return uart_get_baudrate(SWO_UART);
}

#define FIFO_SIZE 256U

/* RX Fifo buffer */
static volatile uint8_t buf_rx[FIFO_SIZE];
/* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
static volatile uint32_t buf_rx_in = 0;
/* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
static volatile uint32_t buf_rx_out = 0;

void trace_buf_push(void)
{
	size_t len;

	if (buf_rx_in == buf_rx_out)
		return;

	if (buf_rx_in > buf_rx_out)
		len = buf_rx_in - buf_rx_out;
	else
		len = FIFO_SIZE - buf_rx_out;

	if (len > 64U)
		len = 64;

	if (usbd_ep_write_packet(usbdev, SWO_ENDPOINT, (uint8_t *)&buf_rx[buf_rx_out], len) == len) {
		buf_rx_out += len;
		buf_rx_out %= FIFO_SIZE;
	}
}

void swo_send_buffer(usbd_device *dev, uint8_t ep)
{
	(void)dev;
	(void)ep;
	trace_buf_push();
}

void trace_tick(void)
{
	trace_buf_push();
}

void SWO_UART_ISR(void)
{
	uint32_t flush = uart_is_interrupt_source(SWO_UART, UART_INT_RT);

	while (!uart_is_rx_fifo_empty(SWO_UART)) {
		const uint32_t c = uart_recv(SWO_UART);

		/* If the next increment of rx_in would put it at the same point
		* as rx_out, the FIFO is considered full.
		*/
		if ((buf_rx_in + 1U) % FIFO_SIZE != buf_rx_out) {
			/* insert into FIFO */
			buf_rx[buf_rx_in++] = c;

			/* wrap out pointer */
			if (buf_rx_in >= FIFO_SIZE)
				buf_rx_in = 0;
		} else {
			flush = 1;
			break;
		}
	}

	if (flush)
		/* advance fifo out pointer by amount written */
		trace_buf_push();
}
