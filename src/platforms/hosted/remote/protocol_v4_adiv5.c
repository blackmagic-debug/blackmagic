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
#include "protocol_v3_adiv5.h"
#include "protocol_v4_defs.h"
#include "protocol_v4_adiv5.h"
#include "hex_utils.h"
#include "exception.h"

static bool remote_v4_have_dp_version_command = true;
static uint8_t remote_v4_current_dp_version = UINT8_MAX;

static void remote_v4_adiv5_dp_version(adiv5_debug_port_s *const dp)
{
	/*
	 * Check if the probe actually has this command, skip if it does not.
	 * Likewise check if the DP version has changed since last call.
	 */
	if (!remote_v4_have_dp_version_command || remote_v4_current_dp_version == dp->version)
		return;
	/* Create the request and send it to the remote */
	char buffer[REMOTE_MAX_MSG_SIZE];
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_DP_VERSION_STR, dp->version);
	platform_buffer_write(buffer, length);
	/* Now read back the answer and note any errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1)
		DEBUG_ERROR("%s comms error: %zd\n", __func__, length);
	else if (buffer[0] != REMOTE_RESP_OK) {
		DEBUG_WARN("Please upgrade your firmware to allow ADIv6 devices to work properly\n");
		remote_v4_have_dp_version_command = false;
	}
}

uint32_t remote_v4_adiv5_raw_access(
	adiv5_debug_port_s *const dp, const uint8_t rnw, const uint16_t addr, const uint32_t request_value)
{
	remote_v4_adiv5_dp_version(dp);
	return remote_v3_adiv5_raw_access(dp, rnw, addr, request_value);
}

uint32_t remote_v4_adiv5_dp_read(adiv5_debug_port_s *const dp, const uint16_t addr)
{
	remote_v4_adiv5_dp_version(dp);
	return remote_v3_adiv5_dp_read(dp, addr);
}

uint32_t remote_v4_adiv5_ap_read(adiv5_access_port_s *const ap, const uint16_t addr)
{
	remote_v4_adiv5_dp_version(ap->dp);
	return remote_v3_adiv5_ap_read(ap, addr);
}

void remote_v4_adiv5_ap_write(adiv5_access_port_s *const ap, const uint16_t addr, const uint32_t value)
{
	remote_v4_adiv5_dp_version(ap->dp);
	remote_v3_adiv5_ap_write(ap, addr, value);
}

void remote_v4_adiv5_mem_read_bytes(
	adiv5_access_port_s *const ap, void *const dest, const target_addr64_t src, const size_t read_length)
{
	/* Check if we have anything to do */
	if (!read_length)
		return;
	remote_v4_adiv5_dp_version(ap->dp);
	char *const data = (char *)dest;
	DEBUG_PROBE("%s: @%08" PRIx64 "+%zx\n", __func__, src, read_length);
	char buffer[REMOTE_MAX_MSG_SIZE];
	/*
	 * As we do, calculate how large a transfer we can do from the firmware.
	 * there are 2 leader bytes around responses, 1 trailer, and the data is hex-encoded taking 2 bytes a byte
	 */
	const size_t blocksize = (REMOTE_MAX_MSG_SIZE - REMOTE_ADIV5_MEM_READ_LENGTH) >> 1U;
	/* For each transfer block size, ask the firmware to read that block of bytes */
	for (size_t offset = 0; offset < read_length; offset += blocksize) {
		/* Pick the amount left to read or the block size, whichever is smaller */
		const size_t amount = MIN(read_length - offset, blocksize);
		/* Create the request and send it to the remote */
		ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIV5_MEM_READ_STR, ap->dp->dev_index, ap->apsel,
			ap->csw, src + offset, amount);
		platform_buffer_write(buffer, length);

		/* Read back the answer and check for errors */
		length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (!remote_v3_adiv5_check_error(__func__, ap->dp, buffer, length)) {
			DEBUG_ERROR("%s error around 0x%08zx\n", __func__, (size_t)src + offset);
			return;
		}
		/* If the response indicates all's OK, decode the data read */
		unhexify(data + offset, buffer + 1, amount);
	}
}

void remote_v4_adiv5_mem_write_bytes(adiv5_access_port_s *const ap, const target_addr64_t dest, const void *const src,
	const size_t write_length, const align_e align)
{
	/* Check if we have anything to do */
	if (!write_length)
		return;
	remote_v4_adiv5_dp_version(ap->dp);
	const char *data = (const char *)src;
	DEBUG_PROBE("%s: @%08" PRIx64 "+%zx alignment %u\n", __func__, dest, write_length, align);
	/* + 1 for terminating NUL character */
	char buffer[REMOTE_MAX_MSG_SIZE + 1U];
	/* As we do, calculate how large a transfer we can do to the firmware */
	const size_t alignment_mask = ~((1U << align) - 1U);
	/* NB: Hex encoding robs us of half the buffer space that would be available */
	const size_t blocksize = ((REMOTE_MAX_MSG_SIZE - REMOTE_ADIV5_MEM_WRITE_LENGTH) >> 1U) & alignment_mask;
	/* For each transfer block size, ask the firmware to write that block of bytes */
	for (size_t offset = 0; offset < write_length; offset += blocksize) {
		/* Pick the amount left to write or the block size, whichever is smaller */
		const size_t amount = MIN(write_length - offset, blocksize);
		/* Create the request and validate it ends up the right length */
		ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIV5_MEM_WRITE_STR, ap->dp->dev_index, ap->apsel,
			ap->csw, align, dest + offset, amount);
		assert(length == REMOTE_ADIV5_MEM_WRITE_LENGTH - 1U);
		/* Encode the data to send after the request block and append the packet termination marker */
		hexify(buffer + length, data + offset, amount);
		length += (ssize_t)(amount * 2U);
		buffer[length++] = REMOTE_EOM;
		buffer[length++] = '\0';
		platform_buffer_write(buffer, length);

		/* Read back the answer and check for errors */
		length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (!remote_v3_adiv5_check_error(__func__, ap->dp, buffer, length)) {
			DEBUG_ERROR("%s error around 0x%08zx\n", __func__, (size_t)dest + offset);
			return;
		}
	}
}
