/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
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

#include <assert.h>
#include "bmp_remote.h"
#include "protocol_v4_defs.h"
#include "protocol_v4_riscv.h"
#include "hex_utils.h"
#include "exception.h"

bool remote_v4_riscv_check_error(
	const char *const func, riscv_dmi_s *const dmi, const char *const buffer, const ssize_t length)
{
	/* Check the response length for error codes */
	if (length < 1) {
		DEBUG_ERROR("%s comms error: %zd\n", func, length);
		return false;
	}
	/* Now check if the remote is returning an error */
	if (buffer[0U] == REMOTE_RESP_ERR) {
		const uint64_t response_code = remote_decode_response(buffer + 1, (size_t)length - 1U);
		const uint8_t error = response_code & 0xffU;
		/* If the error part of the response code indicates a fault, store the fault value */
		if (error == REMOTE_ERROR_FAULT)
			dmi->fault = response_code >> 8U;
		/* If the error part indicates an exception had occurred, make that happen here too */
		else if (error == REMOTE_ERROR_EXCEPTION)
			raise_exception(response_code >> 8U, "Remote protocol exception");
		/* Otherwise it's an unexpected error */
		else
			DEBUG_ERROR("%s: Unexpected error %u\n", func, error);
	} /* Check if the remote is reporting a parameter error*/
	else if (buffer[0U] == REMOTE_RESP_PARERR)
		DEBUG_ERROR("%s: !BUG! Firmware reported a parameter error\n", func);
	/* Check if the firmware is reporting some other kind of error */
	else if (buffer[0U] != REMOTE_RESP_OK)
		DEBUG_ERROR("%s: Firmware reported unexpected error: %c\n", func, buffer[0]);
	/* Return whether the remote indicated the request was successful */
	return buffer[0U] == REMOTE_RESP_OK;
}

bool remote_v4_riscv_jtag_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	/* Format the read request into a new buffer and send it to the probe */
	char buffer[REMOTE_MAX_MSG_SIZE];
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_RISCV_DMI_READ_STR, dmi->dev_index, dmi->idle_cycles,
		dmi->address_width, address);
	platform_buffer_write(buffer, length);

	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_v4_riscv_check_error(__func__, dmi, buffer, length))
		return false;

	/* Log the probe-level request and its response having decoded it from the buffer */
	unhexify(value, buffer + 1U, 4U);
	DEBUG_PROBE("%s: %08" PRIx32 " -> %08" PRIx32 "\n", __func__, address, *value);
	return true;
}

bool remote_v4_riscv_jtag_dmi_write(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	/* Format the write request into a new buffer and send it to the probe */
	char buffer[REMOTE_MAX_MSG_SIZE];
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_RISCV_DMI_WRITE_STR, dmi->dev_index, dmi->idle_cycles,
		dmi->address_width, address, value);
	platform_buffer_write(buffer, length);

	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_v4_riscv_check_error(__func__, dmi, buffer, length))
		return false;
	/* Log the probe-level request now it's succeeded */
	DEBUG_PROBE("%s: %08" PRIx32 " <- %08" PRIx32 "\n", __func__, address, value);
	return true;
}
