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
#include "protocol_v0_jtag.h"

static inline uint8_t bool_to_int(const bool value)
{
	return value ? 1U : 0U;
}

void remote_v0_jtag_reset(void)
{
	platform_buffer_write(REMOTE_JTAG_RESET_STR, sizeof(REMOTE_JTAG_RESET_STR));
	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("remote_jtag_reset failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}

void remote_v0_jtag_tms_seq(uint32_t tms_states, size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_TMS_STR, clock_cycles, tms_states);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("remote_jtag_tms_seq failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}

void remote_v0_jtag_tdi_tdo_seq(uint8_t *data_out, bool final_tms, const uint8_t *data_in, size_t clock_cycles)
{
	/* NB: Until firmware version v1.7.1-233, the remote can only handle 32 clock cycles at a time */
	if (!clock_cycles || (!data_in && !data_out))
		return;

	char buffer[REMOTE_MAX_MSG_SIZE];
	size_t offset = 0;
	/* Loop through the data to send/receive and handle it in chunks of up to 32 bits */
	for (size_t cycle = 0; cycle < clock_cycles; cycle += 32U) {
		/* Calculate how many bits need to be in this chunk, capped at 32 */
		const size_t chunk_length = MIN(clock_cycles - cycle, 32U);
		/* If the result would complete the transaction, check if TMS needs to be high at the end */
		const char packet_type =
			cycle + chunk_length == clock_cycles && final_tms ? REMOTE_TDITDO_TMS : REMOTE_TDITDO_NOTMS;

		/* Build a representation of the data to send safely */
		uint32_t packet_data_in = 0U;
		const size_t bytes = (chunk_length + 7U) >> 3U;
		if (data_in) {
			for (size_t idx = 0; idx < bytes; ++idx)
				packet_data_in |= data_in[offset + idx] << (idx * 8U);
		}
		/*
		 * Build the remote protocol message to send, and send it.
		 * This uses its own copy of the REMOTE_JTAG_TDIDO_STR to correct for how
		 * formatting a uint32_t is platform-specific.
		 */
		int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "!J%c%02zx%" PRIx32 "%c", packet_type, chunk_length,
			packet_data_in, REMOTE_EOM);
		platform_buffer_write(buffer, length);

		/* Receive the response and check if it's an error response */
		length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (!length || buffer[0] == REMOTE_RESP_ERR) {
			DEBUG_ERROR("remote_jtag_tdi_tdo_seq failed, error %s\n", length ? buffer + 1 : "unknown");
			exit(-1);
		}
		if (data_out) {
			const uint64_t packet_data_out = remote_hex_string_to_num(-1, buffer + 1);
			for (size_t idx = 0; idx < bytes; ++idx)
				data_out[offset + idx] = (uint8_t)(packet_data_out >> (idx * 8U));
		}
		offset += bytes;
	}
}

void remote_v0_jtag_tdi_seq(bool final_tms, const uint8_t *data_in, size_t clock_cycles)
{
	remote_v0_jtag_tdi_tdo_seq(NULL, final_tms, data_in, clock_cycles);
}

bool remote_v0_jtag_next(bool tms, bool tdi)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_NEXT, bool_to_int(tms), bool_to_int(tdi));
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("jtagtap_next failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}

	return remote_hex_string_to_num(1, buffer + 1);
}

void remote_v0_jtag_cycle(const bool tms, const bool tdi, const size_t clock_cycles)
{
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle)
		remote_v0_jtag_next(tms, tdi);
}
