/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
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

/* This file implements the SW-DP interface. */

#include "general.h"
#include "platform.h"
#include "timing.h"
#include "swd.h"
#include "maths_utils.h"

#if !defined(SWDIO_IN_PORT)
#define SWDIO_IN_PORT SWDIO_PORT
#endif
#if !defined(SWDIO_IN_PIN)
#define SWDIO_IN_PIN SWDIO_PIN
#endif

typedef enum swdio_status_e {
	SWDIO_STATUS_FLOAT = 0,
	SWDIO_STATUS_DRIVE
} swdio_status_t;

swd_proc_s swd_proc;

static void swdptap_turnaround(swdio_status_t dir) __attribute__((optimize(3)));
static uint32_t swdptap_seq_in(size_t clock_cycles) __attribute__((optimize(3)));
static bool swdptap_seq_in_parity(uint32_t *ret, size_t clock_cycles) __attribute__((optimize(3)));
static void swdptap_seq_out(uint32_t tms_states, size_t clock_cycles) __attribute__((optimize(3)));
static void swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles) __attribute__((optimize(3)));

/*
 * Overall strategy for timing consistency:
 *
 * - Each primitive ends with a falling clock edge
 * - Output is driven after the falling clock edge
 * - Input is read immediately before the rising clock edge
 * - Each primitive assumes it was immediately preceded by a falling clock edge
 *
 * This increases the chances of meeting setup and hold times when the target
 * connection is lower bandwidth (with adequately slower clocks configured).
 */

void swdptap_init(void)
{
	swd_proc.seq_in = swdptap_seq_in;
	swd_proc.seq_in_parity = swdptap_seq_in_parity;
	swd_proc.seq_out = swdptap_seq_out;
	swd_proc.seq_out_parity = swdptap_seq_out_parity;
}

static void swdptap_turnaround(const swdio_status_t dir)
{
	static swdio_status_t olddir = SWDIO_STATUS_FLOAT;
	/* Don't turnaround if direction not changing */
	if (dir == olddir)
		return;
	olddir = dir;

#ifdef DEBUG_SWD_BITS
	DEBUG_INFO("%s", dir ? "\n-> " : "\n<- ");
#endif

	if (dir == SWDIO_STATUS_FLOAT) {
		SWDIO_MODE_FLOAT();
	}
	for (volatile uint32_t counter = target_clk_divider + 1; counter > 0; --counter)
		continue;

	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for (volatile uint32_t counter = target_clk_divider + 1; counter > 0; --counter)
		continue;

	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	if (dir == SWDIO_STATUS_DRIVE) {
		SWDIO_MODE_DRIVE();
	}
}

static uint32_t swdptap_seq_in_clk_delay(size_t clock_cycles) __attribute__((optimize(3)));

static uint32_t swdptap_seq_in_clk_delay(const size_t clock_cycles)
{
	uint32_t value = 0;
	if (!clock_cycles)
		return 0;
	/*
	 * Count down instead of up, because with an up-count, some ARM-GCC
	 * versions use an explicit CMP, missing the optimization of converting
	 * to a faster down-count that uses SUBS followed by BCS/BCC.
	 */
	for (size_t cycle = clock_cycles; cycle--;) {
		for (volatile uint32_t counter = target_clk_divider; counter > 0; --counter)
			continue;
		const bool bit = gpio_get(SWDIO_IN_PORT, SWDIO_IN_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		for (volatile uint32_t counter = target_clk_divider; counter > 0; --counter)
			continue;
		value >>= 1U;
		value |= (uint32_t)bit << 31U;
		/* Reordering barrier */
		__asm__("" ::: "memory");
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		/* Reordering barrier */
		__asm__("" ::: "memory");
	}
	value >>= (32U - clock_cycles);
	return value;
}

static uint32_t swdptap_seq_in_no_delay(size_t clock_cycles) __attribute__((optimize(3)));

static uint32_t swdptap_seq_in_no_delay(const size_t clock_cycles)
{
	if (!clock_cycles)
		return 0;
	uint32_t value = 0;
	/*
	 * Count down instead of up, because with an up-count, some ARM-GCC
	 * versions use an explicit CMP, missing the optimization of converting
	 * to a faster down-count that uses SUBS followed by BCS/BCC.
	 */
	for (size_t cycle = clock_cycles; cycle--;) {
		/* Reordering barrier */
		__asm__("" ::: "memory");
		bool bit = gpio_get(SWDIO_IN_PORT, SWDIO_IN_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		__asm__("nop" ::: "memory");
		value >>= 1U;
		value |= (uint32_t)bit << 31U;
		/* Reordering barrier */
		__asm__("" ::: "memory");
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		/* Reordering barrier */
		__asm__("" ::: "memory");
	}
	value >>= (32U - clock_cycles);
	return value;
}

static uint32_t swdptap_seq_in(size_t clock_cycles)
{
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (target_clk_divider != UINT32_MAX)
		return swdptap_seq_in_clk_delay(clock_cycles);
	else // NOLINT(readability-else-after-return)
		return swdptap_seq_in_no_delay(clock_cycles);
}

static bool swdptap_seq_in_parity(uint32_t *ret, size_t clock_cycles)
{
	const uint32_t result = swdptap_seq_in(clock_cycles);
	for (volatile uint32_t counter = target_clk_divider + 1; counter > 0; --counter)
		continue;

	const bool bit = gpio_get(SWDIO_IN_PORT, SWDIO_IN_PIN);

	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for (volatile uint32_t counter = target_clk_divider + 1; counter > 0; --counter)
		continue;

	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	/* Terminate the read cycle now */
	swdptap_turnaround(SWDIO_STATUS_DRIVE);

	const bool parity = calculate_odd_parity(result);
	*ret = result;
	return parity == bit;
}

static void swdptap_seq_out_clk_delay(uint32_t tms_states, size_t clock_cycles) __attribute__((optimize(3)));

static void swdptap_seq_out_clk_delay(const uint32_t tms_states, const size_t clock_cycles)
{
	uint32_t value = tms_states;
	bool bit = value & 1U;
	if (!clock_cycles)
		return;
	/*
	 * Count down instead of up, because with an up-count, some ARM-GCC
	 * versions use an explicit CMP, missing the optimization of converting
	 * to a faster down-count that uses SUBS followed by BCS/BCC.
	 */
	for (size_t cycle = clock_cycles; cycle--;) {
		/* Reordering barrier */
		__asm__("" ::: "memory");
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, bit);
		for (volatile uint32_t counter = target_clk_divider; counter > 0; --counter)
			continue;
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		for (volatile uint32_t counter = target_clk_divider; counter > 0; --counter)
			continue;
		__asm__("nop" ::: "memory");
		value >>= 1U;
		bit = value & 1U;
		/* Reordering barrier */
		__asm__("" ::: "memory");
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
}

static void swdptap_seq_out_no_delay(uint32_t tms_states, size_t clock_cycles) __attribute__((optimize(3)));

static void swdptap_seq_out_no_delay(const uint32_t tms_states, const size_t clock_cycles)
{
	uint32_t value = tms_states;
	bool bit = value & 1U;
	if (!clock_cycles)
		return;
	/*
	 * Count down instead of up, because with an up-count, some ARM-GCC
	 * versions use an explicit CMP, missing the optimization of converting
	 * to a faster down-count that uses SUBS followed by BCS/BCC.
	 */
	for (size_t cycle = clock_cycles; cycle--;) {
		/* Reordering barrier */
		__asm__("" ::: "memory");
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, bit);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		__asm__("nop" ::: "memory");
		value >>= 1U;
		bit = value & 1U;
		/* Reordering barrier */
		__asm__("" ::: "memory");
	}
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
}

static void swdptap_seq_out(const uint32_t tms_states, const size_t clock_cycles)
{
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (target_clk_divider != UINT32_MAX)
		swdptap_seq_out_clk_delay(tms_states, clock_cycles);
	else
		swdptap_seq_out_no_delay(tms_states, clock_cycles);
}

static void swdptap_seq_out_parity(const uint32_t tms_states, const size_t clock_cycles)
{
	const bool parity = calculate_odd_parity(tms_states);
	swdptap_seq_out(tms_states, clock_cycles);
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, parity);
	for (volatile uint32_t counter = target_clk_divider + 1; counter > 0; --counter)
		continue;
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for (volatile uint32_t counter = target_clk_divider + 1; counter > 0; --counter)
		continue;
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
}
