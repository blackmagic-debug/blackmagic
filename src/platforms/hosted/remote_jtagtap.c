/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2008  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Dave Marples <dave@marples.net>
 * Modified (c) 2020 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
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

/* Low level JTAG implementation using FT2232 with libftdi.
 *
 * Issues:
 * Should share interface with swdptap.c or at least clean up...
 */

#include "general.h"
#include <unistd.h>

#include <assert.h>

#include "remote.h"
#include "jtagtap.h"
#include "bmp_hosted.h"
#include "bmp_remote.h"

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t tms_states, size_t ticks);
static void jtagtap_tdi_tdo_seq(uint8_t *data_out, bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static void jtagtap_tdi_seq(bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static bool jtagtap_next(bool tms, bool tdi);
static void jtagtap_cycle(bool tms, bool tdi, size_t clock_cycles);

static inline unsigned int bool_to_int(const bool value)
{
	return value ? 1 : 0;
}

int remote_jtagtap_init(jtag_proc_t *jtag_proc)
{
	platform_buffer_write((uint8_t *)REMOTE_JTAG_INIT_STR, sizeof(REMOTE_JTAG_INIT_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if ((!length) || (buffer[0] == REMOTE_RESP_ERR)) {
		DEBUG_WARN("jtagtap_init failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}

	jtag_proc->jtagtap_reset = jtagtap_reset;
	jtag_proc->jtagtap_next = jtagtap_next;
	jtag_proc->jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq = jtagtap_tdi_seq;

	platform_buffer_write((uint8_t *)REMOTE_HL_CHECK_STR, sizeof(REMOTE_HL_CHECK_STR));
	length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR || buffer[0] == 1)
		PRINT_INFO("Firmware does not support newer JTAG commands, please update it.");
	else
		jtag_proc->jtagtap_cycle = jtagtap_cycle;

	return 0;
}

/* See remote.c/.h for protocol information */

static void jtagtap_reset(void)
{
	platform_buffer_write((uint8_t *)REMOTE_JTAG_RESET_STR, sizeof(REMOTE_JTAG_RESET_STR));
	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("jtagtap_reset failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}

static void jtagtap_tms_seq(uint32_t tms_states, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_TMS_STR, clock_cycles, tms_states);
	platform_buffer_write((uint8_t *)buffer, length);

	length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("jtagtap_tms_seq failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}

/* At least up to v1.7.1-233, remote handles only up to 32 clock cycles in one
 * call. Break up large calls.
 *
 * FIXME: Provide and test faster call and keep fallback
 * for old firmware
 */
static void jtagtap_tdi_tdo_seq(
	uint8_t *const data_out, const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	if (!clock_cycles || (!data_in && !data_out))
		return;

	char buffer[REMOTE_MAX_MSG_SIZE];
	size_t in_offset = 0;
	size_t out_offset = 0;
	for (size_t cycle = 0; cycle < clock_cycles;) {
		size_t chunk;
		if (clock_cycles - cycle <= 64)
			chunk = clock_cycles - cycle;
		else
			chunk = 64;
		cycle += chunk;

		uint64_t data = 0;
		const size_t bytes = (chunk + 7U) >> 3U;
		if (data_in) {
			for (size_t i = 0; i < bytes; ++i)
				data |= data_in[in_offset++] << (i * 8U);
		}
		/* PRIx64 differs with system. Use it explicit in the format string*/
		int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "!J%c%02zx%" PRIx64 "%c",
			!clock_cycles && final_tms ? REMOTE_TDITDO_TMS : REMOTE_TDITDO_NOTMS, chunk, data, REMOTE_EOM);
		platform_buffer_write((uint8_t *)buffer, length);

		length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
		if (!length || buffer[0] == REMOTE_RESP_ERR) {
			DEBUG_WARN("jtagtap_tdi_tdo_seq failed, error %s\n", length ? buffer + 1 : "unknown");
			exit(-1);
		}
		if (data_out) {
			const uint64_t data = remotehston(-1, buffer + 1);
			for (size_t i = 0; i < bytes; ++i)
				data_out[out_offset++] = (uint8_t)(data >> (i * 8U));
		}
	}
}

static void jtagtap_tdi_seq(const bool final_tms, const uint8_t *data_in, size_t clock_cycles)
{
	return jtagtap_tdi_tdo_seq(NULL, final_tms, data_in, clock_cycles);
}

static bool jtagtap_next(const bool tms, const bool tdi)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf((char *)buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_NEXT, bool_to_int(tms), bool_to_int(tdi));
	platform_buffer_write((uint8_t *)buffer, length);

	length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("jtagtap_next failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}

	return remotehston(-1, buffer + 1);
}

static void jtagtap_cycle(const bool tms, const bool tdi, const size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length =
		snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_CYCLE_STR, bool_to_int(tms), bool_to_int(tdi), clock_cycles);
	platform_buffer_write((uint8_t *)buffer, length);

	length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("jtagtap_cycle failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}
