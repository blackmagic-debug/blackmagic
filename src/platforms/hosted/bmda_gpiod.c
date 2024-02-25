/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by OmniTechnoMancer <OmniTechnoMancer@wah.quest>
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

/* Implement libgpiod based GPIO backend */

#include <gpiod.h>
#include "general.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

#include "bmda_gpiod.h"

struct gpiod_line *bmda_gpiod_tck_pin;
struct gpiod_line *bmda_gpiod_tms_pin;
struct gpiod_line *bmda_gpiod_tdi_pin;
struct gpiod_line *bmda_gpiod_tdo_pin;

struct gpiod_line *bmda_gpiod_swdio_pin;
struct gpiod_line *bmda_gpiod_swclk_pin;

uint32_t target_clk_divider = UINT32_MAX;

static void bmda_gpiod_debug_pin(struct gpiod_line *line, const char *op, bool print, bool val)
{
#ifdef DEBUG
	DEBUG_WIRE("GPIO %s %s", gpiod_line_consumer(line), op);
	if (print)
		DEBUG_WIRE("=%d", val);
	DEBUG_WIRE("\n");
#else
	(void)line;
	(void)op;
	(void)print;
	(void)val;
#endif
}

void bmda_gpiod_set_pin(struct gpiod_line *pin, bool val)
{
	if (pin) {
		bmda_gpiod_debug_pin(pin, "set", true, val);
		if (gpiod_line_set_value(pin, val ? 1 : 0)) {
			DEBUG_ERROR("Failed to set pin to value %d errno: %d", val, errno);
			exit(1);
		}
	} else
		DEBUG_ERROR("BUG! attempt to write uninit GPIO");
}

bool bmda_gpiod_get_pin(struct gpiod_line *pin)
{
	if (pin) {
		int ret = gpiod_line_get_value(pin);
		if (ret < 0) {
			DEBUG_ERROR("Failed to get pin value errno: %d", errno);
			exit(1);
		}
		bmda_gpiod_debug_pin(pin, "read", true, ret);
		return ret;
	} else {
		DEBUG_ERROR("BUG! attempt to read uninit GPIO");
		exit(1);
	}
}

void bmda_gpiod_mode_input(struct gpiod_line *pin)
{
	if (pin) {
		bmda_gpiod_debug_pin(pin, "input", false, false);
		if (gpiod_line_set_direction_input(pin)) {
			DEBUG_ERROR("Failed to set pin to input errno: %d", errno);
			exit(1);
		}
	} else
		DEBUG_ERROR("BUG! attempt to set uninit GPIO to input");
}

void bmda_gpiod_mode_output(struct gpiod_line *pin)
{
	if (pin) {
		bmda_gpiod_debug_pin(pin, "output", false, false);
		if (gpiod_line_set_direction_output(pin, 0)) {
			DEBUG_ERROR("Failed to set pin to output errno: %d", errno);
			exit(1);
		}
	} else
		DEBUG_ERROR("BUG! attempt to set uninit GPIO to output");
}
