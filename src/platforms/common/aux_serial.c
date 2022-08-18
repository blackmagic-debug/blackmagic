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
