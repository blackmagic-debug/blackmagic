/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Additions by Dave Marples <dave@marples.net>
 * Modifications (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "general.h"
#include "gdb_if.h"
#include "version.h"
#include "remote.h"
#include "target.h"
#include "bmp_remote.h"
#include "cli.h"
#include "hex_utils.h"
#include "exception.h"

#include <assert.h>
#include <sys/time.h>
#include <errno.h>

#include "adiv5.h"

int remote_init(const bool power_up)
{
	platform_buffer_write(REMOTE_START_STR, sizeof(REMOTE_START_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("Remote Start failed, error %s\n", length ? buffer + 1 : "unknown");
		return -1;
	}
	DEBUG_PROBE("Remote is %s\n", buffer + 1);
	remote_target_set_power(power_up);
	return 0;
}

bool remote_target_get_power(void)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_PWR_GET_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_target_get_power failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
	return buffer[1] == '1';
}

bool remote_target_set_power(const bool power)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_PWR_SET_STR, power ? '1' : '0');
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("platform_target_set_power failed, error %s\n", length ? buffer + 1 : "unknown");
	return length > 0 && buffer[0] == REMOTE_RESP_OK;
}

void remote_nrst_set_val(bool assert)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_NRST_SET_STR, assert ? '1' : '0');
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_nrst_set_val failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}

bool remote_nrst_get_val(void)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_NRST_GET_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_nrst_set_val failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
	return buffer[1] == '1';
}

void remote_max_frequency_set(uint32_t freq)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_FREQ_SET_STR, freq);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("Update Firmware to allow to set max SWJ frequency\n");
}

uint32_t remote_max_frequency_get(void)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_FREQ_GET_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		return FREQ_FIXED;
	uint32_t freq;
	unhexify(&freq, buffer + 1, 4);
	return freq;
}

const char *remote_target_voltage(void)
{
	static char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, " %s", REMOTE_VOLTAGE_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_target_voltage failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
	return buffer + 1;
}

void remote_target_clk_output_enable(const bool enable)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_TARGET_CLK_OE_STR, enable ? '1' : '0');
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("remote_target_clk_output_enable failed, error %s\n", length ? buffer + 1 : "unknown");
}

static uint64_t remote_decode_response(const char *const response, size_t digits)
{
	uint64_t value = 0U;
	for (size_t idx = 0U; idx < digits; ++idx) {
		value <<= 4U;
		value |= unhex_digit(response[idx]);
	}
	return value;
}

static bool remote_adiv5_check_error(
	const char *const func, adiv5_debug_port_s *const target_dp, const char *const buffer, const ssize_t length)
{
	/* Check the response length for error codes */
	if (length < 1) {
		DEBUG_WARN("%s comms error: %zd\n", func, length);
		return false;
	}
	/* Now check if the remote is reporting an error */
	if (buffer[0] == REMOTE_RESP_ERR) {
		const uint64_t response_code = remote_decode_response(buffer + 1, (size_t)length - 1U);
		const uint8_t error = response_code & 0xffU;
		/* If the error part of the response code indicates a fault, store the fault value */
		if (error == REMOTE_ERROR_FAULT)
			target_dp->fault = response_code >> 8U;
		/* If the error part indicates an exception had occured, make that happen here too */
		else if (error == REMOTE_ERROR_EXCEPTION)
			raise_exception(response_code >> 8U, "Remote protocol exception");
		/* Otherwise it's an unexpected error */
		else
			DEBUG_WARN("%s: Unexpected error %u\n", func, error);
	}
	/* Return whether the remote indicated the request was successfull */
	return buffer[0] == REMOTE_RESP_OK;
}

static uint32_t remote_adiv5_dp_read(adiv5_debug_port_s *const target_dp, const uint16_t addr)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_DP_READ_STR, target_dp->dev_index, addr);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_adiv5_check_error(__func__, target_dp, buffer, length))
		return 0U;
	/* If the response indicates all's OK, decode the data read and return it */
	uint32_t value = 0U;
	unhexify(&value, buffer + 1, 4);
	DEBUG_PROBE("%s: addr %04x -> %08" PRIx32 "\n", __func__, addr, value);
	return value;
}

static uint32_t remote_adiv5_raw_access(
	adiv5_debug_port_s *const target_dp, const uint8_t rnw, const uint16_t addr, const uint32_t request_value)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	int length = snprintf(
		buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIv5_RAW_ACCESS_STR, target_dp->dev_index, rnw, addr, request_value);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_adiv5_check_error(__func__, target_dp, buffer, length))
		return 0U;
	/* If the response indicates all's OK, decode the data read and return it */
	uint32_t result_value = 0U;
	unhexify(&result_value, buffer + 1, 4);
	DEBUG_PROBE("%s: addr %04x %s %08" PRIx32, __func__, addr, rnw ? "->" : "<-", rnw ? result_value : request_value);
	if (!rnw)
		DEBUG_PROBE(" -> %08" PRIx32, result_value);
	DEBUG_PROBE("\n");
	return result_value;
}

static uint32_t remote_adiv5_ap_read(adiv5_access_port_s *const target_ap, const uint16_t addr)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	int length =
		snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_AP_READ_STR, target_ap->dp->dev_index, target_ap->apsel, addr);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_adiv5_check_error(__func__, target_ap->dp, buffer, length))
		return 0U;
	/* If the response indicates all's OK, decode the data read and return it */
	uint32_t value = 0U;
	unhexify(&value, buffer + 1, 4);
	DEBUG_PROBE("%s: addr %04x -> %08" PRIx32 "\n", __func__, addr, value);
	return value;
}

static void remote_adiv5_ap_write(adiv5_access_port_s *const target_ap, const uint16_t addr, const uint32_t value)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Create the request and send it to the remote */
	int length = snprintf(
		buffer, REMOTE_MAX_MSG_SIZE, REMOTE_AP_WRITE_STR, target_ap->dp->dev_index, target_ap->apsel, addr, value);
	platform_buffer_write(buffer, length);
	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!remote_adiv5_check_error(__func__, target_ap->dp, buffer, length))
		return;
	DEBUG_PROBE("%s: addr %04x <- %08" PRIx32 "\n", __func__, addr, value);
}

static void remote_adiv5_mem_read_bytes(
	adiv5_access_port_s *const target_ap, void *const dest, const uint32_t src, const size_t read_length)
{
	/* Check if we have anything to do */
	if (!read_length)
		return;
	char *const data = (char *)dest;
	DEBUG_PROBE("%s: @%08" PRIx32 "+%zx\n", __func__, src, read_length);
	char buffer[REMOTE_MAX_MSG_SIZE];
	/*
	 * As we do, calculate how large a transfer we can do to the firmware.
	 * there are 2 leader bytes around responses and the data is hex-encoded taking 2 bytes a byte
	 */
	const size_t blocksize = (REMOTE_MAX_MSG_SIZE - 2U) / 2U;
	/* For each transfer block size, ask the firmware to read that block of bytes */
	for (size_t offset = 0; offset < read_length; offset += blocksize) {
		/* Pick the amount left to read or the block size, whichever is smaller */
		const size_t amount = MIN(read_length - offset, blocksize);
		/* Create the request and send it to the remote */
		int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIv5_MEM_READ_STR, target_ap->dp->dev_index,
			target_ap->apsel, target_ap->csw, src + offset, amount);
		platform_buffer_write(buffer, length);

		/* Read back the answer and check for errors */
		length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (!remote_adiv5_check_error(__func__, target_ap->dp, buffer, length)) {
			DEBUG_WARN("%s error around 0x%08zx\n", __func__, (size_t)src + offset);
			return;
		}
		/* If the response indicates all's OK, decode the data read */
		unhexify(data + offset, buffer + 1, amount);
	}
}

static void remote_adiv5_mem_write_bytes(adiv5_access_port_s *const target_ap, const uint32_t dest,
	const void *const src, const size_t write_length, const align_e align)
{
	/* Check if we have anything to do */
	if (!write_length)
		return;
	const char *data = (const char *)src;
	DEBUG_PROBE("%s: @%08" PRIx32 "+%zx alignment %u\n", __func__, dest, write_length, align);
	/* + 1 for terminating NUL character */
	char buffer[REMOTE_MAX_MSG_SIZE + 1U];
	/* As we do, calculate how large a transfer we can do to the firmware */
	const size_t blocksize = (REMOTE_MAX_MSG_SIZE - REMOTE_ADIv5_MEM_WRITE_LENGTH) / 2U;
	/* For each transfer block size, ask the firmware to write that block of bytes */
	for (size_t offset = 0; offset < write_length; offset += blocksize) {
		/* Pick the amount left to write or the block size, whichever is smaller */
		const size_t amount = MIN(write_length - offset, blocksize);
		/* Create the request and validate it ends up the right length */
		ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIv5_MEM_WRITE_STR, target_ap->dp->dev_index,
			target_ap->apsel, target_ap->csw, align, dest + offset, amount);
		assert(length == REMOTE_ADIv5_MEM_WRITE_LENGTH - 1U);
		/* Encode the data to send after the request block and append the packet termination marker */
		hexify(buffer + length, data + offset, amount);
		length += (ssize_t)(amount * 2U);
		buffer[length++] = REMOTE_EOM;
		buffer[length++] = '\0';
		platform_buffer_write(buffer, length);

		/* Read back the answer and check for errors */
		length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (!remote_adiv5_check_error(__func__, target_ap->dp, buffer, length)) {
			DEBUG_WARN("%s error around 0x%08zx\n", __func__, (size_t)dest + offset);
			return;
		}
	}
}

void remote_adiv5_dp_defaults(adiv5_debug_port_s *const target_dp)
{
	/* Ask the remote for its protocol version */
	platform_buffer_write(REMOTE_HL_CHECK_STR, sizeof(REMOTE_HL_CHECK_STR));
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* Read back the answer and check for errors */
	const ssize_t length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1) {
		DEBUG_WARN("%s comms error: %zd\n", __func__, length);
		exit(2);
	} else if (buffer[0] != REMOTE_RESP_OK) {
		DEBUG_INFO("Your probe firmware is too old, please update it to continue\n");
		exit(1);
	}
	/* If the probe's indicated that the request succeeded, convert the version number */
	const uint64_t version = remote_decode_response(buffer + 1, length - 1);
	if (version < 2) {
		DEBUG_WARN("Please update your probe's firmware for a substantial speed increase\n");
		return;
	}
	if (version == 2) {
		DEBUG_WARN("Falling back to non-high-level probe interface\n");
		return;
	}
	/* If the probe firmware talks a new enough variant of the protocol, we can use the accelerated routines above. */
	target_dp->low_access = remote_adiv5_raw_access;
	target_dp->dp_read = remote_adiv5_dp_read;
	target_dp->ap_write = remote_adiv5_ap_write;
	target_dp->ap_read = remote_adiv5_ap_read;
	target_dp->mem_read = remote_adiv5_mem_read_bytes;
	target_dp->mem_write = remote_adiv5_mem_write_bytes;
}

void remote_add_jtag_dev(uint32_t dev_indx, const jtag_dev_s *jtag_dev)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_ADD_DEV_STR, dev_indx, jtag_dev->dr_prescan,
		jtag_dev->dr_postscan, jtag_dev->ir_len, jtag_dev->ir_prescan, jtag_dev->ir_postscan, jtag_dev->current_ir);
	platform_buffer_write(buffer, length);
	(void)platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	/* Don't need to check for error here - it's already done in remote_adiv5_dp_defaults */
}
