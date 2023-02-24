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

#include "dap.h"
#include "dap_command.h"
#include "buffer_utils.h"

void dap_swdptap_seq_out(const uint32_t tms_states, const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles,
		DAP_SWD_OUT_SEQUENCE,
	};
	write_le4(sequence.data, 0, tms_states);
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U))
		DEBUG_WARN("dap_swdptap_seq_out failed\n");
}

void dap_swdptap_seq_out_parity(const uint32_t tms_states, const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles + 1,
		DAP_SWD_OUT_SEQUENCE,
	};
	write_le4(sequence.data, 0, tms_states);
	sequence.data[4] = __builtin_parity(tms_states);
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U))
		DEBUG_WARN("dap_swdptap_seq_out_parity failed\n");
}

uint32_t dap_swdptap_seq_in(const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles,
		DAP_SWD_IN_SEQUENCE,
	};
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U)) {
		DEBUG_WARN("dap_swdptap_seq_in failed\n");
		return 0U;
	}

	uint32_t result = 0;
	for (size_t offset = 0; offset < clock_cycles; offset += 8U)
		result |= sequence.data[offset >> 3U] << offset;
	return result;
}

bool dap_swdptap_seq_in_parity(uint32_t *const result, const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles + 1U,
		DAP_SWD_IN_SEQUENCE,
	};
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U)) {
		DEBUG_WARN("dap_swdptap_seq_in_parity failed\n");
		return false;
	}

	uint32_t data = 0;
	for (size_t offset = 0; offset < clock_cycles; offset += 8U)
		data |= sequence.data[offset >> 3U] << offset;
	*result = data;
	uint8_t parity = __builtin_parity(data) & 1U;
	parity ^= sequence.data[4] & 1U;
	return !parity;
}
