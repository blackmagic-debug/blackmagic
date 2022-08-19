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

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>
#elif defined(LM4F)
#include <libopencm3/lm4f/uart.h>
#else
#error "Unknown processor target"
#endif
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

#include "general.h"
#include "usbuart.h"
#include "usb.h"
#include "aux_serial.h"

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
static char aux_serial_transmit_buffer[2U][TX_BUF_SIZE];
static uint8_t aux_serial_transmit_buffer_index;
/* Active buffer part used capacity */
static uint8_t buf_tx_act_sz;
#elif defined(LM4F)
static char aux_serial_transmit_buffer[FIFO_SIZE];
#endif

void aux_serial_set_encoding(struct usb_cdc_line_coding *coding)
{
	usart_set_baudrate(USBUSART, coding->dwDTERate);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
	if (coding->bParityType)
		usart_set_databits(USBUSART, (coding->bDataBits + 1 <= 8 ? 8 : 9));
	else
		usart_set_databits(USBUSART, (coding->bDataBits <= 8 ? 8 : 9));
#elif defined(LM4F)
	uart_set_databits(USBUART, coding->bDataBits);
#endif

	switch(coding->bCharFormat) {
	case 0:
		usart_set_stopbits(USBUSART, USART_STOPBITS_1);
		break;
	case 1:
		usart_set_stopbits(USBUSART, USART_STOPBITS_1_5);
		break;
	case 2:
	default:
		usart_set_stopbits(USBUSART, USART_STOPBITS_2);
		break;
	}

	switch(coding->bParityType) {
	case 0:
		usart_set_parity(USBUSART, USART_PARITY_NONE);
		break;
	case 1:
		usart_set_parity(USBUSART, USART_PARITY_ODD);
		break;
	case 2:
	default:
		usart_set_parity(USBUSART, USART_PARITY_EVEN);
		break;
	}
}

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4)
char *aux_serial_current_transmit_buffer(void)
{
	return aux_serial_transmit_buffer[aux_serial_transmit_buffer_index];
}

size_t aux_serial_transmit_buffer_fullness(void)
{
	return buf_tx_act_sz;
}

/*
 * Changes USBUSART TX buffer in which data is accumulated from USB.
 * Filled buffer is submitted to DMA for transfer.
 */
void aux_serial_switch_transmit_buffers(void)
{
	/* Make the buffer we've been filling the active DMA buffer, and swap to the other */
	dma_set_memory_address(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, (uintptr_t)aux_serial_current_transmit_buffer());
	dma_set_number_of_data(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, buf_tx_act_sz);
	dma_enable_channel(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);

	/* Change active buffer */
	buf_tx_act_sz = 0;
	aux_serial_transmit_buffer_index ^= 1;
}

void aux_serial_send(const size_t len)
{
	buf_tx_act_sz += len;

	/* If DMA is idle, schedule new transfer */
	if (len && tx_trfr_cplt)
	{
		tx_trfr_cplt = false;
		aux_serial_switch_transmit_buffers();

		/* Enable LED */
		usbuart_set_led_state(TX_LED_ACT, true);
	}
}
#elif defined(LM4F)
char *aux_serial_current_transmit_buffer(void)
{
	return aux_serial_transmit_buffer;
}

size_t aux_serial_transmit_buffer_fullness(void)
{
	return 0;
}

void aux_serial_send(const size_t len)
{
	for(size_t i = 0; i < len; ++i)
		uart_send_blocking(USBUART, aux_serial_transmit_buffer[i]);
}
#endif
