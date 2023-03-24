/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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
#include "general.h"
#include "platform.h"
#include "morse.h"

#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/adc.h>

bool running_status = false;
static volatile uint32_t time_ms = 0;
uint32_t target_clk_divider = 0;

static size_t morse_tick = 0;
#ifdef PLATFORM_HAS_POWER_SWITCH
static uint8_t monitor_ticks = 0;

/* Derived from calculating (1.2V / 3.0V) * 4096 */
#define ADC_VREFINT_MAX 1638U
/*
 * Derived from calculating (1.2V / 3.6V) * 4096 (1365) and
 * then applying an offset to adjust for being 10-20mV over
 */
#define ADC_VREFINT_MIN 1404U
#endif

void platform_timing_init(void)
{
	/* Setup heartbeat timer */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	/* Interrupt us at 100 Hz */
	systick_set_reload(rcc_ahb_frequency / (8U * SYSTICKHZ));
	/* SYSTICK_IRQ with low priority */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 14U << 4U);
	systick_interrupt_enable();
	systick_counter_enable();
}

void platform_delay(uint32_t ms)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, ms);
	while (!platform_timeout_is_expired(&timeout))
		continue;
}

void sys_tick_handler(void)
{
	time_ms += SYSTICKMS;

	if (morse_tick >= MORSECNT) {
		if (running_status)
			gpio_toggle(LED_PORT, LED_IDLE_RUN);
		SET_ERROR_STATE(morse_update());
		morse_tick = 0;
	} else
		++morse_tick;

#ifdef PLATFORM_HAS_POWER_SWITCH
	/* First check if target power is presently enabled */
	if (platform_target_get_power()) {
		/*
		 * Every 10 systicks, set up an ADC conversion on the 9th tick, then
		 * read back the value on the 10th, checking the internal bandgap reference
		 * is still sat in the correct range. If it diverges down, this indicates
		 * backfeeding and that VCC is being pulled higher than 3.3V. If it diverges
		 * up, this indicates either backfeeding or overcurrent and that VCC is being
		 * pulled below 3.3V. In either case, for safety, disable tpwr and set
		 * a morse error of "TPWR ERROR"
		 */

		/* If we're on the 9th tick, start the bandgap conversion */
		if (monitor_ticks == 8U) {
			uint8_t channel = ADC_CHANNEL_VREF;
			adc_set_regular_sequence(ADC1, 1, &channel);
			adc_start_conversion_direct(ADC1);
		}

		/* If we're on the 10th tick, check the result of bandgap conversion */
		if (monitor_ticks == 9U) {
			const uint32_t ref = adc_read_regular(ADC1);
			/* Clear EOC bit. The GD32F103 does not automatically reset it on ADC read. */
			ADC_SR(ADC1) &= ~ADC_SR_EOC;
			monitor_ticks = 0;

			/* Now compare the reference against the known good range */
			if (ref > ADC_VREFINT_MAX || ref < ADC_VREFINT_MIN) {
				/* Something's wrong, so turn tpwr off and set the morse blink pattern */
				platform_target_set_power(false);
				morse("TPWR ERROR", true);
			}
		} else
			++monitor_ticks;
	} else
		monitor_ticks = 0;
#endif
}

uint32_t platform_time_ms(void)
{
	return time_ms;
}

/*
 * Assume some USED_SWD_CYCLES per clock and CYCLES_PER_CNT cycles
 * per delay loop count with 2 delay loops per clock
 */

/* Values for STM32F103 at 72 MHz */
#define USED_SWD_CYCLES 22
#define CYCLES_PER_CNT  10

void platform_max_frequency_set(const uint32_t frequency)
{
#ifdef BITBANG_CALIBRATED_FREQS
	/*
	 * If the frequency requested is above the value given when no delays are used of any kind,
	 * then set the clock divider to UINT32_MAX which the bitbanging routines translate to the right thing.
	 */
	if (frequency > BITBANG_0_DELAY_FREQ)
		target_clk_divider = UINT32_MAX;
	/* If the frequency requested is 0, then set the divider factor to the largest available number */
	else if (!frequency)
		target_clk_divider = UINT32_MAX - 1U;
	else {
		/*
		 * For everything else, start by dividing the CPU frequency by the requested
		 * frequency to make a division ratio for the divider
		 */
		const uint32_t ratio = rcc_ahb_frequency / frequency;
		/* Then apply the offset and division factor to arrive at a divider value */
		target_clk_divider = (ratio - BITBANG_DIVIDER_OFFSET) / BITBANG_DIVIDER_FACTOR;
	}
#else
	uint32_t divisor = rcc_ahb_frequency - USED_SWD_CYCLES * frequency;
	/* If we now have an insanely big divisor, the above operation wrapped to a negative signed number. */
	if (divisor >= 0x80000000U) {
		target_clk_divider = UINT32_MAX;
		return;
	}
	divisor /= 2U;
	target_clk_divider = divisor / (CYCLES_PER_CNT * frequency);
	if (target_clk_divider * (CYCLES_PER_CNT * frequency) < divisor)
		++target_clk_divider;
#endif
}

uint32_t platform_max_frequency_get(void)
{
#ifdef BITBANG_CALIBRATED_FREQS
	/* If we aren't applying a division factor, return the no-delay clock frequency */
	if (target_clk_divider == UINT32_MAX)
		return BITBANG_NO_DELAY_FREQ;
	/*
	 * Otherwise we have to run the above calculations in reverse:
	 * Start by multiplying the divider used by the division factor, then add the offset to get back to a ratio.
	 * Finally, divide the CPU clock frequency by the ratio to get back to the actual clock frequency being
	 * generated by the bitbanging routines.
	 */
	const uint32_t ratio = (target_clk_divider * BITBANG_DIVIDER_FACTOR) + BITBANG_DIVIDER_OFFSET;
	return rcc_ahb_frequency / ratio;
#else
	uint32_t result = rcc_ahb_frequency;
	result /= USED_SWD_CYCLES + CYCLES_PER_CNT * target_clk_divider;
	return result;
#endif
}
