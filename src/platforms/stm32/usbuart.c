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
/* RX usb transfer complete */
bool rx_usb_trfr_cplt = true;

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

#define USBUSART_DMA_RX_ISR_TEMPLATE(USART_IRQ, DMA_RX_CHAN) do {		\
	nvic_disable_irq(USART_IRQ);						\
										\
	/* Clear flags */							\
	dma_clear_interrupt_flags(USBUSART_DMA_BUS, DMA_RX_CHAN, DMA_CGIF);	\
	/* Transmit a packet */							\
	debug_uart_run();								\
										\
	nvic_enable_irq(USART_IRQ);						\
} while(0)

#if defined(USBUSART_DMA_RX_ISR)
void USBUSART_DMA_RX_ISR(void)
{
	USBUSART_DMA_RX_ISR_TEMPLATE(USBUSART_IRQ, USBUSART_DMA_RX_CHAN);
}
#endif

#if defined(USBUSART1_DMA_RX_ISR)
void USBUSART1_DMA_RX_ISR(void)
{
	USBUSART_DMA_RX_ISR_TEMPLATE(USBUSART1_IRQ, USBUSART1_DMA_RX_CHAN);
}
#endif

#if defined(USBUSART2_DMA_RX_ISR)
void USBUSART2_DMA_RX_ISR(void)
{
	USBUSART_DMA_RX_ISR_TEMPLATE(USBUSART2_IRQ, USBUSART2_DMA_RX_CHAN);
}
#endif

#if defined(USBUSART_DMA_RXTX_ISR)
void USBUSART_DMA_RXTX_ISR(void)
{
	if (dma_get_interrupt_flag(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, DMA_CGIF))
		USBUSART_DMA_RX_ISR();
	if (dma_get_interrupt_flag(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, DMA_CGIF))
		USBUSART_DMA_TX_ISR();
}
#endif
