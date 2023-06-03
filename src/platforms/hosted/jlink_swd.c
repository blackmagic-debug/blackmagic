/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2019 - 2021 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

/*
 * This file implements the SW-DP specific functions of the
 * ARM Debug Interface v5 Architecture Specification, ARM doc IHI0031A.
 */

#include <assert.h>
#include "general.h"
#include "exception.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "jlink.h"
#include "jlink_protocol.h"
#include "buffer_utils.h"
#include "cli.h"

static uint32_t jlink_swd_seq_in(size_t clock_cycles);

static bool jlink_adiv5_swdp_write_nocheck(uint16_t addr, uint32_t data);
static uint32_t jlink_adiv5_swdp_read_nocheck(uint16_t addr);
static uint32_t jlink_adiv5_swdp_error(adiv5_debug_port_s *dp, bool protocol_recovery);
static uint32_t jlink_adiv5_swdp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
static void jlink_adiv5_swdp_abort(adiv5_debug_port_s *dp, uint32_t abort);

/*
 * Write at least 50 bits high, two bits low and read DP_IDR and put
 * idle cycles at the end
 */
static bool line_reset(bmp_info_s *const info)
{
	uint8_t cmd[44];
	memset(cmd, 0, sizeof(cmd));

	cmd[0] = JLINK_CMD_IO_TRANSACT;
	/* write 19 bytes */
	cmd[2] = 19U * 8U;
	uint8_t *const direction = cmd + 4U;
	memset(direction + 5U, 0xffU, 9U);
	direction[18] = 0xe0U;
	uint8_t *const data = direction + 19U;
	memset(data + 5U, 0xffU, 7U);
	data[13] = 0xa5U;

	uint8_t res[19];
	bmda_usb_transfer(info->usb_link, cmd, 42U, res, 19U);
	bmda_usb_transfer(info->usb_link, NULL, 0U, res, 1U);

	if (res[0] != 0) {
		DEBUG_ERROR("Line reset failed\n");
		return false;
	}
	return true;
}

bool jlink_swd_init(adiv5_debug_port_s *dp)
{
	DEBUG_PROBE("-> jlink_swd_init(%u)\n", dp->dev_index);
	/* Try to switch the adaptor into SWD mode */
	uint8_t res[4];
	if (jlink_simple_request(JLINK_CMD_TARGET_IF, JLINK_IF_GET_AVAILABLE, res, sizeof(res)) < 0 ||
		!(res[0] & JLINK_IF_SWD) || jlink_simple_request(JLINK_CMD_TARGET_IF, SELECT_IF_SWD, res, sizeof(res)) < 0) {
		DEBUG_ERROR("SWD not available\n");
		return false;
	}
	platform_delay(10);

	/* Set up the underlying SWD functions using the implementation below */
	swd_proc.seq_in = jlink_swd_seq_in;

	/* Set up the accelerated SWD functions for basic target operations */
	dp->dp_low_write = jlink_adiv5_swdp_write_nocheck;
	dp->dp_read = firmware_swdp_read;
	dp->error = jlink_adiv5_swdp_error;
	dp->low_access = jlink_adiv5_swdp_low_access;
	dp->abort = jlink_adiv5_swdp_abort;
	return true;
}

uint32_t jlink_swdp_scan(bmp_info_s *const info)
{
	adiv5_debug_port_s *dp = calloc(1, sizeof(*dp));
	if (!dp) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return 0;
	}

	target_list_free();
	if (!jlink_swd_init(dp)) {
		free(dp);
		return 0;
	}

	uint8_t cmd[44];
	memset(cmd, 0, sizeof(cmd));

	cmd[0] = JLINK_CMD_IO_TRANSACT;
	/* write 18 Bytes.*/
	cmd[2] = 17U * 8U;
	uint8_t *direction = cmd + 4U;
	memset(direction, 0xffU, 17U);
	uint8_t *data = direction + 17U;
	memset(data, 0xffU, 7U);
	data[7] = 0x9eU;
	data[8] = 0xe7U;
	memset(data + 9U, 0xffU, 6U);

	uint8_t res[18];
	bmda_usb_transfer(info->usb_link, cmd, 38U, res, 17U);
	bmda_usb_transfer(info->usb_link, NULL, 0U, res, 1U);

	if (res[0] != 0) {
		DEBUG_ERROR("Line reset failed\n");
		free(dp);
		return 0;
	}

	adiv5_dp_error(dp);
	adiv5_dp_init(dp, 0);
	return target_list ? 1U : 0U;
}

static uint32_t jlink_swd_seq_in(const size_t clock_cycles)
{
	/* Create a buffer to hold the result of the transfer */
	uint8_t data_out[4] = {0};
	/* Attempt the transfer */
	if (!jlink_transfer_swd(clock_cycles, JLINK_SWD_IN, NULL, data_out)) {
		DEBUG_ERROR("jlink_swd_seq_in failed\n");
		return 0U;
	}
	/* Everything went well, so now convert the result and return it */
	const uint32_t result = read_le4(data_out, 0);
	DEBUG_PROBE("%s %zu clock_cycles: %08" PRIx32 "\n", __func__, clock_cycles, result);
	return result;
}

static void jlink_adiv5_swdp_make_packet_request(
	uint8_t *cmd, size_t cmd_length, const uint8_t RnW, const uint16_t addr)
{
	assert(cmd_length == 8U);
	memset(cmd, 0, cmd_length);
	cmd[0] = JLINK_CMD_IO_TRANSACT;

	/*
	 * It seems that JLink samples the data to read at the end of the
	 * previous clock cycle, so reading target data must start at the
	 * 12th clock cycle, while writing starts as expected at the 14th
	 * clock cycle (8 cmd, 3 response, 2 turn around).
	 */
	cmd[2] = RnW ? 11 : 13;

	cmd[4] = 0xffU; /* 8 bits command OUT */
	/*
	 * one IN bit to turn around to read, read 2
	 * (read) or 3 (write) IN bits for response and
	 * and one OUT bit to turn around to write on write
	 */
	cmd[5] = 0xf0U;
	cmd[6] = make_packet_request(RnW, addr);
}

static bool jlink_adiv5_swdp_write_nocheck(const uint16_t addr, const uint32_t data)
{
	uint8_t result[3];
	uint8_t request[8];
	jlink_adiv5_swdp_make_packet_request(request, sizeof(request), ADIV5_LOW_WRITE, addr & 0xfU);
	bmda_usb_transfer(info.usb_link, request, 8U, result, 2U);
	bmda_usb_transfer(info.usb_link, NULL, 0U, result + 2U, 1U);
	const uint8_t ack = result[1] & 7U;

	uint8_t response[16];
	memset(response, 0, sizeof(response));
	response[2] = 33U + 8U; /* 8 idle cycle  to move data through SW-DP */
	memset(response + 4U, 0xffU, 6);
	response[10] = data & 0xffU;
	response[11] = (data >> 8U) & 0xffU;
	response[12] = (data >> 16U) & 0xffU;
	response[13] = (data >> 24U) & 0xffU;
	const uint8_t bit_count = __builtin_popcount(data);
	response[14] = bit_count & 1U;

	bmda_usb_transfer(info.usb_link, response, 16, result, 6);
	bmda_usb_transfer(info.usb_link, NULL, 0, result, 1);
	if (result[0] != 0)
		raise_exception(EXCEPTION_ERROR, "Low access write failed");
	return ack != SWDP_ACK_OK;
}

static uint32_t jlink_adiv5_swdp_read_nocheck(const uint16_t addr)
{
	uint8_t result[6];
	uint8_t request[8];
	jlink_adiv5_swdp_make_packet_request(request, sizeof(request), ADIV5_LOW_READ, addr & 0xfU);
	bmda_usb_transfer(info.usb_link, request, 8U, result, 2U);
	bmda_usb_transfer(info.usb_link, NULL, 0U, result + 2U, 1U);
	const uint8_t ack = result[1] & 7U;

	uint8_t response[14];
	memset(response, 0, sizeof(response));
	response[0] = JLINK_CMD_IO_TRANSACT;
	response[2] = 33U + 2U; /* 2 idle cycles */
	response[8] = 0xfe;
	bmda_usb_transfer(info.usb_link, response, 14, result, 5);
	bmda_usb_transfer(info.usb_link, NULL, 0, result + 5U, 1);
	if (result[5] != 0)
		raise_exception(EXCEPTION_ERROR, "Low access read failed");
	const uint32_t data = result[0] | result[1] << 8U | result[2] << 16U | result[3] << 24U;
	return ack == SWDP_ACK_OK ? data : 0;
}

static uint32_t jlink_adiv5_swdp_error(adiv5_debug_port_s *const dp, const bool protocol_recovery)
{
	if (dp->version >= 2 || protocol_recovery) {
		line_reset(&info);
		jlink_adiv5_swdp_read_nocheck(ADIV5_DP_DPIDR);
		jlink_adiv5_swdp_write_nocheck(ADIV5_DP_TARGETSEL, dp->targetsel);
	}
	uint32_t err = jlink_adiv5_swdp_read_nocheck(ADIV5_DP_CTRLSTAT) &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP | ADIV5_DP_CTRLSTAT_STICKYERR |
			ADIV5_DP_CTRLSTAT_WDATAERR);
	uint32_t clr = 0;

	if (err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;

	jlink_adiv5_swdp_write_nocheck(ADIV5_DP_ABORT, clr);
	if (dp->fault)
		err |= 0x8000U;
	dp->fault = 0;
	return err;
}

static uint32_t jlink_adiv5_swdp_low_read(adiv5_debug_port_s *const dp)
{
	uint8_t cmd[14];
	uint8_t result[6];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = JLINK_CMD_IO_TRANSACT;
	cmd[2] = 33U + 2U; /* 2 idle cycles */
	cmd[8] = 0xfe;
	bmda_usb_transfer(info.usb_link, cmd, 14, result, 5);
	bmda_usb_transfer(info.usb_link, NULL, 0, result + 5U, 1);

	if (result[5] != 0)
		raise_exception(EXCEPTION_ERROR, "Low access read failed");

	const uint32_t response = result[0] | result[1] << 8U | result[2] << 16U | result[3] << 24U;

	const uint8_t parity = result[4] & 1U;
	const uint32_t bit_count = __builtin_popcount(response) + parity;
	if (bit_count & 1U) /* Give up on parity error */
	{
		dp->fault = 1;
		DEBUG_ERROR("SWD access resulted in parity error\n");
		raise_exception(EXCEPTION_ERROR, "SWD parity error");
	}
	return response;
}

static void jlink_adiv5_swdp_low_write(const uint32_t value)
{
	uint8_t cmd[16];
	uint8_t result[6];
	memset(cmd, 0, sizeof(cmd));
	cmd[2] = 33U + 8U; /* 8 idle cycle  to move data through SW-DP */
	memset(cmd + 4U, 0xffU, 6);
	cmd[10] = value & 0xffU;
	cmd[11] = (value >> 8U) & 0xffU;
	cmd[12] = (value >> 16U) & 0xffU;
	cmd[13] = (value >> 24U) & 0xffU;
	const uint8_t bit_count = __builtin_popcount(value);
	cmd[14] = bit_count & 1U;

	bmda_usb_transfer(info.usb_link, cmd, 16, result, 6);
	bmda_usb_transfer(info.usb_link, NULL, 0, result, 1);

	if (result[0] != 0)
		raise_exception(EXCEPTION_ERROR, "Low access write failed");
}

static uint32_t jlink_adiv5_swdp_low_access(
	adiv5_debug_port_s *const dp, const uint8_t RnW, const uint16_t addr, const uint32_t value)
{
	if ((addr & ADIV5_APnDP) && dp->fault)
		return 0;

	uint8_t cmd[8];
	jlink_adiv5_swdp_make_packet_request(cmd, sizeof(cmd), RnW, addr);

	uint8_t res[3];
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 2000);
	uint8_t ack = SWDP_ACK_WAIT;
	while (ack == SWDP_ACK_WAIT && !platform_timeout_is_expired(&timeout)) {
		bmda_usb_transfer(info.usb_link, cmd, 8U, res, 2U);
		bmda_usb_transfer(info.usb_link, NULL, 0U, res + 2U, 1U);

		if (res[2] != 0)
			raise_exception(EXCEPTION_ERROR, "Low access setup failed");
		ack = res[1] & 7U;
	};

	if (ack == SWDP_ACK_WAIT) {
		DEBUG_WARN("SWD access resulted in wait, aborting\n");
		dp->abort(dp, ADIV5_DP_ABORT_DAPABORT);
		dp->fault = ack;
		return 0;
	}

	if (ack == SWDP_ACK_FAULT) {
		DEBUG_ERROR("SWD access resulted in fault\n");
		dp->fault = ack;
		return 0;
	}

	if (ack == SWDP_ACK_NO_RESPONSE) {
		DEBUG_ERROR("SWD access resulted in no response\n");
		dp->fault = ack;
		return 0;
	}

	if (ack != SWDP_ACK_OK) {
		DEBUG_ERROR("SWD access has invalid ack %x\n", ack);
		raise_exception(EXCEPTION_ERROR, "SWD invalid ACK");
	}

	/* Always append 8 idle cycles (SWDIO = 0)! */
	if (RnW)
		return jlink_adiv5_swdp_low_read(dp);
	jlink_adiv5_swdp_low_write(value);
	return 0;
}

static void jlink_adiv5_swdp_abort(adiv5_debug_port_s *const dp, const uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}
