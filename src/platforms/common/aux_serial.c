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

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/cortex.h>
#elif defined(LM4F)
#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/uart.h>
#else
#error "Unknown processor target"
#endif
#include <libopencm3/cm3/nvic.h>

#include "general.h"
#include "platform.h"
#include "usb_serial.h"
#include "aux_serial.h"

static uint32_t aux_serial_active_baud_rate;

static char aux_serial_receive_buffer[AUX_UART_BUFFER_SIZE];
/* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
static uint8_t aux_serial_receive_write_index = 0;
/* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
static uint8_t aux_serial_receive_read_index = 0;

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
static char aux_serial_transmit_buffer[2U][AUX_UART_BUFFER_SIZE];
static uint8_t aux_serial_transmit_buffer_index = 0;
static uint8_t aux_serial_transmit_buffer_consumed = 0;
static bool aux_serial_transmit_complete = true;

static volatile uint8_t aux_serial_led_state = 0;

#ifdef DMA_STREAM0
#define dma_channel_reset(dma, channel)   dma_stream_reset(dma, channel)
#define dma_enable_channel(dma, channel)  dma_enable_stream(dma, channel)
#define dma_disable_channel(dma, channel) dma_disable_stream(dma, channel)

#define DMA_PSIZE_8BIT DMA_SxCR_PSIZE_8BIT
#define DMA_MSIZE_8BIT DMA_SxCR_MSIZE_8BIT
#define DMA_PL_HIGH    DMA_SxCR_PL_HIGH
#define DMA_CGIF       DMA_ISR_FLAGS
#else
#define DMA_PSIZE_8BIT DMA_CCR_PSIZE_8BIT
#define DMA_MSIZE_8BIT DMA_CCR_MSIZE_8BIT
#define DMA_PL_HIGH    DMA_CCR_PL_HIGH
#define DMA_CGIF       DMA_IFCR_CGIF_BIT
#endif
#elif defined(LM4F)
static char aux_serial_transmit_buffer[AUX_UART_BUFFER_SIZE];

#define USBUSART           USBUART
#define USART_STOPBITS_1   1
#define USART_STOPBITS_1_5 1
#define USART_STOPBITS_2   2
#define USART_PARITY_NONE  UART_PARITY_NONE
#define USART_PARITY_ODD   UART_PARITY_ODD
#define USART_PARITY_EVEN  UART_PARITY_EVEN

#define usart_enable(uart)                 uart_enable(uart)
#define usart_disable(uart)                uart_disable(uart)
#define usart_set_baudrate(uart, baud)     uart_set_baudrate(uart, baud)
#define usart_get_databits(uart)           uart_get_databits(uart)
#define usart_get_stopbits(uart)           uart_get_stopbits(uart)
#define usart_set_stopbits(uart, stopbits) uart_set_stopbits(uart, stopbits)
#define usart_get_parity(uart)             uart_get_parity(uart)
#define usart_set_parity(uart, parity)     uart_set_parity(uart, parity)
#endif

static void aux_serial_set_baudrate(const uint32_t baud_rate)
{
	usart_set_baudrate(USBUSART, baud_rate);
	aux_serial_active_baud_rate = baud_rate;
}

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
void aux_serial_init(void)
{
	/* Enable clocks */
	rcc_periph_clock_enable(USBUSART_CLK);
	rcc_periph_clock_enable(USBUSART_DMA_CLK);

	/* Setup UART parameters */
	UART_PIN_SETUP();
	aux_serial_set_baudrate(38400);
	usart_set_databits(USBUSART, 8);
	usart_set_stopbits(USBUSART, USART_STOPBITS_1);
	usart_set_mode(USBUSART, USART_MODE_TX_RX);
	usart_set_parity(USBUSART, USART_PARITY_NONE);
	usart_set_flow_control(USBUSART, USART_FLOWCONTROL_NONE);
	USART_CR1(USBUSART) |= USART_CR1_IDLEIE;

	/* Setup USART TX DMA */
#if !defined(USBUSART_TDR) && defined(USBUSART_DR)
#define USBUSART_TDR USBUSART_DR
#elif !defined(USBUSART_TDR)
#define USBUSART_TDR USART_DR(USBUSART)
#endif
#if !defined(USBUSART_RDR) && defined(USBUSART_DR)
#define USBUSART_RDR USBUSART_DR
#elif !defined(USBUSART_RDR)
#define USBUSART_RDR USART_DR(USBUSART)
#endif
	dma_channel_reset(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);
	dma_set_peripheral_address(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, (uintptr_t)&USBUSART_TDR);
	dma_enable_memory_increment_mode(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);
	dma_set_peripheral_size(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, DMA_PSIZE_8BIT);
	dma_set_memory_size(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, DMA_MSIZE_8BIT);
	dma_set_priority(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, DMA_PL_HIGH);
	dma_enable_transfer_complete_interrupt(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);
#ifdef DMA_STREAM0
	dma_set_transfer_mode(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
	dma_channel_select(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, USBUSART_DMA_TRG);
	dma_set_dma_flow_control(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);
	dma_enable_direct_mode(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);
#else
	dma_set_read_from_memory(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);
#endif

	/* Setup USART RX DMA */
	dma_channel_reset(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
	dma_set_peripheral_address(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, (uintptr_t)&USBUSART_RDR);
	dma_set_memory_address(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, (uintptr_t)aux_serial_receive_buffer);
	dma_set_number_of_data(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, AUX_UART_BUFFER_SIZE);
	dma_enable_memory_increment_mode(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
	dma_enable_circular_mode(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
	dma_set_peripheral_size(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, DMA_PSIZE_8BIT);
	dma_set_memory_size(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, DMA_MSIZE_8BIT);
	dma_set_priority(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, DMA_PL_HIGH);
	dma_enable_half_transfer_interrupt(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
	dma_enable_transfer_complete_interrupt(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
#ifdef DMA_STREAM0
	dma_set_transfer_mode(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, DMA_SxCR_DIR_PERIPHERAL_TO_MEM);
	dma_channel_select(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN, USBUSART_DMA_TRG);
	dma_set_dma_flow_control(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
	dma_enable_direct_mode(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
#else
	dma_set_read_from_peripheral(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
#endif
	dma_enable_channel(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);

	/* Enable interrupts */
	nvic_set_priority(USBUSART_IRQ, IRQ_PRI_USBUSART);
#if defined(USBUSART_DMA_RXTX_IRQ)
	nvic_set_priority(USBUSART_DMA_RXTX_IRQ, IRQ_PRI_USBUSART_DMA);
#else
	nvic_set_priority(USBUSART_DMA_TX_IRQ, IRQ_PRI_USBUSART_DMA);
	nvic_set_priority(USBUSART_DMA_RX_IRQ, IRQ_PRI_USBUSART_DMA);
#endif
	nvic_enable_irq(USBUSART_IRQ);
#if defined(USBUSART_DMA_RXTX_IRQ)
	nvic_enable_irq(USBUSART_DMA_RXTX_IRQ);
#else
	nvic_enable_irq(USBUSART_DMA_TX_IRQ);
	nvic_enable_irq(USBUSART_DMA_RX_IRQ);
#endif

	/* Finally enable the USART */
	usart_enable(USBUSART);
	usart_enable_tx_dma(USBUSART);
	usart_enable_rx_dma(USBUSART);
}
#elif defined(LM4F)
void aux_serial_init(void)
{
	UART_PIN_SETUP();

	periph_clock_enable(USBUART_CLK);
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");

	uart_disable(USBUART);

	/* Setup UART parameters. */
	uart_clock_from_sysclk(USBUART);
	aux_serial_set_baudrate(38400);
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
	uart_enable_interrupts(UART0, UART_INT_RX | UART_INT_RT);

	/* Finally enable the USART. */
	uart_enable(USBUART);

	//nvic_set_priority(USBUSART_IRQ, IRQ_PRI_USBUSART);
	nvic_enable_irq(USBUART_IRQ);
}
#endif

void aux_serial_set_encoding(const usb_cdc_line_coding_s *const coding)
{
	/* Some devices require that the usart is disabled before
	 * changing the usart registers. */
	usart_disable(USBUSART);
	aux_serial_set_baudrate(coding->dwDTERate);

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
	if (coding->bParityType != USB_CDC_NO_PARITY)
		usart_set_databits(USBUSART, coding->bDataBits + 1U <= 8U ? 8 : 9);
	else
		usart_set_databits(USBUSART, coding->bDataBits <= 8U ? 8 : 9);
#elif defined(LM4F)
	uart_set_databits(USBUART, coding->bDataBits);
#endif

	uint32_t stop_bits = USART_STOPBITS_2;
	switch (coding->bCharFormat) {
	case USB_CDC_1_STOP_BITS:
		stop_bits = USART_STOPBITS_1;
		break;
	case USB_CDC_1_5_STOP_BITS:
		stop_bits = USART_STOPBITS_1_5;
		break;
	case USB_CDC_2_STOP_BITS:
	default:
		break;
	}
	usart_set_stopbits(USBUSART, stop_bits);

	switch (coding->bParityType) {
	case USB_CDC_NO_PARITY:
	default:
		usart_set_parity(USBUSART, USART_PARITY_NONE);
		break;
	case USB_CDC_ODD_PARITY:
		usart_set_parity(USBUSART, USART_PARITY_ODD);
		break;
	case USB_CDC_EVEN_PARITY:
		usart_set_parity(USBUSART, USART_PARITY_EVEN);
		break;
	}
	usart_enable(USBUSART);
}

void aux_serial_get_encoding(usb_cdc_line_coding_s *const coding)
{
	coding->dwDTERate = aux_serial_active_baud_rate;

	switch (usart_get_stopbits(USBUSART)) {
	case USART_STOPBITS_1:
		coding->bCharFormat = USB_CDC_1_STOP_BITS;
		break;
#if !defined(LM4F)
	/*
	 * Only include this back mapping on non-Tiva-C platforms as USART_STOPBITS_1 and
	 * USART_STOPBITS_1_5 are the same thing on LM4F
	 */
	case USART_STOPBITS_1_5:
		coding->bCharFormat = USB_CDC_1_5_STOP_BITS;
		break;
#endif
	case USART_STOPBITS_2:
	default:
		coding->bCharFormat = USB_CDC_2_STOP_BITS;
		break;
	}

	switch (usart_get_parity(USBUSART)) {
	case USART_PARITY_NONE:
	default:
		coding->bParityType = USB_CDC_NO_PARITY;
		break;
	case USART_PARITY_ODD:
		coding->bParityType = USB_CDC_ODD_PARITY;
		break;
	case USART_PARITY_EVEN:
		coding->bParityType = USB_CDC_EVEN_PARITY;
		break;
	}

	const uint32_t data_bits = usart_get_databits(USBUSART);
	if (coding->bParityType == USB_CDC_NO_PARITY)
		coding->bDataBits = data_bits;
	else
		coding->bDataBits = data_bits - 1;
}

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7)
void aux_serial_set_led(const aux_serial_led_e led)
{
	aux_serial_led_state |= led;
	gpio_set(LED_PORT_UART, LED_UART);
}

void aux_serial_clear_led(const aux_serial_led_e led)
{
	aux_serial_led_state &= ~led;
	if (!aux_serial_led_state)
		gpio_clear(LED_PORT_UART, LED_UART);
}

char *aux_serial_current_transmit_buffer(void)
{
	return aux_serial_transmit_buffer[aux_serial_transmit_buffer_index];
}

size_t aux_serial_transmit_buffer_fullness(void)
{
	return aux_serial_transmit_buffer_consumed;
}

/*
 * Changes USBUSART TX buffer in which data is accumulated from USB.
 * Filled buffer is submitted to DMA for transfer.
 */
void aux_serial_switch_transmit_buffers(void)
{
	/* Make the buffer we've been filling the active DMA buffer, and swap to the other */
	char *const current_buffer = aux_serial_current_transmit_buffer();
	dma_set_memory_address(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, (uintptr_t)current_buffer);
	dma_set_number_of_data(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN, aux_serial_transmit_buffer_consumed);
	dma_enable_channel(USBUSART_DMA_BUS, USBUSART_DMA_TX_CHAN);

	/* Change active buffer */
	aux_serial_transmit_buffer_consumed = 0;
	aux_serial_transmit_buffer_index ^= 1U;
}

void aux_serial_send(const size_t len)
{
	aux_serial_transmit_buffer_consumed += len;

	/* If DMA is idle, schedule new transfer */
	if (len && aux_serial_transmit_complete) {
		aux_serial_transmit_complete = false;
		aux_serial_switch_transmit_buffers();
		aux_serial_set_led(AUX_SERIAL_LED_TX);
	}
}

void aux_serial_update_receive_buffer_fullness(void)
{
	aux_serial_receive_write_index =
		AUX_UART_BUFFER_SIZE - dma_get_number_of_data(USBUSART_DMA_BUS, USBUSART_DMA_RX_CHAN);
	aux_serial_receive_write_index %= AUX_UART_BUFFER_SIZE;
}

bool aux_serial_receive_buffer_empty(void)
{
	return aux_serial_receive_write_index == aux_serial_receive_read_index;
}

void aux_serial_drain_receive_buffer(void)
{
	aux_serial_receive_read_index = aux_serial_receive_write_index;
	aux_serial_clear_led(AUX_SERIAL_LED_RX);
}

void aux_serial_stage_receive_buffer(void)
{
	aux_serial_receive_read_index = debug_serial_fifo_send(
		aux_serial_receive_buffer, aux_serial_receive_read_index, aux_serial_receive_write_index);
}

static void aux_serial_receive_isr(const uint32_t usart, const uint8_t dma_irq)
{
	nvic_disable_irq(dma_irq);

	/* Get IDLE flag and reset interrupt flags */
	const bool is_idle = usart_get_flag(usart, USART_FLAG_IDLE);
	usart_recv(usart);

	/* If line is now idle, then transmit a packet */
	if (is_idle) {
#ifdef USART_ICR_IDLECF
		USART_ICR(usart) = USART_ICR_IDLECF;
#endif
		debug_serial_run();
	}

	nvic_enable_irq(dma_irq);
}

static void aux_serial_dma_transmit_isr(const uint8_t dma_tx_channel)
{
	nvic_disable_irq(USB_IRQ);

	/* Stop DMA */
	dma_disable_channel(USBUSART_DMA_BUS, dma_tx_channel);
	dma_clear_interrupt_flags(USBUSART_DMA_BUS, dma_tx_channel, DMA_CGIF);

	/*
	 * If a new buffer is ready, continue transmission.
	 * Otherwise we report the transfer has completed.
	 */
	if (aux_serial_transmit_buffer_fullness()) {
		aux_serial_switch_transmit_buffers();
		usbd_ep_nak_set(usbdev, CDCACM_UART_ENDPOINT, 0);
	} else {
		aux_serial_clear_led(AUX_SERIAL_LED_TX);
		aux_serial_transmit_complete = true;
	}

	nvic_enable_irq(USB_IRQ);
}

static void aux_serial_dma_receive_isr(const uint8_t usart_irq, const uint8_t dma_rx_channel)
{
	nvic_disable_irq(usart_irq);

	/* Clear flags and transmit a packet*/
	dma_clear_interrupt_flags(USBUSART_DMA_BUS, dma_rx_channel, DMA_CGIF);
	debug_serial_run();

	nvic_enable_irq(usart_irq);
}

#if defined(USBUSART_ISR)
void USBUSART_ISR(void)
{
#if defined(USBUSART_DMA_RXTX_IRQ)
	aux_serial_receive_isr(USBUSART, USBUSART_DMA_RXTX_IRQ);
#else
	aux_serial_receive_isr(USBUSART, USBUSART_DMA_RX_IRQ);
#endif
}
#endif

#if defined(USBUSART1_ISR)
void USBUSART1_ISR(void)
{
#if defined(USBUSART1_DMA_RXTX_IRQ)
	aux_serial_receive_isr(USBUSART1, USBUSART1_DMA_RXTX_IRQ);
#else
	aux_serial_receive_isr(USBUSART1, USBUSART1_DMA_RX_IRQ);
#endif
}
#endif

#if defined(USBUSART2_ISR)
void USBUSART2_ISR(void)
{
#if defined(USBUSART2_DMA_RXTX_IRQ)
	aux_serial_receive_isr(USBUSART2, USBUSART2_DMA_RXTX_IRQ);
#else
	aux_serial_receive_isr(USBUSART2, USBUSART2_DMA_RX_IRQ);
#endif
}
#endif

#if defined(USBUSART_DMA_TX_ISR)
void USBUSART_DMA_TX_ISR(void)
{
	aux_serial_dma_transmit_isr(USBUSART_DMA_TX_CHAN);
}
#endif

#if defined(USBUSART1_DMA_TX_ISR)
void USBUSART1_DMA_TX_ISR(void)
{
	aux_serial_dma_transmit_isr(USBUSART1_DMA_TX_CHAN);
}
#endif

#if defined(USBUSART2_DMA_TX_ISR)
void USBUSART2_DMA_TX_ISR(void)
{
	aux_serial_dma_transmit_isr(USBUSART2_DMA_TX_CHAN);
}
#endif

#if defined(USBUSART_DMA_RX_ISR)
void USBUSART_DMA_RX_ISR(void)
{
	aux_serial_dma_receive_isr(USBUSART_IRQ, USBUSART_DMA_RX_CHAN);
}
#endif

#if defined(USBUSART1_DMA_RX_ISR)
void USBUSART1_DMA_RX_ISR(void)
{
	aux_serial_dma_receive_isr(USBUSART1_IRQ, USBUSART1_DMA_RX_CHAN);
}
#endif

#if defined(USBUSART2_DMA_RX_ISR)
void USBUSART2_DMA_RX_ISR(void)
{
	aux_serial_dma_receive_isr(USBUSART2_IRQ, USBUSART2_DMA_RX_CHAN);
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
	for (size_t i = 0; i < len; ++i)
		uart_send_blocking(USBUART, aux_serial_transmit_buffer[i]);
}

/*
 * Read a character from the UART RX and stuff it in a software FIFO.
 * Allowed to read from FIFO out pointer, but not write to it.
 * Allowed to write to FIFO in pointer.
 */
void USBUART_ISR(void)
{
	bool flush = uart_is_interrupt_source(USBUART, UART_INT_RT);

	while (!uart_is_rx_fifo_empty(USBUART)) {
		const char c = uart_recv(USBUART);

		/* If the next increment of rx_in would put it at the same point
		* as rx_out, the FIFO is considered full.
		*/
		if (((aux_serial_receive_write_index + 1U) % AUX_UART_BUFFER_SIZE) != aux_serial_receive_read_index) {
			/* insert into FIFO */
			aux_serial_receive_buffer[aux_serial_receive_write_index++] = c;

			/* wrap out pointer */
			if (aux_serial_receive_write_index >= AUX_UART_BUFFER_SIZE)
				aux_serial_receive_write_index = 0;
		} else
			flush = true;
	}

	if (flush) {
		/* forcibly empty fifo if no USB endpoint */
		if (usb_get_config() != 1) {
			aux_serial_receive_read_index = aux_serial_receive_write_index;
			return;
		}

		char packet_buf[CDCACM_PACKET_SIZE];
		uint8_t packet_size = 0;
		uint8_t buf_out = aux_serial_receive_read_index;

		/* copy from uart FIFO into local usb packet buffer */
		while (aux_serial_receive_write_index != buf_out && packet_size < CDCACM_PACKET_SIZE) {
			packet_buf[packet_size++] = aux_serial_receive_buffer[buf_out++];

			/* wrap out pointer */
			if (buf_out >= AUX_UART_BUFFER_SIZE)
				buf_out = 0;
		}

		/* advance fifo out pointer by amount written */
		aux_serial_receive_read_index += usbd_ep_write_packet(usbdev, CDCACM_UART_ENDPOINT, packet_buf, packet_size);
		aux_serial_receive_read_index %= AUX_UART_BUFFER_SIZE;
	}
}
#endif
