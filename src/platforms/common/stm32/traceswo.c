/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file implements capture of Machester SWO trace output
 *
 * References:
 * DDI0403 - ARMv7-M Architecture Reference Manual, version E.e
 * - https://developer.arm.com/documentation/ddi0403/latest/
 * DDI0314 - CoreSight Components Technical Reference Manual, version 1.0, rev. H
 * - https://developer.arm.com/documentation/ddi0314/latest/
 *
 * The basic idea is that SWO comes in on a pin connected to a timer block,
 * and because Manchester coding is self-clocking we can determine the timing
 * for that input signal when it's active, so: use the timer to capture edge
 * transition timings; fire an interrupt each complete cycle; and then use some
 * timing analysis on the CPU to extract the SWO data sequence.
 *
 * We use the first capture channel of a pair to capture the cycle time and
 * thee second to capture the high time (mark period).
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "traceswo.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>

/* How many timer clock cycles the half period of a cycle of the SWO signal is allowed to be off by */
#define ALLOWED_PERIOD_ERROR 5U

/* SWO decoding */
static bool decoding = false;

/* Buffer and fill level for USB */
static uint8_t trace_usb_buf[64U];
static uint8_t trace_usb_buf_size;

/* Manchester bit capture buffer and current bit index */
static uint8_t trace_data[16U];
static uint8_t trace_data_bit_index = 0;
/* Number of timer clock cycles that describe half a bit period as detected */
static uint32_t trace_half_bit_period = 0U;

void traceswo_init(const uint32_t swo_chan_bitmask)
{
	/* Make sure the timer block is clocked on platforms that don't do this in their `platform_init()` */
	TRACE_TIM_CLK_EN();
	/* Then make sure the IO pin used is properly set up as an input routed to the timer */
	gpio_set_mode(SWO_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, SWO_PIN);

	/*
	 * Start setting the timer block up by picking a pair of cross-linked capture channels suitable for the input,
	 * and configure them to consume the input channel for the SWO pin. We use one in rising edge mode and the
	 * other in falling to get the mark period and cycle period - together these define all elements of a wiggle.
	 * NB: "TRACE_IC" here refers to the Input Capture channels being used
	 */
	timer_ic_set_input(TRACE_TIM, TRACE_IC_RISING, TRACE_IC_IN);
	timer_ic_set_polarity(TRACE_TIM, TRACE_IC_RISING, TIM_IC_RISING);
	timer_ic_set_input(TRACE_TIM, TRACE_IC_FALLING, TRACE_IC_IN);
	timer_ic_set_polarity(TRACE_TIM, TRACE_IC_FALLING, TIM_IC_FALLING);

	/*
	 * Use reset mode to trigger the timer, which makes the counter reset and start counting anew
	 * when a rising edge is detected on the input pin via the filtered input channel as a trigger source
	 */
	timer_slave_set_trigger(TRACE_TIM, TRACE_TRIG_IN);
	timer_slave_set_mode(TRACE_TIM, TIM_SMCR_SMS_RM);

	/* Enable capture interrupt */
	nvic_set_priority(TRACE_IRQ, IRQ_PRI_TRACE);
	nvic_enable_irq(TRACE_IRQ);
	timer_enable_irq(TRACE_TIM, TRACE_ITR_RISING);

	/* Enable the capture channels */
	timer_ic_enable(TRACE_TIM, TRACE_IC_RISING);
	timer_ic_enable(TRACE_TIM, TRACE_IC_FALLING);

	timer_enable_counter(TRACE_TIM);

#if defined(STM32F4) || defined(STM32F0) || defined(STM32F3)
	/* AF2: TIM3/TIM4/TIM5 */
	gpio_mode_setup(TDO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TDO_PIN);
	gpio_set_af(TDO_PORT, TRACE_TIM_PIN_AF, TDO_PIN);
#endif

	traceswo_setmask(swo_chan_bitmask);
	decoding = swo_chan_bitmask != 0;
}

void traceswo_deinit(void)
{
	/* Disable the timer capturing the incomming data stream */
	timer_disable_counter(TRACE_TIM);
	timer_slave_set_mode(TRACE_TIM, TIM_SMCR_SMS_OFF);

	/* Reset state so that when init is called we wind up in a fresh capture state */
	trace_data_bit_index = 0U;
	trace_half_bit_period = 0U;
}

void trace_buf_push(uint8_t *buf, int len)
{
	if (decoding)
		traceswo_decode(usbdev, CDCACM_UART_ENDPOINT, buf, len);
	else if (usbd_ep_write_packet(usbdev, USB_REQ_TYPE_IN | TRACE_ENDPOINT, buf, len) != len) {
		if (trace_usb_buf_size + len > 64) {
			/* Stall if upstream to too slow. */
			usbd_ep_stall_set(usbdev, USB_REQ_TYPE_IN | TRACE_ENDPOINT, 1);
			trace_usb_buf_size = 0;
			return;
		}
		memcpy(trace_usb_buf + trace_usb_buf_size, buf, len);
		trace_usb_buf_size += len;
	}
}

void trace_buf_drain(usbd_device *dev, uint8_t ep)
{
	if (!trace_usb_buf_size)
		return;

	if (decoding)
		traceswo_decode(dev, CDCACM_UART_ENDPOINT, trace_usb_buf, trace_usb_buf_size);
	else
		usbd_ep_write_packet(dev, ep, trace_usb_buf, trace_usb_buf_size);
	trace_usb_buf_size = 0;
}

void TRACE_ISR(void)
{
	static uint8_t lastbit;
	static bool halfbit = false;
	static bool notstart = false;
	const uint16_t status = TIM_SR(TRACE_TIM);

	/* Reset decoder state if capture overflowed */
	if (status & (TRACE_STATUS_OVERFLOW | TIM_SR_UIF)) {
		timer_clear_flag(TRACE_TIM, TRACE_STATUS_OVERFLOW | TIM_SR_UIF);
		if (!(status & (TRACE_STATUS_RISING | TRACE_STATUS_FALLING)))
			goto flush_and_reset;
	}

	const uint32_t cycle_period = TRACE_CC_RISING;
	const uint32_t mark_period = TRACE_CC_FALLING;

	/* Reset decoder state if crazy things happened */
	if ((trace_half_bit_period &&
			(mark_period / trace_half_bit_period > 2U || mark_period / trace_half_bit_period == 0)) ||
		mark_period == 0)
		goto flush_and_reset;

	/* Check if we're here for a reason other than a valid cycle capture */
	if (!(status & TRACE_STATUS_RISING))
		notstart = true;

	/* If the bit time is not yet known */
	if (trace_half_bit_period == 0U) {
		/* Are we here because we got an interrupt but not for the rising edge capture channel? */
		if (notstart) {
			/* We're are, so leave early */
			notstart = false;
			return;
		}
		/*
		 * We're here because of the rising edge, so we've got our first (start) bit.
		 * Calculate the ratio of the mark period to the space period within a cycle
		 *
		 * At this point, the waveform for what's come in should look something like one of these two options:
		 * ▁▁┊╱▔╲▁┊╱▔  ▁▁┊╱▔╲▁┊▁▁╱▔
		 * The first sequence is the start bit followed by a 1, and the second is followed instead by a 0
		 */
		const uint32_t adjusted_mark_period = mark_period - ALLOWED_PERIOD_ERROR;
		const uint32_t duty_ratio = cycle_period / adjusted_mark_period;
		/*
		 * Check that the duty cycle ratio is between 2:1 and 3:1, indicating an
		 * aproximately even mark-to-space ratio, taking into account the possibility of
		 * the double space bit time caused by start + 0
		 */
		if (duty_ratio < 2U || duty_ratio > 3U)
			return;
		/*
		 * Now we've established a valid duty cycle ratio, store the mark period as the bit timing and
		 * initialise the capture engine: start with the last bit we decoded as a 1, that we're not dealing
		 * with a half bit (such as one caused by a stop bit), and configure the timer maximum period
		 * to 6x the current max half bit period, enabling overflow checking now we have an overflow target for
		 * the timer
		 */
		trace_half_bit_period = adjusted_mark_period;
		lastbit = 1;
		halfbit = false;
		/* XXX: Need to make sure that this isn't setting a value outside the range of the timer */
		timer_set_period(TRACE_TIM, mark_period * 6U);
		timer_clear_flag(TRACE_TIM, TIM_SR_UIF);
		timer_enable_irq(TRACE_TIM, TIM_DIER_UIE);
	} else {
		/*
		 * We know the period of a half cycle, so check that the half period of the new bit isn't too long.
		 * If it is, then this was a bit flip (representing either a 0 -> 1, or 1 -> 0 transition).
		 *
		 * 0 -> 1 transition: ▁▁╱▔┊▔▔╲▁
		 * 1 -> 0 transition: ▔▔╲▁┊▁▁╱▔
		 */
		if (mark_period >= trace_half_bit_period * 2U) {
			/* If we're processing a half bit, then this probably actually means we lost sync */
			if (!halfbit)
				goto flush_and_reset;
			halfbit = false;
			lastbit ^= 1U;
		}
		/* If this would start a new byte in the data buffer, zero it to start with */
		if ((trace_data_bit_index & 7U) == 0U)
			trace_data[trace_data_bit_index >> 3U] = 0U;
		/* Store the new bit in the buffer and move along */
		trace_data[trace_data_bit_index >> 3U] |= lastbit << (trace_data_bit_index & 7U);
		++trace_data_bit_index;
	}

	if (!(status & TRACE_STATUS_RISING) || (cycle_period - mark_period) / trace_half_bit_period > 2)
		goto flush_and_reset;

	if ((cycle_period - mark_period) / trace_half_bit_period > 1) {
		/* If low time extended we need to pack another bit. */
		if (halfbit) /* this is a valid stop-bit or we lost sync */
			goto flush_and_reset;
		halfbit = true;
		lastbit ^= 1U;
		trace_data[trace_data_bit_index >> 3U] |= lastbit << (trace_data_bit_index & 7U);
		++trace_data_bit_index;
	}

	if (trace_data_bit_index < 128U)
		return;

flush_and_reset:
	timer_set_period(TRACE_TIM, -1);
	timer_disable_irq(TRACE_TIM, TIM_DIER_UIE);
	trace_buf_push(trace_data, trace_data_bit_index >> 3U);
	trace_data_bit_index = 0;
	trace_half_bit_period = 0;
}
