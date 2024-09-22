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
 * This file implements recovery and capture of Machester encoded SWO trace output
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
#include "swo.h"
#include "swo_internal.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>

/* How many timer clock cycles the half period of a cycle of the SWO signal is allowed to be off by */
#define ALLOWED_PERIOD_ERROR 5U

#define TIM_SR_MASK                                                                                       \
	(TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_TIF | TIM_SR_CC1OF | \
		TIM_SR_CC2OF | TIM_SR_CC3OF | TIM_SR_CC4OF)

/* Manchester bit capture buffer and current bit index */
static uint8_t swo_data[16U];
static uint8_t swo_data_bit_index = 0;
/* Number of timer clock cycles that describe half a bit period as detected */
static uint32_t swo_half_bit_period = 0U;

void swo_manchester_init(void)
{
	/* Make sure the timer block is clocked on platforms that don't do this in their `platform_init()` */
	SWO_TIM_CLK_EN();

#if defined(STM32F4) || defined(STM32F0) || defined(STM32F3) || defined(STM32F7)
	/* Set any required pin alt-function configuration - TIM3/TIM4/TIM5 are AF2 */
	gpio_mode_setup(SWO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SWO_PIN);
	gpio_set_af(SWO_PORT, SWO_TIM_PIN_AF, SWO_PIN);
#else
	/* Then make sure the IO pin used is properly set up as an input routed to the timer */
	gpio_set_mode(SWO_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, SWO_PIN);
#endif

	/*
	 * Start setting the timer block up by picking a pair of cross-linked capture channels suitable for the input,
	 * and configure them to consume the input channel for the SWO pin. We use one in rising edge mode and the
	 * other in falling to get the mark period and cycle period - together these define all elements of a wiggle.
	 * NB: "SWO_IC" here refers to the Input Capture channels being used
	 */
	timer_ic_set_input(SWO_TIM, SWO_IC_RISING, SWO_IC_IN);
	timer_ic_set_polarity(SWO_TIM, SWO_IC_RISING, TIM_IC_RISING);
	timer_ic_set_input(SWO_TIM, SWO_IC_FALLING, SWO_IC_IN);
	timer_ic_set_polarity(SWO_TIM, SWO_IC_FALLING, TIM_IC_FALLING);

	/*
	 * Use reset mode to trigger the timer, which makes the counter reset and start counting anew
	 * when a rising edge is detected on the input pin via the filtered input channel as a trigger source
	 */
	timer_slave_set_trigger(SWO_TIM, SWO_TRIG_IN);
	timer_slave_set_mode(SWO_TIM, TIM_SMCR_SMS_RM);

	/* Enable capture interrupt */
	nvic_set_priority(SWO_TIM_IRQ, IRQ_PRI_SWO_TIM);
	nvic_enable_irq(SWO_TIM_IRQ);
	timer_enable_irq(SWO_TIM, SWO_ITR_RISING);

	/* Enable the capture channels */
	timer_ic_enable(SWO_TIM, SWO_IC_RISING);
	timer_ic_enable(SWO_TIM, SWO_IC_FALLING);
	/* Make sure all the status register bits are cleared prior to enabling the counter */
	timer_clear_flag(SWO_TIM, TIM_SR_MASK);
	/* Set the period to an improbable value */
	timer_set_period(SWO_TIM, UINT32_MAX);

	/* Now we've got everything configured and ready, enable the timer */
	timer_enable_counter(SWO_TIM);
}

void swo_manchester_deinit(void)
{
	/* Disable the timer capturing the incomming data stream */
	timer_disable_counter(SWO_TIM);
	timer_slave_set_mode(SWO_TIM, TIM_SMCR_SMS_OFF);

	/* Reset state so that when init is called we wind up in a fresh capture state */
	swo_data_bit_index = 0U;
	swo_half_bit_period = 0U;

#if defined(STM32F4) || defined(STM32F0) || defined(STM32F3) || defined(STM32F7)
	gpio_mode_setup(SWO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWO_PIN);
#else
	/* Put the GPIO back into normal service as a GPIO */
	gpio_set_mode(SWO_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, SWO_PIN);
#endif
}

void swo_buffer_data(void)
{
	const uint8_t byte_count = swo_data_bit_index >> 3U;
	/* First, see how much space we have in the buffer and move what we can */
	const uint16_t amount = MIN(byte_count, SWO_BUFFER_SIZE - swo_buffer_write_index);
	memcpy(swo_buffer + swo_buffer_write_index, swo_data, amount);
	swo_buffer_write_index += amount;
	swo_buffer_write_index &= SWO_BUFFER_SIZE - 1U;
	swo_buffer_bytes_available += amount;
	/* Make sure we're sending the data if we've got more than an endpoint buffer's worth */
	if (swo_buffer_bytes_available >= SWO_ENDPOINT_SIZE) {
		swo_send_buffer(usbdev, SWO_ENDPOINT);
		/* If we have anything left to move, put that at the start of the buffer */
		if (amount != byte_count) {
			const uint16_t remainder = byte_count - amount;
			memcpy(swo_buffer, swo_data + amount, remainder);
			swo_buffer_write_index = remainder;
			swo_buffer_bytes_available += remainder;
		}
	}
	swo_data_bit_index = 0U;
}

void SWO_TIM_ISR(void)
{
	static uint8_t bit_value;
	const uint16_t status = TIM_SR(SWO_TIM);

	const uint32_t cycle_period = SWO_CC_RISING;
	/* Check that we entered the handler because of a fresh trigger but have not yet had a chance to capture data */
	if ((status & SWO_STATUS_RISING) && cycle_period == 0U) {
		/* Clear the rising edge flag and wait for it to set again */
		timer_clear_flag(SWO_TIM, SWO_STATUS_RISING | SWO_STATUS_FALLING | SWO_STATUS_OVERFLOW);
		return;
	}

	timer_clear_flag(SWO_TIM, SWO_STATUS_RISING | SWO_STATUS_FALLING | SWO_STATUS_OVERFLOW | TIM_SR_UIF);

	const uint32_t mark_period = SWO_CC_FALLING;
	const uint32_t space_period = cycle_period - mark_period;

	/* Reset decoder state if crazy things happened */
	if (cycle_period <= mark_period || (swo_half_bit_period && mark_period < swo_half_bit_period) || mark_period == 0U)
		goto flush_and_reset;

	/* If the bit time is not yet known */
	if (swo_half_bit_period == 0U) {
		/* Are we here because we got an interrupt but not for the rising edge capture channel? */
		if (!(status & SWO_STATUS_RISING))
			/* We're are, so leave early */
			return;
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
		 * initialise the capture engine: check whether we captured, the start of a 0 bit to set the next
		 * bit value, and configure the timer maximum period to 6x the current max half bit period, enabling
		 * overflow checking now we have an overflow target for the timer
		 */
		swo_half_bit_period = adjusted_mark_period;
		bit_value = space_period >= swo_half_bit_period * 2U ? 0U : 1U;
		/* XXX: Need to make sure that this isn't setting a value outside the range of the timer */
		timer_set_period(SWO_TIM, mark_period * 6U);
		timer_clear_flag(SWO_TIM, TIM_SR_UIF | SWO_STATUS_OVERFLOW);
		timer_enable_irq(SWO_TIM, TIM_DIER_UIE);
	} else {
		/*
		 * We start off needing to store a newly captured bit - the value of which is determined in the *previous*
		 * traversal of this function. We don't yet worry about whether we're starting half way through a bit or not.
		 *
		 * If this would start a new byte in the data buffer, zero it to start with
		 */
		if ((swo_data_bit_index & 7U) == 0U)
			swo_data[swo_data_bit_index >> 3U] = 0U;
		/* Store the new bit in the buffer and move along */
		swo_data[swo_data_bit_index >> 3U] |= bit_value << (swo_data_bit_index & 7U);
		++swo_data_bit_index;

		/*
		 * Having stored a bit, check if we've got a long cycle period - this can happen due to any sequence
		 * involving at least one bit transition (0 -> 1, 1 -> 0), or a 1 -> STOP sequence:
		 * 0 -> 1:    ▁▁╱▔┊▔▔╲▁
		 * 1 -> 0:    ▔▔╲▁┊▁▁╱▔
		 * 1 -> STOP: ▔▔╲▁┊▁▁▁▁
		 *
		 * An even longer non-stop cycle time occurs when a 0 -> 1 -> 0 sequence is encountered:
		 * ▁▁╱▔┊▔▔╲▁┊▁▁╱▔
		 *
		 * All of these cases need special handling and can appear to this decoder as part of one of the following:
		 * 0 -> 1 -> 0:    ▁▁╱▔┊▔▔╲▁┊▁▁╱▔ (4x half bit periods)
		 * 0 -> 1 -> 1:    ▁▁╱▔┊▔▔╲▁┊╱▔╲▁ (3x half bit periods)
		 * 0 -> 1 -> STOP: ▁▁╱▔┊▔▔╲▁┊▁▁▁▁
		 * 1 -> 1 -> 0:    ▔▔╲▁┊╱▔╲▁┊▁▁╱▔ (3x half bit periods)
		 * 1 -> 1 -> STOP: ▔▔╲▁┊╱▔╲▁┊▁▁▁▁
		 * 1 -> 0 -> STOP: ▔▔╲▁┊▁▁╱▔┊╲▁▁▁
		 *
		 * The bit write that has already occured deals with the lead-in part of all of these.
		 */
		if (cycle_period >= swo_half_bit_period * 3U) {
			/*
			 * Having determined that we're in a long cycle, we need to figure out which kind.
			 * If the mark period is short, then whether we're starting half way into a bit determines
			 * if the next is a 1 (not half way in) or a 0 (half way in). This copies the current bit value.
			 * If the mark period is long, then this can only occur from a 0 -> 1 transition where we're
			 * half way into the cycle. Anything else indicates a fault occured.
			 */
			if (mark_period >= swo_half_bit_period * 2U) {
				if (bit_value == 1U)
					goto flush_and_reset; /* Something bad happened and we lost sync */
				bit_value = 1U;
			}

			/*
			 * We now know the value of the extra bit, if it's from anything other than a short mark, long space,
			 * then we need to store that next bit.
			 */
			if (mark_period >= swo_half_bit_period * 2U || space_period < swo_half_bit_period * 2U) {
				/* If this would overflow the buffer, then do nothing */
				if (swo_data_bit_index < 128U) {
					/* If this would start a new byte in the data buffer, zero it to start with */
					if ((swo_data_bit_index & 7U) == 0U)
						swo_data[swo_data_bit_index >> 3U] = 0U;
					/* Store the new bit in the buffer and move along */
					swo_data[swo_data_bit_index >> 3U] |= bit_value << (swo_data_bit_index & 7U);
					++swo_data_bit_index;
				}
			}
			/* If it's a long space, we just saw a 1 -> 0 transition */
			if (space_period >= swo_half_bit_period * 2U) {
				/* Unless of course this was acompanied by a short mark period, in which case it's a STOP bit */
				if (bit_value == 0U)
					goto flush_and_reset;
				bit_value = 0U;
			}

			/*
			 * We've now written enough data to the buffer, so we have one final check:
			 * If the cycle has a long space, we need to determine how long to check for STOP bits.
			 */
			if (space_period >= swo_half_bit_period * 3U)
				goto flush_and_reset;
		}
	}

	/* If the buffer is not full, and we haven't encountered a STOP bit, we're done here */
	if (swo_data_bit_index < 128U)
		return;

flush_and_reset:
	timer_set_period(SWO_TIM, UINT32_MAX);
	timer_disable_irq(SWO_TIM, TIM_DIER_UIE);
	swo_buffer_data();
	swo_half_bit_period = 0;
}
