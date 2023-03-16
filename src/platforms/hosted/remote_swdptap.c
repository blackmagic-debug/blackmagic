/*
 * This file is part of the Black Magic Debug project.
 *
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Modified by Dave Marples <dave@marples.net>
 * Modified 2020 - 2021 by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* MPSSE bit-banging SW-DP interface over FTDI with loop unrolled.
 * Speed is sensible.
 */

#include "general.h"
#include <stdio.h>
#include <assert.h>

#include "remote.h"
#include "jtagtap.h"
#include "bmp_remote.h"

static bool remote_swd_seq_in_parity(uint32_t *res, size_t clock_cycles);
static uint32_t remote_swd_seq_in(size_t clock_cycles);
static void remote_swd_seq_out(uint32_t tms_states, size_t clock_cycles);
static void remote_swd_seq_out_parity(uint32_t tms_states, size_t clock_cycles);

bool remote_swdptap_init(void)
{
	DEBUG_PROBE("remote_swdptap_init\n");
	platform_buffer_write(REMOTE_SWDP_INIT_STR, sizeof(REMOTE_SWDP_INIT_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("swdptap_init failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}

	swd_proc.seq_in = remote_swd_seq_in;
	swd_proc.seq_in_parity = remote_swd_seq_in_parity;
	swd_proc.seq_out = remote_swd_seq_out;
	swd_proc.seq_out_parity = remote_swd_seq_out_parity;
	return true;
}

static bool remote_swd_seq_in_parity(uint32_t *res, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	int length = sprintf(buffer, REMOTE_SWDP_IN_PAR_STR, clock_cycles);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 2 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("%s failed, error %s\n", __func__, length ? buffer + 1 : "short response");
		exit(-1);
	}

	*res = remote_hex_string_to_num(-1, buffer + 1);
	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 " %s\n", __func__, clock_cycles, *res,
		buffer[0] != REMOTE_RESP_OK ? "ERR" : "OK");
	return buffer[0] != REMOTE_RESP_OK;
}

static uint32_t remote_swd_seq_in(size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	int length = sprintf(buffer, REMOTE_SWDP_IN_STR, clock_cycles);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 2 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("%s failed, error %s\n", __func__, length ? buffer + 1 : "short response");
		exit(-1);
	}
	uint32_t res = remote_hex_string_to_num(-1, buffer + 1);
	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, res);
	return res;
}

static void remote_swd_seq_out(uint32_t tms_states, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, tms_states);
	int length = sprintf(buffer, REMOTE_SWDP_OUT_STR, clock_cycles, tms_states);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("%s failed, error %s\n", __func__, length ? buffer + 1 : "short response");
		exit(-1);
	}
}

static void remote_swd_seq_out_parity(uint32_t tms_states, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, tms_states);
	int length = sprintf(buffer, REMOTE_SWDP_OUT_PAR_STR, clock_cycles, tms_states);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[1] == REMOTE_RESP_ERR) {
		DEBUG_WARN("%s failed, error %s\n", __func__, length ? buffer + 2 : "short response");
		exit(-1);
	}
}
