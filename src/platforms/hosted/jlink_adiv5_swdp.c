/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2019 - 2021 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
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

/* This file implements the SW-DP specific functions of the
 * ARM Debug Interface v5 Architecure Specification, ARM doc IHI0031A.
 */

#include "general.h"
#include "exception.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "jlink.h"
#include "cli.h"

static uint32_t jlink_adiv5_swdp_read(ADIv5_DP_t *dp, uint16_t addr);
static uint32_t jlink_adiv5_swdp_error(ADIv5_DP_t *dp);
static uint32_t jlink_adiv5_swdp_low_access(ADIv5_DP_t *dp, uint8_t RnW, uint16_t addr, uint32_t value);
static void jlink_adiv5_swdp_abort(ADIv5_DP_t *dp, uint32_t abort);

enum {
	SWDIO_WRITE,
	SWDIO_READ
};

/* Write at least 50 bits high, two bits low and read DP_IDR and put
*  idle cyccles at the end*/
static int line_reset(bmp_info_t *info)
{
	uint8_t cmd[44];
	memset(cmd, 0, sizeof(cmd));

	cmd[0] = CMD_HW_JTAG3;
	/* write 19 Bytes.*/
	cmd[2] = 19 * 8;
	uint8_t *direction = cmd + 4;
	memset(direction + 5, 0xffU, 9);
	direction[18] = 0xe0;
	uint8_t *data = direction + 19;
	memset(data + 5, 0xffU, 7);
	data[13] = 0xa5;

	uint8_t res[19];
	send_recv(info->usb_link, cmd, 42, res, 19);
	send_recv(info->usb_link, NULL, 0, res, 1);

	if (res[0] != 0) {
		DEBUG_WARN("Line reset failed\n");
		return -1;
	}

	return 0;
}

static bool jlink_swdptap_init(bmp_info_t *info)
{
	uint8_t cmd[2] = {CMD_GET_SELECT_IF, JLINK_IF_GET_AVAILABLE};
	uint8_t res[4];
	send_recv(info->usb_link, cmd, 2, res, sizeof(res));

	if (!(res[0] & JLINK_IF_SWD))
		return false;

	cmd[1] = SELECT_IF_SWD;
	send_recv(info->usb_link, cmd, 2, res, sizeof(res));

	platform_delay(10);

	/* SWD speed is fixed. Do not set it here*/

	return true;
}

uint32_t jlink_swdp_scan(bmp_info_t *info)
{
	target_list_free();

	if (!jlink_swdptap_init(info))
		return 0;

	uint8_t cmd[44];
	memset(cmd, 0, sizeof(cmd));

	cmd[0] = CMD_HW_JTAG3;
	/* write 18 Bytes.*/
	cmd[2] = 17U * 8U;
	uint8_t *direction = cmd + 4;
	memset(direction, 0xffU, 17);
	uint8_t *data = direction + 17;
	memset(data, 0xffU, 7);
	data[7] = 0x9e;
	data[8] = 0xe7;
	memset(data + 9, 0xffU, 6);

	uint8_t res[18];
	send_recv(info->usb_link, cmd, 38, res, 17);
	send_recv(info->usb_link, NULL, 0, res, 1);

	if (res[0] != 0) {
		DEBUG_WARN("Line reset failed\n");
		return 0;
	}

	ADIv5_DP_t *dp = calloc(1, sizeof(*dp));
	if (!dp) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return 0;
	}

	dp->dp_read = jlink_adiv5_swdp_read;
	dp->error = jlink_adiv5_swdp_error;
	dp->low_access = jlink_adiv5_swdp_low_access;
	dp->abort = jlink_adiv5_swdp_abort;

	jlink_adiv5_swdp_error(dp);

	adiv5_dp_init(dp, 0);

	return target_list ? 1U : 0U;
}

static uint32_t jlink_adiv5_swdp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	if (addr & ADIV5_APnDP) {
		adiv5_dp_low_access(dp, ADIV5_LOW_READ, addr, 0);
		return adiv5_dp_low_access(dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
	} else {
		return jlink_adiv5_swdp_low_access(dp, ADIV5_LOW_READ, addr, 0);
	}
}

static uint32_t jlink_adiv5_swdp_error(ADIv5_DP_t *dp)
{
	uint32_t err, clr = 0;
	err = jlink_adiv5_swdp_read(dp, ADIV5_DP_CTRLSTAT) &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP | ADIV5_DP_CTRLSTAT_STICKYERR |
			ADIV5_DP_CTRLSTAT_WDATAERR);

	if (err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;
	if (clr)
		adiv5_dp_write(dp, ADIV5_DP_ABORT, clr);
	if (dp->fault)
		err |= 0x8000U;
	dp->fault = 0;

	return err;
}

static uint32_t jlink_adiv5_swdp_low_access(ADIv5_DP_t *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	uint8_t request = make_packet_request(RnW, addr);
	bool APnDP = addr & ADIV5_APnDP;
	uint32_t response = 0;
	uint8_t ack;
	platform_timeout timeout;

	if (APnDP && dp->fault)
		return 0;

	uint8_t cmd[16];
	memset(cmd, 0, sizeof(cmd));

	uint8_t res[8];
	cmd[0] = CMD_HW_JTAG3;

	/* It seems, JLINK samples read data at end of previous clock.
	 * So target data read must start at the 12'th clock, while
	 * write starts as expected at the 14'th clock (8 cmd, 3 response,
	 * 2 turn around.
	 */
	cmd[2] = RnW ? 11 : 13;

	cmd[4] = 0xff; /* 8 bits command OUT */
	cmd[5] = 0xf0; /* one IN bit to turn around to read, read 2
					  (read) or 3 (write) IN bits for response and
					  and one OUT bit to turn around to write on write*/
	cmd[6] = request;

	platform_timeout_set(&timeout, 2000);
	do {
		send_recv(info.usb_link, cmd, 8, res, 2);
		send_recv(info.usb_link, NULL, 0, res + 2, 1);

		if (res[2] != 0)
			raise_exception(EXCEPTION_ERROR, "Low access setup failed");

		ack = res[1] & 7;
	} while (ack == SWDP_ACK_WAIT && !platform_timeout_is_expired(&timeout));

	if (ack == SWDP_ACK_WAIT)
		raise_exception(EXCEPTION_TIMEOUT, "SWDP ACK timeout");

	if (ack == SWDP_ACK_FAULT) {
		if (cl_debuglevel & BMP_DEBUG_TARGET)
			DEBUG_WARN("Fault\n");
		dp->fault = 1;
		return 0;
	}

	if (ack != SWDP_ACK_OK) {
		if (cl_debuglevel & BMP_DEBUG_TARGET)
			DEBUG_WARN("Protocol %d\n", ack);
		line_reset(&info);
		return 0;
	}

	/* Always append 8 idle cycle (SWDIO = 0)!*/
	if (RnW) {
		memset(cmd + 4, 0, 10);
		cmd[2] = 33 + 2; /* 2 idle cycles */
		cmd[8] = 0xfe;
		send_recv(info.usb_link, cmd, 14, res, 5);
		send_recv(info.usb_link, NULL, 0, res + 5, 1);

		if (res[5] != 0)
			raise_exception(EXCEPTION_ERROR, "Low access read failed");

		response = res[0] | res[1] << 8U | res[2] << 16U | res[3] << 24U;

		const unsigned int parity = res[4] & 1;
		const unsigned int bit_count = __builtin_popcount(response) + parity;
		if (bit_count & 1) /* Give up on parity error */
			raise_exception(EXCEPTION_ERROR, "SWDP Parity error");
	} else {
		cmd[2] = 33 + 8; /* 8 idle cycle  to move data through SW-DP */
		memset(cmd + 4, 0xffU, 6);
		cmd[10] = ((value >> 0U) & 0xffU);
		cmd[11] = ((value >> 8U) & 0xffU);
		cmd[12] = ((value >> 16U) & 0xffU);
		cmd[13] = ((value >> 24U) & 0xffU);
		const unsigned int bit_count = __builtin_popcount(value);
		cmd[14] = bit_count & 1;
		cmd[15] = 0;

		send_recv(info.usb_link, cmd, 16, res, 6);
		send_recv(info.usb_link, NULL, 0, res, 1);

		if (res[0] != 0)
			raise_exception(EXCEPTION_ERROR, "Low access write failed");
	}
	return response;
}

static void jlink_adiv5_swdp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}
