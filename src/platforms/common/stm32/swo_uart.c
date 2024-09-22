/*
 * This file is part of the Black Magic Debug project.
 *
 * Based on work that is Copyright (C) 2017 Black Sphere Technologies Ltd.
 * Copyright (C) 2017 Dave Marples <dave@marples.net>
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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
 * This file implements recovery and capture of UART/NRZ encoded SWO trace output
 *
 * References:
 * DDI0403 - ARMv7-M Architecture Reference Manual, version E.e
 * - https://developer.arm.com/documentation/ddi0403/latest/
 * DDI0314 - CoreSight Components Technical Reference Manual, version 1.0, rev. H
 * - https://developer.arm.com/documentation/ddi0314/latest/
 *
 * We use a hardware UART to capture and recover the data, and DMA to buffer it.
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "swo.h"
#include "swo_internal.h"

#include <libopencmsis/core_cm3.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>

#if defined(DMA_STREAM0)
#define dma_channel_reset(dma, channel)   dma_stream_reset(dma, channel)
#define dma_enable_channel(dma, channel)  dma_enable_stream(dma, channel)
#define dma_disable_channel(dma, channel) dma_disable_stream(dma, channel)

#define DMA_PSIZE_8BIT DMA_SxCR_PSIZE_8BIT
#define DMA_MSIZE_8BIT DMA_SxCR_MSIZE_8BIT
#define DMA_PL_HIGH    DMA_SxCR_PL_HIGH
#else
#define DMA_PSIZE_8BIT DMA_CCR_PSIZE_8BIT
#define DMA_MSIZE_8BIT DMA_CCR_MSIZE_8BIT
#define DMA_PL_HIGH    DMA_CCR_PL_HIGH
#endif

void swo_uart_init(const uint32_t baudrate)
{
	/* Ensure required peripherals are spun up */
	/* TODO: Move this into platform_init()! */
	rcc_periph_clock_enable(SWO_UART_CLK);
	rcc_periph_clock_enable(SWO_DMA_CLK);

	/* Reconfigure the GPIO over to UART mode */
#if defined(STM32F4) || defined(STM32F0) || defined(STM32F3) || defined(STM32F7)
	gpio_mode_setup(SWO_UART_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, SWO_UART_RX_PIN);
	gpio_set_output_options(SWO_UART_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_100MHZ, SWO_UART_RX_PIN);
	gpio_set_af(SWO_UART_PORT, SWO_UART_PIN_AF, SWO_UART_RX_PIN);
#else
	gpio_set_mode(SWO_UART_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, SWO_UART_RX_PIN);
	/* Pull SWO pin high to keep open SWO line ind uart idle state! */
	gpio_set(SWO_UART_PORT, SWO_UART_RX_PIN);
#endif

	/* Set up the UART for 8N1 at the requested baud rate in RX only */
	bmd_usart_set_baudrate(SWO_UART, baudrate);
	usart_set_databits(SWO_UART, 8U);
	usart_set_stopbits(SWO_UART, USART_STOPBITS_1);
	usart_set_mode(SWO_UART, USART_MODE_RX);
	usart_set_parity(SWO_UART, USART_PARITY_NONE);
	usart_set_flow_control(SWO_UART, USART_FLOWCONTROL_NONE);

	/* Set up DMA channel and tell the DMA subsystem where to put the data received from the UART */
	dma_channel_reset(SWO_DMA_BUS, SWO_DMA_CHAN);
	// NOLINTNEXTLINE(clang-diagnostic-pointer-to-int-cast,performance-no-int-to-ptr)
	dma_set_peripheral_address(SWO_DMA_BUS, SWO_DMA_CHAN, (uintptr_t)&SWO_UART_DR);
	// NOLINTNEXTLINE(clang-diagnostic-pointer-to-int-cast)
	dma_set_memory_address(SWO_DMA_BUS, SWO_DMA_CHAN, (uintptr_t)swo_buffer);
	/* Define the buffer length and configure this as a peripheral -> memory transfer */
	dma_set_number_of_data(SWO_DMA_BUS, SWO_DMA_CHAN, SWO_BUFFER_SIZE);
#if defined(DMA_STREAM0)
	dma_set_transfer_mode(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
	dma_channel_select(SWO_DMA_BUS, SWO_DMA_CHAN, SWO_DMA_TRG);
	dma_set_dma_flow_control(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_enable_direct_mode(SWO_DMA_BUS, SWO_DMA_CHAN);
#else
	dma_set_read_from_peripheral(SWO_DMA_BUS, SWO_DMA_CHAN);
#endif
	dma_enable_memory_increment_mode(SWO_DMA_BUS, SWO_DMA_CHAN);
	/* Define it as being bytewise into a circular buffer with high priority */
	dma_set_peripheral_size(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_PSIZE_8BIT);
	dma_set_memory_size(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_MSIZE_8BIT);
	dma_set_priority(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_PL_HIGH);
	dma_enable_circular_mode(SWO_DMA_BUS, SWO_DMA_CHAN);
	/* Enable the 50% and 100% interrupts so we can update the buffer counters to initiate the USB half of the picture */
	dma_enable_transfer_complete_interrupt(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_enable_half_transfer_interrupt(SWO_DMA_BUS, SWO_DMA_CHAN);
	/* Enable DMA trigger on receive for the UART */
	usart_enable_rx_dma(SWO_UART);

	/* Enable the interrupts */
	nvic_set_priority(SWO_DMA_IRQ, IRQ_PRI_SWO_DMA);
	nvic_enable_irq(SWO_DMA_IRQ);

	/* Reset the read and write indicies */
	swo_buffer_read_index = 0U;
	swo_buffer_write_index = 0U;

	/* Now everything has been configured, enable the UART and its associated DMA channel */
	dma_enable_channel(SWO_DMA_BUS, SWO_DMA_CHAN);
	usart_enable(SWO_UART);
}

void swo_uart_deinit(void)
{
	/* Disable the UART and halt DMA for it, grabbing the number of bytes left in the buffer as we do */
	usart_disable(SWO_UART);
	const uint16_t space_remaining = dma_get_number_of_data(SWO_DMA_BUS, SWO_DMA_CHAN);
	dma_disable_channel(SWO_DMA_BUS, SWO_DMA_CHAN);

	/* Convert the counter into an amount captured and add that to the write index and amount available */
	const uint16_t amount = (SWO_BUFFER_SIZE - space_remaining) & ((SWO_BUFFER_SIZE / 2U) - 1U);
	swo_buffer_write_index += amount;
	swo_buffer_bytes_available += amount;

	/* Put the GPIO back into normal service as a GPIO */
#if defined(STM32F4) || defined(STM32F0) || defined(STM32F3) || defined(STM32F7)
	gpio_mode_setup(SWO_UART_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWO_UART_RX_PIN);
#else
	gpio_set_mode(SWO_UART_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, SWO_UART_RX_PIN);
#endif
}

uint32_t swo_uart_get_baudrate(void)
{
	return usart_get_baudrate(SWO_UART);
}

void SWO_DMA_ISR(void)
{
	if (dma_get_interrupt_flag(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_HTIF)) {
		dma_clear_interrupt_flags(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_HTIF);
		swo_buffer_bytes_available += SWO_BUFFER_SIZE / 2U;
	}
	if (dma_get_interrupt_flag(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_TCIF)) {
		dma_clear_interrupt_flags(SWO_DMA_BUS, SWO_DMA_CHAN, DMA_TCIF);
		swo_buffer_bytes_available += SWO_BUFFER_SIZE / 2U;
	}
	swo_send_buffer(usbdev, SWO_ENDPOINT);
}
