/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Additions by Dave Marples <dave@marples.net>
 * Modifications (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "gdb_if.h"
#include "version.h"
#include "remote.h"
#include "target.h"
#include "bmp_remote.h"
#include "cli.h"
#include "hex_utils.h"
#include "exception.h"

#include "remote/protocol_v0.h"
#include "remote/protocol_v1.h"
#include "remote/protocol_v2.h"
#include "remote/protocol_v3.h"

#include <assert.h>
#include <sys/time.h>
#include <errno.h>

#include "adiv5.h"

bmp_remote_protocol_s remote_funcs;

uint64_t remote_decode_response(const char *const response, const size_t digits)
{
	uint64_t value = 0U;
	for (size_t idx = 0U; idx < digits; ++idx) {
		value <<= 4U;
		value |= unhex_digit(response[idx]);
	}
	return value;
}

bool remote_init(const bool power_up)
{
	/* When starting remote communications, start by asking the firmware to initialise remote mode */
	platform_buffer_write(REMOTE_START_STR, sizeof(REMOTE_START_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	ssize_t length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	/* Check if the launch failed for any reason */
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("Remote Start failed, error %s\n", length ? buffer + 1 : "unknown");
		return false;
	}
	/* If it did not, we now have the firmware version string so log it */
	DEBUG_PROBE("Remote is %s\n", buffer + 1);

	/*
	 * Next, ask the probe for its protocol version number.
	 * This is unfortunately part of the "high level" protocol component, but it's a general request.
	 */
	platform_buffer_write(REMOTE_HL_CHECK_STR, sizeof(REMOTE_HL_CHECK_STR));
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	/* Check for communication failures */
	if (length < 1) {
		DEBUG_ERROR("%s comms error: %zd\n", __func__, length);
		return false;
	}
	/* If the request failed by way of a not implemented response, we're on a v0 protocol probe */
	if (buffer[0] != REMOTE_RESP_OK)
		remote_v0_init();
	else {
		/* If the probe's indicated that the request succeeded, convert the version number */
		const uint64_t version = remote_decode_response(buffer + 1, length - 1);
		switch (version) {
		case 0:
			/* protocol version number 0 coresponds to an enhanced v0 protocol probe ("v0+") */
			remote_v0_plus_init();
			break;
		case 1:
			remote_v1_init();
			break;
		case 2:
			remote_v2_init();
			break;
		case 3:
			remote_v3_init();
			break;
		default:
			DEBUG_ERROR("Unknown remote protocol version %" PRIu64 ", aborting\n", version);
			return false;
		}
	}

	/* Finally, power the target up having selected remote routines to use */
	remote_target_set_power(power_up);
	return true;
}

bool remote_target_get_power(void)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_PWR_GET_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("platform_target_get_power failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
	return buffer[1] == '1';
}

bool remote_target_set_power(const bool power)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_PWR_SET_STR, power ? '1' : '0');
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_ERROR("platform_target_set_power failed, error %s\n", length ? buffer + 1 : "unknown");
	return length > 0 && buffer[0] == REMOTE_RESP_OK;
}

void remote_nrst_set_val(bool assert)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_NRST_SET_STR, assert ? '1' : '0');
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("platform_nrst_set_val failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}

bool remote_nrst_get_val(void)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_NRST_GET_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("platform_nrst_set_val failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
	return buffer[1] == '1';
}

void remote_max_frequency_set(uint32_t freq)
{
	if (remote_funcs.set_comms_frequency)
		remote_funcs.set_comms_frequency(freq);
	else
		DEBUG_WARN("Please update probe firmware to enable SWD/JTAG frequency selection\n");
}

uint32_t remote_max_frequency_get(void)
{
	if (remote_funcs.get_comms_frequency)
		return remote_funcs.get_comms_frequency();
	return FREQ_FIXED;
}

const char *remote_target_voltage(void)
{
	static char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, " %s", REMOTE_VOLTAGE_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("platform_target_voltage failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
	return buffer + 1;
}

void remote_target_clk_output_enable(const bool enable)
{
	if (remote_funcs.target_clk_output_enable)
		remote_funcs.target_clk_output_enable(enable);
	else
		DEBUG_WARN("Please update probe firmware to enable high impedance clock feature\n");
}

bool remote_jtag_init(void)
{
	return remote_funcs.jtag_init();
}

bool remote_swd_init(void)
{
	return remote_funcs.swd_init();
}

void remote_adiv5_dp_init(adiv5_debug_port_s *const dp)
{
	remote_funcs.adiv5_init(dp);
}

void remote_add_jtag_dev(uint32_t dev_index, const jtag_dev_s *jtag_dev)
{
	if (remote_funcs.add_jtag_dev)
		remote_funcs.add_jtag_dev(dev_index, jtag_dev);
}
