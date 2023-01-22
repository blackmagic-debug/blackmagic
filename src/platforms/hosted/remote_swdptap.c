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

static bool swdptap_seq_in_parity(uint32_t *res, size_t clock_cycles);
static uint32_t swdptap_seq_in(size_t clock_cycles);
static void swdptap_seq_out(uint32_t tms_states, size_t clock_cycles);
static void swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles);

bool remote_swdptap_init(void)
{
	DEBUG_WIRE("remote_swdptap_init\n");
	platform_buffer_write((uint8_t *)REMOTE_SWDP_INIT_STR, sizeof(REMOTE_SWDP_INIT_STR));

	char construct[REMOTE_MAX_MSG_SIZE];
	int length = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (!length || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("swdptap_init failed, error %s\n", length ? construct + 1 : "unknown");
		exit(-1);
	}

	swd_proc.seq_in = swdptap_seq_in;
	swd_proc.seq_in_parity = swdptap_seq_in_parity;
	swd_proc.seq_out = swdptap_seq_out;
	swd_proc.seq_out_parity = swdptap_seq_out_parity;
	return true;
}

static bool swdptap_seq_in_parity(uint32_t *res, size_t clock_cycles)
{
	char construct[REMOTE_MAX_MSG_SIZE];

	int length = sprintf(construct, REMOTE_SWDP_IN_PAR_STR, clock_cycles);
	platform_buffer_write((uint8_t *)construct, length);

	length = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (length < 2 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("swdptap_seq_in_parity failed, error %s\n", length ? &(construct[1]) : "short response");
		exit(-1);
	}

	*res = remotehston(-1, &construct[1]);
	DEBUG_PROBE("swdptap_seq_in_parity  %2d clock_cycles: %08" PRIx32 " %s\n", clock_cycles, *res,
		construct[0] != REMOTE_RESP_OK ? "ERR" : "OK");
	return construct[0] != REMOTE_RESP_OK;
}

static uint32_t swdptap_seq_in(size_t clock_cycles)
{
	char construct[REMOTE_MAX_MSG_SIZE];

	int length = sprintf(construct, REMOTE_SWDP_IN_STR, clock_cycles);
	platform_buffer_write((uint8_t *)construct, length);

	length = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (length < 2 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("swdptap_seq_in failed, error %s\n", length ? construct + 1 : "short response");
		exit(-1);
	}
	uint32_t res = remotehston(-1, &construct[1]);
	DEBUG_PROBE("swdptap_seq_in         %2d clock_cycles: %08" PRIx32 "\n", clock_cycles, res);
	return res;
}

static void swdptap_seq_out(uint32_t tms_states, size_t clock_cycles)
{
	char construct[REMOTE_MAX_MSG_SIZE];

	DEBUG_PROBE("swdptap_seq_out        %2d clock_cycles: %08" PRIx32 "\n", clock_cycles, tms_states);
	int length = sprintf(construct, REMOTE_SWDP_OUT_STR, clock_cycles, tms_states);
	platform_buffer_write((uint8_t *)construct, length);

	length = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("swdptap_seq_out failed, error %s\n", length ? construct + 1 : "short response");
		exit(-1);
	}
}

static void swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles)
{
	char construct[REMOTE_MAX_MSG_SIZE];

	DEBUG_PROBE("swdptap_seq_out_parity %2d clock_cycles: %08" PRIx32 "\n", clock_cycles, tms_states);
	int length = sprintf(construct, REMOTE_SWDP_OUT_PAR_STR, clock_cycles, tms_states);
	platform_buffer_write((uint8_t *)construct, length);

	length = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || construct[1] == REMOTE_RESP_ERR) {
		DEBUG_WARN("swdptap_seq_out_parity failed, error %s\n", length ? construct + 2 : "short response");
		exit(-1);
	}
}
