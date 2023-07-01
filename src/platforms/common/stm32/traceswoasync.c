/*
 * This file is part of the Black Magic Debug project.
 *
 * Based on work that is Copyright (C) 2017 Black Sphere Technologies Ltd.
 * Copyright (C) 2017 Dave Marples <dave@marples.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file implements capture of the Trace/SWO output using async signalling.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

/* TDO/TRACESWO signal comes into the SWOUSART RX pin. */

#include <stdatomic.h>
#include "general.h"
#include "platform.h"
#include "usb.h"
#include "traceswo.h"

#include <libopencmsis/core_cm3.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>

static volatile uint32_t write_index; /* Packet currently received via UART */
static volatile uint32_t read_index;  /* Packet currently waiting to transmit to USB */
/* Packets arrived from the SWO interface */
static uint8_t trace_rx_buf[NUM_TRACE_PACKETS * TRACE_ENDPOINT_SIZE];
/* Packet pingpong buffer used for receiving packets */
static uint8_t pingpong_buf[2 * TRACE_ENDPOINT_SIZE];
/* SWO decoding */
static bool decoding = false;

void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	static atomic_flag reentry_flag = ATOMIC_FLAG_INIT;

	/* If we are already in this routine then we don't need to come in again */
	if (atomic_flag_test_and_set_explicit(&reentry_flag, memory_order_relaxed))
		return;
	/* Attempt to write everything we buffered */
	if (write_index != read_index) {
		uint16_t result;
		if (decoding)
			/* write decoded swo packets to the uart port */
			result = traceswo_decode(
				dev, CDCACM_UART_ENDPOINT, &trace_rx_buf[read_index * TRACE_ENDPOINT_SIZE], TRACE_ENDPOINT_SIZE);
		else
			/* write raw swo packets to the trace port */
			result =
				usbd_ep_write_packet(dev, ep, &trace_rx_buf[read_index * TRACE_ENDPOINT_SIZE], TRACE_ENDPOINT_SIZE);
		if (result)
			read_index = (read_index + 1U) % NUM_TRACE_PACKETS;
	}
	atomic_flag_clear_explicit(&reentry_flag, memory_order_relaxed);
}

void traceswo_setspeed(uint32_t baudrate)
{
	dma_disable_channel(SWO_DMA_BUS, SWO_DMA_CHAN);
	usart_disable(SWO_UART);
	usart_set_baudrate(SWO_UART, baudrate);
	usart_set_databits(SWO_UART, 8);
	usart_set_stopbits(SWO_UART, USART_STOPBITS_1);
	usart_set_mode(SWO_UART, USART_MODE_RX);
	usart_set_parity(SWO_UART, USART_PARITY_NONE);
	usart_set_flow_control(SWO_UART, USART_FLOWCONTROL_NONE);

	/* Set up DMA channel */
	dma_channel_reset(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_set_peripheral_address(SWO_DMA_BUS, SWO_DMA_CHAN, (uint32_t)&SWO_UART_DR);
	dma_set_read_from_peripheral(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_enable_memory_increment_mode(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_set_peripheral_size(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_CCR_PSIZE_8BIT);
	dma_set_memory_size(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_CCR_MSIZE_8BIT);
	dma_set_priority(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_CCR_PL_HIGH);
	dma_enable_transfer_complete_interrupt(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_enable_half_transfer_interrupt(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_enable_circular_mode(SWO_DMA_BUS, SWO_DMA_CHAN);

	usart_enable(SWO_UART);
	nvic_enable_irq(SWO_DMA_IRQ);
	write_index = read_index = 0;
	dma_set_memory_address(SWO_DMA_BUS, SWO_DMA_CHAN, (uint32_t)pingpong_buf);
	dma_set_number_of_data(SWO_DMA_BUS, SWO_DMA_CHAN, 2 * TRACE_ENDPOINT_SIZE);
	dma_enable_channel(SWO_DMA_BUS, SWO_DMA_CHAN);
	usart_enable_rx_dma(SWO_UART);
}

void SWO_DMA_ISR(void)
{
	if (DMA_ISR(SWO_DMA_BUS) & DMA_ISR_HTIF(SWO_DMA_CHAN)) {
		DMA_IFCR(SWO_DMA_BUS) |= DMA_ISR_HTIF(SWO_DMA_CHAN);
		memcpy(&trace_rx_buf[write_index * TRACE_ENDPOINT_SIZE], pingpong_buf, TRACE_ENDPOINT_SIZE);
	}
	if (DMA_ISR(SWO_DMA_BUS) & DMA_ISR_TCIF(SWO_DMA_CHAN)) {
		DMA_IFCR(SWO_DMA_BUS) |= DMA_ISR_TCIF(SWO_DMA_CHAN);
		memcpy(
			&trace_rx_buf[write_index * TRACE_ENDPOINT_SIZE], &pingpong_buf[TRACE_ENDPOINT_SIZE], TRACE_ENDPOINT_SIZE);
	}
	write_index = (write_index + 1U) % NUM_TRACE_PACKETS;
	trace_buf_drain(usbdev, TRACE_ENDPOINT | USB_REQ_TYPE_IN);
}

void traceswo_init(uint32_t baudrate, uint32_t swo_chan_bitmask)
{
	if (!baudrate)
		baudrate = SWO_DEFAULT_BAUD;

	rcc_periph_clock_enable(SWO_UART_CLK);
	rcc_periph_clock_enable(SWO_DMA_CLK);

	gpio_set_mode(SWO_UART_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, SWO_UART_RX_PIN);
	/* Pull SWO pin high to keep open SWO line ind uart idle state! */
	gpio_set(SWO_UART_PORT, SWO_UART_RX_PIN);
	nvic_set_priority(SWO_DMA_IRQ, IRQ_PRI_SWO_DMA);
	nvic_enable_irq(SWO_DMA_IRQ);
	traceswo_setspeed(baudrate);
	traceswo_setmask(swo_chan_bitmask);
	decoding = (swo_chan_bitmask != 0);
}
