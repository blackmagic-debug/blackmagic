/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
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
#include "morse.h"

#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>

bool running_status = false;
static volatile uint32_t time_ms = 0;
uint32_t swd_delay_cnt = 0;

static size_t morse_tick = 0;

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

void platform_max_frequency_set(uint32_t freq)
{
	uint32_t divisor = rcc_ahb_frequency - USED_SWD_CYCLES * freq;
	/* If we now have an insanely big divisor, the above operation wrapped to a negative signed number. */
	if (divisor >= 0x80000000U) {
		swd_delay_cnt = 0;
		return;
	}
	divisor /= 2U;
	swd_delay_cnt = divisor / (CYCLES_PER_CNT * freq);
	if (swd_delay_cnt * (CYCLES_PER_CNT * freq) < divisor)
		++swd_delay_cnt;
}

uint32_t platform_max_frequency_get(void)
{
	uint32_t ret = rcc_ahb_frequency;
	ret /= USED_SWD_CYCLES + CYCLES_PER_CNT * swd_delay_cnt;
	return ret;
}
