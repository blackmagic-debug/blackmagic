/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file implements the RVSWD interface
 *
 * It's mostly the same routines as the SWD interface, with some RVSWD specifics
 */

#include "general.h"
#include "platform.h"
#include "timing.h"
#include "rvswd.h"
#include "maths_utils.h"

/**
 * RVSWD I/O is shared with SWD
 */
#define RVSWD_DIO_PORT SWDIO_PORT
#define RVSWD_DIO_PIN  SWDIO_PIN

#define RVSWD_CLK_PORT SWCLK_PORT
#define RVSWD_CLK_PIN  SWCLK_PIN

#define RVSWD_DIO_MODE_FLOAT SWDIO_MODE_FLOAT
#define RVSWD_DIO_MODE_DRIVE SWDIO_MODE_DRIVE

typedef enum rvswd_direction_t {
	RVSWD_DIRECTION_INPUT,
	RVSWD_DIRECTION_OUTPUT
} rvswd_direction_t;

rvswd_proc_s rvswd_proc;

static void rvswd_start(void) __attribute__((optimize(3)));
static void rvswd_stop(void) __attribute__((optimize(3)));

static uint32_t rvswd_seq_in(size_t clock_cycles) __attribute__((optimize(3)));
static uint32_t rvswd_seq_in_clk_delay(size_t clock_cycles) __attribute__((optimize(3)));
static uint32_t rvswd_seq_in_no_delay(size_t clock_cycles) __attribute__((optimize(3)));

static void rvswd_seq_out(uint32_t dio_states, size_t clock_cycles) __attribute__((optimize(3)));
static void rvswd_seq_out_clk_delay(uint32_t dio_states, size_t clock_cycles) __attribute__((optimize(3)));
static void rvswd_seq_out_no_delay(uint32_t dio_states, size_t clock_cycles) __attribute__((optimize(3)));

static inline void __attribute__((always_inline)) rvswd_hold_period(void)
{
	/* Hold for a period */
	for (volatile uint32_t counter = target_clk_divider + 1U; counter > 0U; --counter)
		continue;
}

void rvswd_init(void)
{
	rvswd_proc.start = rvswd_start;
	rvswd_proc.stop = rvswd_stop;
	rvswd_proc.seq_in = rvswd_seq_in;
	rvswd_proc.seq_out = rvswd_seq_out;
}

static void rvswd_set_dio_direction(rvswd_direction_t direction)
{
	/* Do nothing if the direction is already set */
	/* FIXME: this internal state may become invalid if the IO is modified elsewhere (e.g. SWD) */
	static rvswd_direction_t current_direction = RVSWD_DIRECTION_INPUT;
	if (direction == current_direction)
		return;

	/* Change the direction */
	if (direction == RVSWD_DIRECTION_OUTPUT) {
		RVSWD_DIO_MODE_DRIVE();
	} else {
		RVSWD_DIO_MODE_FLOAT();
	}
	current_direction = direction;
}

static void rvswd_start(void)
{
	/*
	 * DIO falling edge while CLK is idle high marks a START condition
	 */

	/* Setup for the start sequence by setting the bus to the idle state */
	rvswd_set_dio_direction(RVSWD_DIRECTION_OUTPUT);
	gpio_set(RVSWD_DIO_PORT, RVSWD_DIO_PIN);
	gpio_set(RVSWD_CLK_PORT, RVSWD_CLK_PIN);

	/* Ensure the bus is idle for a period */
	rvswd_hold_period();

	/* Generate the start condition */
	gpio_clear(RVSWD_DIO_PORT, RVSWD_DIO_PIN);
	rvswd_hold_period();
}

static void rvswd_stop(void)
{
	/*
	 * DIO rising edge while CLK is idle high marks a STOP condition
	 */

	/* Setup for the stop condition by driving the CLK and DIO low */
	gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
	rvswd_set_dio_direction(RVSWD_DIRECTION_OUTPUT);
	gpio_clear(RVSWD_DIO_PORT, RVSWD_DIO_PIN);

	/* Ensure setup for a period */
	rvswd_hold_period();

	/* Generate the stop condition */
	gpio_set(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
	rvswd_hold_period();
	gpio_set(RVSWD_DIO_PORT, RVSWD_DIO_PIN);
}

static uint32_t rvswd_seq_in_clk_delay(const size_t clock_cycles)
{
	uint32_t value = 0; /* Return value */

	/* Shift clock_cycles bits in */
	for (size_t cycle = clock_cycles; cycle > 0; --cycle) {
		/* Drive the CLK low and hold for a period */
		gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
		rvswd_hold_period();

		/* Sample the DIO line and Raise the CLK, then hold for a period */
		value |= gpio_get(RVSWD_DIO_PORT, RVSWD_DIO_PIN) ? (1U << (cycle - 1U)) : 0U;
		gpio_set(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
		rvswd_hold_period();
	}

	// /* Leave the CLK low and return the value */
	// gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);

	/* Leave the CLK high and return the value */
	return value;
}

static uint32_t rvswd_seq_in_no_delay(const size_t clock_cycles)
{
	uint32_t value = 0U; /* Return value */

	/* Shift clock_cycles bits in */
	for (size_t cycle = clock_cycles; cycle > 0; --cycle) {
		/* Drive the CLK low */
		gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);

		/* Sample the DIO line and raise the CLK */
		value |= gpio_get(RVSWD_DIO_PORT, RVSWD_DIO_PIN) ? (1U << (cycle - 1U)) : 0U;
		gpio_set(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
	}

	// /* Leave the CLK low and return the value */
	// gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);

	/* Leave the CLK high and return the value */
	return value;
}

static uint32_t rvswd_seq_in(size_t clock_cycles)
{
	/* Set the DIO line to float to give control to the target */
	rvswd_set_dio_direction(RVSWD_DIRECTION_INPUT);

	/* Delegate to the appropriate sequence in routine depending on the clock divider */
	if (target_clk_divider != UINT32_MAX)
		return rvswd_seq_in_clk_delay(clock_cycles);
	else
		return rvswd_seq_in_no_delay(clock_cycles);
}

static void rvswd_seq_out_clk_delay(const uint32_t dio_states, const size_t clock_cycles)
{
	/* Shift clock_cycles bits out */
	for (size_t cycle = clock_cycles; cycle > 0; --cycle) {
		/* Drive the CLK low and setup the DIO line, then hold for a period */
		gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, dio_states & (1U << (cycle - 1U)));
		rvswd_hold_period();

		/* Raise the CLK and hold for a period */
		gpio_set(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
		rvswd_hold_period();
	}

	// /* Leave the CLK low */
	// gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);

	/* Leave the CLK high and return */
}

static void rvswd_seq_out_no_delay(const uint32_t dio_states, const size_t clock_cycles)
{
	/* Shift clock_cycles bits out */
	for (size_t cycle = clock_cycles; cycle > 0; --cycle) {
		/* Drive the CLK low and setup the DIO line */
		gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, dio_states & (1U << (cycle - 1U)));

		/* Raise the CLK */
		gpio_set(RVSWD_CLK_PORT, RVSWD_CLK_PIN);
	}

	// /* Leave the CLK low */
	// gpio_clear(RVSWD_CLK_PORT, RVSWD_CLK_PIN);

	/* Leave the CLK high and return */
}

static void rvswd_seq_out(const uint32_t dio_states, const size_t clock_cycles)
{
	/* Set the DIO line to drive to give us control */
	rvswd_set_dio_direction(RVSWD_DIRECTION_OUTPUT);

	/* Delegate to the appropriate sequence in routine depending on the clock divider */
	if (target_clk_divider != UINT32_MAX)
		rvswd_seq_out_clk_delay(dio_states, clock_cycles);
	else
		rvswd_seq_out_no_delay(dio_states, clock_cycles);
}
