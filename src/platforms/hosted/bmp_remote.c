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
#include <sys/time.h>
#include <errno.h>

#include "adiv5.h"

int remote_init(void)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int c = snprintf(construct, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_START_STR);
	platform_buffer_write((uint8_t *)construct, c);
	c = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (c < 1 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("Remote Start failed, error %s\n", c ? construct + 1 : "unknown");
		return -1;
	}
	DEBUG_PROBE("Remote is %s\n", construct + 1);
	return 0;
}

bool remote_target_get_power(void)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_PWR_GET_STR);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_target_get_power failed, error %s\n", s ? construct + 1 : "unknown");
		exit(-1);
	}
	return construct[1] == '1';
}

bool remote_target_set_power(const bool power)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_PWR_SET_STR, power ? '1' : '0');
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("platform_target_set_power failed, error %s\n", s ? construct + 1 : "unknown");
	return s > 0 && construct[0] == REMOTE_RESP_OK;
}

void remote_nrst_set_val(bool assert)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_NRST_SET_STR, assert ? '1' : '0');
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_nrst_set_val failed, error %s\n", s ? construct + 1 : "unknown");
		exit(-1);
	}
}

bool remote_nrst_get_val(void)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_NRST_GET_STR);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_nrst_set_val failed, error %s\n", s ? construct + 1 : "unknown");
		exit(-1);
	}
	return construct[1] == '1';
}

void remote_max_frequency_set(uint32_t freq)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_FREQ_SET_STR, freq);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("Update Firmware to allow to set max SWJ frequency\n");
}

uint32_t remote_max_frequency_get(void)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_FREQ_GET_STR);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		return FREQ_FIXED;
	uint32_t freq;
	unhexify(&freq, construct + 1, 4);
	return freq;
}

const char *remote_target_voltage(void)
{
	static char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, " %s", REMOTE_VOLTAGE_STR);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR) {
		DEBUG_WARN("platform_target_voltage failed, error %s\n", s ? construct + 1 : "unknown");
		exit(-1);
	}
	return construct + 1;
}

void remote_target_clk_output_enable(const bool enable)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_TARGET_CLK_OE_STR, enable ? '1' : '0');
	platform_buffer_write((uint8_t *)buffer, length);
	length = platform_buffer_read((uint8_t *)buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("remote_target_clk_output_enable failed, error %s\n", length ? buffer + 1 : "unknown");
}

static uint32_t remote_adiv5_dp_read(adiv5_debug_port_s *dp, uint16_t addr)
{
	(void)dp;
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_DP_READ_STR, dp->dp_jd_index, addr);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, s);
	uint32_t dest;
	unhexify(&dest, construct + 1, 4);
	DEBUG_PROBE("dp_read addr %04x: %08" PRIx32 "\n", dest);
	return dest;
}

static uint32_t remote_adiv5_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	(void)dp;
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_LOW_ACCESS_STR, dp->dp_jd_index, RnW, addr, value);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, s);
	uint32_t dest;
	unhexify(&dest, construct + 1, 4);
	return dest;
}

static uint32_t remote_adiv5_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_AP_READ_STR, ap->dp->dp_jd_index, ap->apsel, addr);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, s);
	uint32_t dest;
	unhexify(&dest, construct + 1, 4);
	return dest;
}

static void remote_adiv5_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	char construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_AP_WRITE_STR, ap->dp->dp_jd_index, ap->apsel, addr, value);
	platform_buffer_write((uint8_t *)construct, s);
	s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR)
		DEBUG_WARN("%s error %d\n", __func__, s);
}

static void remote_ap_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	(void)ap;
	if (len == 0)
		return;
	char construct[REMOTE_MAX_MSG_SIZE];
	size_t batchsize = (REMOTE_MAX_MSG_SIZE - 0x20U) / 2U;
	for (size_t offset = 0; offset < len; offset += batchsize) {
		const size_t count = MIN(len - offset, batchsize);
		int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_AP_MEM_READ_STR, ap->dp->dp_jd_index, ap->apsel,
			ap->csw, src + offset, count);
		platform_buffer_write((uint8_t *)construct, s);
		s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
		if (s > 0 && construct[0] == REMOTE_RESP_OK) {
			unhexify(dest + offset, (const char *)&construct[1], count);
			continue;
		}
		if (construct[0] == REMOTE_RESP_ERR) {
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
	(void)ap;
	if (len == 0)
		return;
	char construct[REMOTE_MAX_MSG_SIZE];
	/* (5 * 1 (char)) + (2 * 2 (bytes)) + (3 * 8 (words)) */
	size_t batchsize = (REMOTE_MAX_MSG_SIZE - 0x30U) / 2U;
	const char *data = (const char *)src;
	for (size_t offset = 0; offset < len; offset += batchsize) {
		const size_t count = MIN(len - offset, batchsize);
		int s = snprintf(construct, REMOTE_MAX_MSG_SIZE, REMOTE_AP_MEM_WRITE_SIZED_STR, ap->dp->dp_jd_index, ap->apsel,
			ap->csw, align, dest + offset, count);
		assert(s > 0);
		hexify(construct + s, data + offset, count);
		const size_t hex_length = s + (count * 2U);
		construct[hex_length] = REMOTE_EOM;
		construct[hex_length + 1U] = '\0';
		platform_buffer_write((uint8_t *)construct, hex_length + 2U);

		s = platform_buffer_read((uint8_t *)construct, REMOTE_MAX_MSG_SIZE);
		if (s > 0 && construct[0] == REMOTE_RESP_OK)
			continue;
		if (s > 0 && construct[0] == REMOTE_RESP_ERR) {
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
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_HL_CHECK_STR);
	platform_buffer_write(construct, s);
	s = platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	if (s < 1 || construct[0] == REMOTE_RESP_ERR || construct[1] - '0' < REMOTE_HL_VERSION) {
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

void remote_add_jtag_dev(uint32_t i, const jtag_dev_s *jtag_dev)
{
	uint8_t construct[REMOTE_MAX_MSG_SIZE];
	const int s = snprintf((char *)construct, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_ADD_DEV_STR, i, jtag_dev->dr_prescan,
		jtag_dev->dr_postscan, jtag_dev->ir_len, jtag_dev->ir_prescan, jtag_dev->ir_postscan, jtag_dev->current_ir);
	platform_buffer_write(construct, s);
	(void)platform_buffer_read(construct, REMOTE_MAX_MSG_SIZE);
	/* Don't need to check for error here - it's already done in remote_adiv5_dp_defaults */
}
