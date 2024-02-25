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

#define _POSIX_C_SOURCE 200809L

#include <gpiod.h>
#include "general.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

#include "bmda_gpiod.h"

struct gpiod_line *bmda_gpiod_tck_pin;
struct gpiod_line *bmda_gpiod_tms_pin;
struct gpiod_line *bmda_gpiod_tdi_pin;
struct gpiod_line *bmda_gpiod_tdo_pin;

bool bmda_gpiod_jtag_ok = false;

struct gpiod_line *bmda_gpiod_swdio_pin;
struct gpiod_line *bmda_gpiod_swclk_pin;

bool bmda_gpiod_swd_ok = false;

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

static bool bmda_gpiod_parse_gpio(const char *name, char *gpio)
{
	DEBUG_INFO("GPIO set %s: %s\n", name, gpio);
	char *offset = strstr(gpio, ":");
	if (!offset)
		return false;
	*offset = '\0';
	offset++;
	char *end = offset + strlen(offset);
	char *valid = NULL;

	unsigned long offset_val = strtoul(offset, &valid, 10);

	if (valid != end || offset_val > UINT_MAX)
		return false;

	/* This is an unsigned int because that is what gpiod_line_get consumes. */
	unsigned int gpio_off = (unsigned int)offset_val;

	DEBUG_INFO("gpiochip: %s offset: %u\n", gpio, gpio_off);
	struct gpiod_line *line = gpiod_line_get(gpio, gpio_off);
	if (!line) {
		DEBUG_ERROR("Couldn't get GPIO: %s:%u errno: %d\n", gpio, gpio_off, errno);
		return false;
	}

	if (!strcmp("tck", name)) {
		if (gpiod_line_request_output_flags(line, "bmda-tck", GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE, 0))
			goto out_fail_close;
		bmda_gpiod_tck_pin = line;
	} else if (!strcmp("tms", name)) {
		if (gpiod_line_request_output_flags(line, "bmda-tms", GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE, 0))
			goto out_fail_close;
		bmda_gpiod_tms_pin = line;
	} else if (!strcmp("tdi", name)) {
		if (gpiod_line_request_output_flags(line, "bmda-tdi", GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE, 0))
			goto out_fail_close;
		bmda_gpiod_tdi_pin = line;
	} else if (!strcmp("tdo", name)) {
		if (gpiod_line_request_input_flags(line, "bmda-tdo", GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE))
			goto out_fail_close;
		bmda_gpiod_tdo_pin = line;
	} else if (!strcmp("swdio", name)) {
		if (gpiod_line_request_input_flags(line, "bmda-swdio", GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE))
			goto out_fail_close;
		bmda_gpiod_swdio_pin = line;
	} else if (!strcmp("swclk", name)) {
		if (gpiod_line_request_output_flags(line, "bmda-swclk", GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE, 0))
			goto out_fail_close;
		bmda_gpiod_swclk_pin = line;
	} else {
		DEBUG_ERROR("Unrecognised signal name: %s\n", name);
		gpiod_chip_close(gpiod_line_get_chip(line));
		return false;
	}

	DEBUG_INFO("Line consumer: %s\n", gpiod_line_consumer(line));

	return true;

out_fail_close:
	DEBUG_ERROR("Requesting gpio failed errno: %d", errno);
	gpiod_chip_close(gpiod_line_get_chip(line));
	return false;
}

static bool bmda_gpiod_parse_gpiomap(const char *gpio_map)
{
	bool ret = true;

	char *gpio_map_copy = strdup(gpio_map);
	DEBUG_INFO("GPIO mapping: %s\n", gpio_map);

	char *saveptr = NULL;
	char *token = strtok_r(gpio_map_copy, ",", &saveptr);
	while (token) {
		DEBUG_INFO("GPIO: %s\n", token);
		char *val = strstr(token, "=");
		if (!val) {
			ret = false;
			break;
		}
		*val = '\0';
		val++;
		if (!bmda_gpiod_parse_gpio(token, val)) {
			ret = false;
			break;
		}

		token = strtok_r(NULL, ",", &saveptr);
	}

	free(gpio_map_copy);
	return ret;
}

bool bmda_gpiod_init(bmda_cli_options_s *const cl_opts)
{
	if (!cl_opts->opt_gpio_map)
		return false;

	if (!bmda_gpiod_parse_gpiomap(cl_opts->opt_gpio_map))
		return false;

	if (bmda_gpiod_swclk_pin && bmda_gpiod_swdio_pin)
		bmda_gpiod_swd_ok = true;

	if (bmda_gpiod_tck_pin && bmda_gpiod_tdi_pin && bmda_gpiod_tdo_pin && bmda_gpiod_tms_pin)
		bmda_gpiod_jtag_ok = true;

	return bmda_gpiod_jtag_ok || bmda_gpiod_swd_ok;
}

bool bmda_gpiod_jtag_init(void)
{
	if (!bmda_gpiod_jtag_ok)
		return false;

	jtagtap_init();

	return true;
}

bool bmda_gpiod_swd_init(void)
{
	if (!bmda_gpiod_swd_ok)
		return false;

	swdptap_init();

	return true;
}
