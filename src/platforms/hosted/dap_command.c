/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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

#include <string.h>
#include "dap.h"
#include "dap_command.h"

bool perform_dap_swj_sequence(size_t clock_cycles, const uint8_t *data)
{
	/* Validate that clock_cycles is in range for the request (spec limits it to 256) */
	if (clock_cycles > 256)
		return false;

	DEBUG_PROBE("-> dap_swj_sequence (%zu cycles)\n", clock_cycles);
	/* Construct the request buffer */
	uint8_t request[34] = {
		DAP_SWJ_SEQUENCE,
		(uint8_t)clock_cycles,
	};
	/* Calculate the number of bytes needed to represent the requeted number of clock cycles */
	const size_t bytes = (clock_cycles + 7U) >> 3U;
	/* And copy the data into the buffer */
	memcpy(request + 2, data, bytes);

	/* Sequence response is a single byte */
	uint8_t response = DAP_RESPONSE_OK;
	/* Run the request */
	if (!dap_run_cmd(request, 2U + bytes, &response, 1U))
		return false;
	/* And check that it succeeded */
	return response == DAP_RESPONSE_OK;
}
