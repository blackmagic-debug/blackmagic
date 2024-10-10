/*
 * This file is part of the Black Magic Debug project.
 *
 * Based on work that is Copyright (C) 2017 Black Sphere Technologies Ltd.
 * Based on work that is Copyright (C) 2017 Dave Marples <dave@marples.net>
 * Updates for ctxLink Copyright (C) Sid Price 2024 <sid@sidprice.com>
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

/* This file implements capture of the TRACESWO output using ASYNC signalling.
 *
 * ARM DDI 0403D - ARMv7M Architecture Reference Manual
 * ARM DDI 0337I - Cortex-M3 Technical Reference Manual
 * ARM DDI 0314H - CoreSight Components Technical Reference Manual
 */

/* TDO/TRACESWO signal comes into the SWOUSART RX pin.
 */

#include "general.h"
// #include "cdcacm.h"
#include "traceswo.h"
#include "platform.h"
#include "WiFi_Server.h"

#include <libopencm3/cm3/common.h>
#include <libopencmsis/core_cm3.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

/* For speed this is set to the USB transfer size */
#define FULL_SWO_PACKET (64)
/* Default line rate....used as default for a request without baudrate */

#define BUFFER_SIZE 1024

static volatile uint32_t input_buffer = 0;  // input buffer index
static volatile uint32_t output_buffer = 0; // output buffer index
_Atomic uint32_t buffer_size = 0;           // Number of bytes in the buffer

static uint8_t trace_rx_buf[BUFFER_SIZE] = {0};
#define NUM_PINGPONG_BUFFERS 2
static uint8_t pingpong_buffers[NUM_PINGPONG_BUFFERS * FULL_SWO_PACKET] = {0};
static uint32_t buffer_select = 0;
//
// Check for SWO Trace nework client, if present send
// any queued data
//
static uint8_t swo_data[BUFFER_SIZE];

void trace_send_data(void)
{
	if (is_swo_trace_client_connected()) {
		uint32_t dataCount = buffer_size;
		if (dataCount >= FULL_SWO_PACKET) {
			//
			// Copy the data
			//
			for (uint32_t i = 0; i < dataCount; i++) {
				swo_data[i] = trace_rx_buf[output_buffer++];
				if (output_buffer >= BUFFER_SIZE)
					output_buffer = 0;
			}
			send_swo_trace_data(&swo_data[0], dataCount);
			buffer_count -= datasize;
		}
	}
}

// TODO Will address the functions leading underscore when Wi-Fi SWO is implemented against base platform
#if 0
void _trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	uint32_t outCount;
	uint8_t *bufferPointer, *bufferStart;

	__atomic_load(&buffer_size, &outCount, __ATOMIC_RELAXED);
	if (outCount == 0)
		return;
	//
	// If we have an SWO network client there is no more
	// to do. The network code will pick up the data
	// and deal with it directly out of the trace_rx_buf
	//
	if (is_swo_trace_client_connected())
		return;
	//
	// Set up the pointer to grab the data
	//
	bufferPointer = bufferStart = &pingpong_buffers[buffer_select * FULL_SWO_PACKET];
	//
	// Copy the data
	//
	for (uint32_t i = 0; i < outCount; i++) {
		*bufferPointer++ = trace_rx_buf[output_buffer++];
		if (output_buffer >= BUFFER_SIZE) {
			output_buffer = 0;
		}
	}
	//
	// Bump the pingpong buffer selection
	//
	buffer_select = (buffer_select + 1) % NUM_PINGPONG_BUFFERS;
	usbd_ep_write_packet(dev, ep, bufferStart, outCount);
	__atomic_fetch_sub(&buffer_size, outCount, __ATOMIC_RELAXED);
}
#endif
/* TODO Address this */
void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	(void)dev;
	(void)ep;
	// _trace_buf_drain(usbdev, ep);
}

#define TRACE_TIM_COMPARE_VALUE 2000

static volatile uint32_t error_count = 0;

void SWO_UART_ISR(void)
{
	uint32_t err = USART_SR(SWO_UART);
	char ch = usart_recv(SWO_UART);

	if (err & (USART_FLAG_ORE | USART_FLAG_FE | USART_SR_NE)) {
		error_count++;
		return;
	}
	/* If the next increment of rx_in would put it at the same point
	 * as rx_out, the FIFO is considered full.
	 */
	uint32_t copyOutBuf;
	__atomic_load(&output_buffer, &copyOutBuf, __ATOMIC_RELAXED);
	if (((input_buffer + 1) % BUFFER_SIZE) != copyOutBuf) {
		/* insert into FIFO */
		trace_rx_buf[input_buffer++] = ch;
		__atomic_fetch_add(&buffer_size, 1, __ATOMIC_RELAXED); // buffer_size++ ;

		/* wrap out pointer */
		if (input_buffer >= BUFFER_SIZE) {
			input_buffer = 0;
		}
		//
		// If we have a packet-sized amount of data send
		// it to USB
		//
		uint32_t outCount;
		__atomic_load(&buffer_size, &outCount, __ATOMIC_RELAXED);
		// if (outCount >= FULL_SWO_PACKET)
		if (outCount >= FULL_SWO_PACKET) {
			/* TODO Address this */
			//_trace_buf_drain(usbdev, USB_TRACESWO_ENDPOINT);
		}
	} else {
		// Just drop data ????
	}
}

/** TODO Rework this, the native prototype is updated! */
// void traceswo_init(uint32_t baudrate, uint32_t swo_chan_bitmask)
void traceswo_init(uint32_t swo_chan_bitmask)
{
	(void)swo_chan_bitmask;
	// (void)swo_chan_bitmask;
	// rcc_periph_clock_enable(SWO_UART_CLK);
	// gpio_mode_setup(SWO_UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SWO_UART_RX_PIN);
	// gpio_set_af(SWO_UART_PORT, GPIO_AF8, SWO_UART_RX_PIN);
	// //
	// if (!baudrate)
	// 	baudrate = DEFAULTSPEED;
	// /* Setup input UART parameters. */
	// usart_set_baudrate(SWO_UART, baudrate);
	// usart_set_databits(SWO_UART, 8);
	// usart_set_stopbits(SWO_UART, USART_STOPBITS_1);
	// usart_set_mode(SWO_UART, USART_MODE_RX);
	// usart_set_parity(SWO_UART, USART_PARITY_NONE);
	// usart_set_flow_control(SWO_UART, USART_FLOWCONTROL_NONE);
	// usart_enable(SWO_UART);
	// //
	// // If we have a network client for GDB, ensure
	// // the SWO Trace server is active
	// //
	// if (isGDBClientConnected()) {
	// 	// Check if SWO Trace Server is already active
	// 	if (!swo_trace_server_active()) {
	// 		wifi_setup_swo_trace_server();
	// 	}
	// }
	// // Enable interrupts
	// SWO_UART_CR1 |= USART_CR1_RXNEIE;
	// nvic_set_priority(SWO_UART_IRQ, IRQ_PRI_SWOUSART);
	// nvic_enable_irq(SWO_UART_IRQ);
}
