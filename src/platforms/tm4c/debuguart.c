/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Copyright (C) 2020 Francesco Valla <valla.francesco@gmail.com>
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

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "general.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/uart.h>

void debuguart_init(void)
{
	DEBUGUART_PIN_SETUP();

	periph_clock_enable(DEBUGUART_CLK);
	__asm__("nop"); __asm__("nop"); __asm__("nop");

	uart_disable(DEBUGUART);

	/* Setup UART parameters. */
	uart_clock_from_sysclk(DEBUGUART);
	uart_set_baudrate(DEBUGUART, 115200);
	uart_set_databits(DEBUGUART, 8);
	uart_set_stopbits(DEBUGUART, 1);
	uart_set_parity(DEBUGUART, UART_PARITY_NONE);

	// Enable FIFO
	uart_enable_fifo(DEBUGUART);

	// Set FIFO interrupt trigger levels to 1/8 full for RX buffer and
	// 7/8 empty (1/8 full) for TX buffer
	uart_set_fifo_trigger_levels(DEBUGUART, UART_FIFO_RX_TRIG_1_8, UART_FIFO_TX_TRIG_7_8);

	/* Finally enable the USART. */
	uart_enable(DEBUGUART);
}

void debuguart_test(void)
{
	for(int i = 0; i < 10; i++)
		uart_send_blocking(DEBUGUART, 'a' + i);
}

int _write(int file, char *ptr, int len)
{
	int i;

	if (file == STDOUT_FILENO || file == STDERR_FILENO) {
		for (i = 0; i < len; i++) {
			if (ptr[i] == '\n') {
				uart_send_blocking(DEBUGUART, '\r');
			}
			uart_send_blocking(DEBUGUART, ptr[i]);
		}
		return i;
	}
	errno = EIO;
	return -1;
}
