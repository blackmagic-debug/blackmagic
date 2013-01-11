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

#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/f1/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

#include "platform.h"

void usbuart_init(void)
{
	/* On mini hardware, UART and SWD share connector pins.
	 * Don't enable UART if we're being debugged. */
	if ((platform_hwversion() == 1) && (SCS_DEMCR & SCS_DEMCR_TRCENA))
		return;

	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_USART1EN);

	/* UART1 TX to 'alternate function output push-pull' */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO9);

	/* Setup UART parameters. */
	usart_set_baudrate(USART1, 38400);
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_STOPBITS_1);
	usart_set_mode(USART1, USART_MODE_TX_RX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

	/* Finally enable the USART. */
	usart_enable(USART1);

	/* Enable interrupts */
	USART1_CR1 |= USART_CR1_RXNEIE;
	nvic_set_priority(NVIC_USART1_IRQ, IRQ_PRI_USART1);
	nvic_enable_irq(NVIC_USART1_IRQ);
}

void usbuart_set_line_coding(struct usb_cdc_line_coding *coding)
{
	usart_set_baudrate(USART1, coding->dwDTERate);
	usart_set_databits(USART1, coding->bDataBits);
	switch(coding->bCharFormat) {
	case 0:
		usart_set_stopbits(USART1, USART_STOPBITS_1);
		break;
	case 1:
		usart_set_stopbits(USART1, USART_STOPBITS_1_5);
		break;
	case 2:
		usart_set_stopbits(USART1, USART_STOPBITS_2);
		break;
	}
	switch(coding->bParityType) {
	case 0:
		usart_set_parity(USART1, USART_PARITY_NONE);
		break;
	case 1:
		usart_set_parity(USART1, USART_PARITY_ODD);
		break;
	case 2:
		usart_set_parity(USART1, USART_PARITY_EVEN);
		break;
	}
}

void usbuart_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;

	char buf[CDCACM_PACKET_SIZE];
	int len = usbd_ep_read_packet(dev, CDCACM_UART_ENDPOINT,
					buf, CDCACM_PACKET_SIZE);

	/* Don't bother if uart is disabled.
	 * This will be the case on mini while we're being debugged. 
	 */
	if(!(RCC_APB2ENR & RCC_APB2ENR_USART1EN)) 
		return;

	gpio_set(LED_PORT, LED_UART);
	for(int i = 0; i < len; i++)
		usart_send_blocking(USART1, buf[i]);
	gpio_clear(LED_PORT, LED_UART);
}

static uint8_t uart_usb_buf[CDCACM_PACKET_SIZE];
static uint8_t uart_usb_buf_size;

void usbuart_usb_in_cb(usbd_device *dev, uint8_t ep)
{
	if (!uart_usb_buf_size) {
		gpio_clear(LED_PORT, LED_UART);
		return;
	}

	usbd_ep_write_packet(dev, ep, uart_usb_buf, uart_usb_buf_size);
	uart_usb_buf_size = 0;
}

void usart1_isr(void)
{
	char c = usart_recv(USART1);

	gpio_set(LED_PORT, LED_UART);

	/* Try to send now */
	if (usbd_ep_write_packet(usbdev, CDCACM_UART_ENDPOINT, &c, 1) == 1)
		return;

	/* We failed, so queue for later */
	if (uart_usb_buf_size == CDCACM_PACKET_SIZE) {
		/* Drop if the buffer's full: we have no flow control */
		return;
	}

	uart_usb_buf[uart_usb_buf_size++] = c;
}

