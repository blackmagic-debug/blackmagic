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

bool remote_adiv5_check_error(
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
		/* Otherwise it's an unexpected error */
		else
			DEBUG_WARN("%s: Unexpected error %u\n", func, error);
	}
	/* Return whether the remote indicated the request was successfull */
	return buffer[0] == REMOTE_RESP_OK;
}

static uint32_t remote_adiv5_dp_read(adiv5_debug_port_s *dp, uint16_t addr)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_DP_READ_STR, dp->dev_index, addr);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, length);
	uint32_t dest;
	unhexify(&dest, buffer + 1, 4);
	DEBUG_PROBE("dp_read addr %04x: %08" PRIx32 "\n", dest);
	return dest;
}

static uint32_t remote_adiv5_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIv5_RAW_ACCESS_STR, dp->dev_index, RnW, addr, value);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, length);
	uint32_t dest;
	unhexify(&dest, buffer + 1, 4);
	return dest;
}

static uint32_t remote_adiv5_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_AP_READ_STR, ap->dp->dev_index, ap->apsel, addr);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, length);
	uint32_t dest;
	unhexify(&dest, buffer + 1, 4);
	return dest;
}

static void remote_adiv5_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_AP_WRITE_STR, ap->dp->dev_index, ap->apsel, addr, value);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, length);
}

static void remote_ap_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	(void)ap;
	if (len == 0)
		return;
	char buffer[REMOTE_MAX_MSG_SIZE];
	size_t batchsize = (REMOTE_MAX_MSG_SIZE - 0x20U) / 2U;
	for (size_t offset = 0; offset < len; offset += batchsize) {
		const size_t count = MIN(len - offset, batchsize);
		int s = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIv5_MEM_READ_STR, ap->dp->dev_index, ap->apsel, ap->csw,
			src + offset, count);
		platform_buffer_write(buffer, s);
		s = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (s > 0 && buffer[0] == REMOTE_RESP_OK) {
			unhexify(dest + offset, (const char *)&buffer[1], count);
			continue;
		}
		if (buffer[0] == REMOTE_RESP_ERR) {
			ap->dp->fault = 1;
			DEBUG_WARN(
				"%s returned REMOTE_RESP_ERR at apsel %u, addr: 0x%08zx\n", __func__, ap->apsel, (size_t)src + offset);
			break;
		}
		DEBUG_WARN("%s error %d around 0x%08zx\n", __func__, s, (size_t)src + offset);
		break;
	}
}

static void remote_ap_mem_write_sized(
	adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	if (len == 0)
		return;
	char buffer[REMOTE_MAX_MSG_SIZE];
	/* (5 * 1 (char)) + (2 * 2 (bytes)) + (3 * 8 (words)) */
	size_t batchsize = (REMOTE_MAX_MSG_SIZE - 0x30U) / 2U;
	const char *data = (const char *)src;
	for (size_t offset = 0; offset < len; offset += batchsize) {
		const size_t count = MIN(len - offset, batchsize);
		int s = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_ADIv5_MEM_WRITE_STR, ap->dp->dev_index, ap->apsel, ap->csw,
			align, dest + offset, count);
		assert(s > 0);
		hexify(buffer + s, data + offset, count);
		const size_t hex_length = s + (count * 2U);
		buffer[hex_length] = REMOTE_EOM;
		buffer[hex_length + 1U] = '\0';
		platform_buffer_write(buffer, hex_length + 2U);

		s = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		if (s > 0 && buffer[0] == REMOTE_RESP_OK)
			continue;
		if (s > 0 && buffer[0] == REMOTE_RESP_ERR) {
			ap->dp->fault = 1;
			DEBUG_WARN(
				"%s returned REMOTE_RESP_ERR at apsel %u, addr: 0x%08zx\n", __func__, ap->apsel, (size_t)dest + offset);
		}
		DEBUG_WARN("%s error %d around address 0x%08zx\n", __func__, s, (size_t)dest + offset);
		break;
	}
}

void remote_adiv5_dp_defaults(adiv5_debug_port_s *dp)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_HL_CHECK_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR || buffer[1] - '0' < REMOTE_HL_VERSION) {
		DEBUG_WARN("Please update BMP firmware for substantial speed increase!\n");
		return;
	}
	dp->low_access = remote_adiv5_low_access;
	dp->dp_read = remote_adiv5_dp_read;
	dp->ap_write = remote_adiv5_ap_write;
	dp->ap_read = remote_adiv5_ap_read;
	dp->mem_read = remote_ap_mem_read;
	dp->mem_write = remote_ap_mem_write_sized;
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
