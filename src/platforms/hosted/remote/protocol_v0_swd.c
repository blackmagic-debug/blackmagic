/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#include "bmp_remote.h"
#include "protocol_v0_defs.h"
#include "protocol_v0_swd.h"

uint32_t remote_v0_swd_seq_in(size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	int length = sprintf(buffer, REMOTE_SWD_IN_STR, clock_cycles);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 2 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("%s failed, error %s\n", __func__, length ? buffer + 1 : "short response");
		exit(-1);
	}
	const uint32_t result = remote_hex_string_to_num(-1, buffer + 1);
	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, result);
	return result;
}

bool remote_v0_swd_seq_in_parity(uint32_t *result, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	int length = sprintf(buffer, REMOTE_SWD_IN_PAR_STR, clock_cycles);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 2 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("%s failed, error %s\n", __func__, length ? buffer + 1 : "short response");
		exit(-1);
	}

	*result = remote_hex_string_to_num(-1, buffer + 1);
	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 " %s\n", __func__, clock_cycles, *result,
		buffer[0] != REMOTE_RESP_OK ? "ERR" : "OK");
	return buffer[0] != REMOTE_RESP_OK;
}

void remote_v0_swd_seq_out(uint32_t value, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, value);
	int length = sprintf(buffer, REMOTE_SWD_OUT_STR, clock_cycles, value);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("%s failed, error %s\n", __func__, length ? buffer + 1 : "short response");
		exit(-1);
	}
}

void remote_v0_swd_seq_out_parity(uint32_t value, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];

	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, value);
	int length = sprintf(buffer, REMOTE_SWD_OUT_PAR_STR, clock_cycles, value);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[1] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("%s failed, error %s\n", __func__, length ? buffer + 2 : "short response");
		exit(-1);
	}
}
