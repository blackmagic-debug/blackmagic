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
#include "general.h"
#include "cdcacm.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/uart.h>

#define FIFO_SIZE 128

/* RX Fifo buffer */
static uint8_t buf_rx[FIFO_SIZE];
/* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
static uint8_t buf_rx_in;
/* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
static uint8_t buf_rx_out;

void usbuart_init(void)
{
	UART_PIN_SETUP();
	
	periph_clock_enable(USBUART_CLK);
	__asm__("nop"); __asm__("nop"); __asm__("nop");
	
	uart_disable(USBUART);

	/* Setup UART parameters. */
	uart_clock_from_sysclk(USBUART);
	uart_set_baudrate(USBUART, 38400);
	uart_set_databits(USBUART, 8);
	uart_set_stopbits(USBUART, 1);
	uart_set_parity(USBUART, UART_PARITY_NONE);

	// Enable FIFO
	uart_enable_fifo(USBUART);

	// Set FIFO interrupt trigger levels to 1/8 full for RX buffer and
	// 7/8 empty (1/8 full) for TX buffer
	uart_set_fifo_trigger_levels(USBUART, UART_FIFO_RX_TRIG_1_8, UART_FIFO_TX_TRIG_7_8);

	uart_clear_interrupt_flag(USBUART, UART_INT_RX | UART_INT_RT);

	/* Enable interrupts */
	uart_enable_interrupts(UART0, UART_INT_RX| UART_INT_RT);

	/* Finally enable the USART. */
	uart_enable(USBUART);

	//nvic_set_priority(USBUSART_IRQ, IRQ_PRI_USBUSART);
	nvic_enable_irq(USBUART_IRQ);
}

void usbuart_set_line_coding(struct usb_cdc_line_coding *coding)
{
	uart_set_baudrate(USBUART, coding->dwDTERate);
	uart_set_databits(USBUART, coding->bDataBits);
	switch(coding->bCharFormat) {
	case 0:
	case 1:
		uart_set_stopbits(USBUART, 1);
		break;
	case 2:
		uart_set_stopbits(USBUART, 2);
		break;
	}
	switch(coding->bParityType) {
	case 0:
		uart_set_parity(USBUART, UART_PARITY_NONE);
		break;
	case 1:
		uart_set_parity(USBUART, UART_PARITY_ODD);
		break;
	case 2:
		uart_set_parity(USBUART, UART_PARITY_EVEN);
		break;
	}
}

void usbuart_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;

	char buf[CDCACM_PACKET_SIZE];
	int len = usbd_ep_read_packet(dev, CDCACM_UART_ENDPOINT,
					buf, CDCACM_PACKET_SIZE);

	for(int i = 0; i < len; i++)
		uart_send_blocking(USBUART, buf[i]);
}


void usbuart_usb_in_cb(usbd_device *dev, uint8_t ep)
{
	(void) dev;
	(void) ep;
}

/*
 * Read a character from the UART RX and stuff it in a software FIFO.
 * Allowed to read from FIFO out pointer, but not write to it.
 * Allowed to write to FIFO in pointer.
 */
void USBUART_ISR(void)
{
	int flush = uart_is_interrupt_source(USBUART, UART_INT_RT);

	while (!uart_is_rx_fifo_empty(USBUART)) {
		char c = uart_recv(USBUART);

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
		}
	}

	if (flush) {
		/* forcibly empty fifo if no USB endpoint */
		if (cdcacm_get_config() != 1)
		{
			buf_rx_out = buf_rx_in;
			return;
		}

		uint8_t packet_buf[CDCACM_PACKET_SIZE];
		uint8_t packet_size = 0;
		uint8_t buf_out = buf_rx_out;

		/* copy from uart FIFO into local usb packet buffer */
		while (buf_rx_in != buf_out && packet_size < CDCACM_PACKET_SIZE)
		{
			packet_buf[packet_size++] = buf_rx[buf_out++];

			/* wrap out pointer */
			if (buf_out >= FIFO_SIZE)
			{
				buf_out = 0;
			}

		}

		/* advance fifo out pointer by amount written */
		buf_rx_out += usbd_ep_write_packet(usbdev,
				CDCACM_UART_ENDPOINT, packet_buf, packet_size);
		buf_rx_out %= FIFO_SIZE;
	}
}

