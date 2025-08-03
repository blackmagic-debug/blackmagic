/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
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
#include "protocol_v3_defs.h"
#include "protocol_v3_spi.h"
#include "hex_utils.h"
#include "exception.h"

static const char *remote_v3_fault_to_string(const char *buffer, const ssize_t length)
{
	if (length < 1)
		return "communications failure";
	switch (buffer[0]) {
	case REMOTE_RESP_ERR:
		if (length != 2)
			return "truncated error packet";
		if (buffer[1] == REMOTE_ERROR_FAULT)
			return "fault occured on probe";
		return "unknown error occured";
	case REMOTE_RESP_NOTSUP:
		return "not supported";
	case REMOTE_RESP_PARERR:
		return "parameter error in request";
	default:
		break;
	}
	return "[BUG] impossible fault state";
}

bool remote_v3_spi_init(const spi_bus_e bus)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_SPI_BEGIN_STR, bus);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] != REMOTE_RESP_OK) {
		DEBUG_ERROR("Remote SPI initialisation failed, %s", remote_v3_fault_to_string(buffer, length));
		return false;
	}
	DEBUG_PROBE("%s: bus %u\n", __func__, bus);
	return true;
}

bool remote_v3_spi_deinit(const spi_bus_e bus)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_SPI_END_STR, bus);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] != REMOTE_RESP_OK) {
		DEBUG_ERROR("Remote SPI deinitialisation failed, %s", remote_v3_fault_to_string(buffer, length));
		return false;
	}
	DEBUG_PROBE("%s: bus %u\n", __func__, bus);
	return true;
}

bool remote_v3_spi_chip_select(uint8_t device_select)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_SPI_CHIP_SELECT_STR, device_select);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] != REMOTE_RESP_OK) {
		DEBUG_ERROR("Remote SPI chip select failed, %s", remote_v3_fault_to_string(buffer, length));
		return false;
	}
	DEBUG_PROBE("%s: %02x\n", __func__, device_select);
	return true;
}

uint8_t remote_v3_spi_xfer(spi_bus_e bus, uint8_t value)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_SPI_TRANSFER_STR, bus, value);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] != REMOTE_RESP_OK) {
		DEBUG_ERROR("Remote SPI transfer failed, %s", remote_v3_fault_to_string(buffer, length));
		return UINT8_MAX;
	}
	const uint8_t result_value = (uint8_t)remote_decode_response(buffer + 1U, 2U);
	DEBUG_PROBE("%s: bus %u => %02x -> %02x\n", __func__, bus, value, result_value);
	return result_value;
}
