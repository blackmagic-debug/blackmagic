/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements the JTAG-DP specific functions of the
 * ARM Debug Interface v5 Architecure Specification, ARM doc IHI0031A.
 */

#include "general.h"
#include "exception.h"
#include "adiv5.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "morse.h"

#define JTAGDP_ACK_OK	0x02
#define JTAGDP_ACK_WAIT	0x01

/* 35-bit registers that control the ADIv5 DP */
#define IR_ABORT	0x8
#define IR_DPACC	0xA
#define IR_APACC	0xB

static uint32_t adiv5_jtagdp_read(ADIv5_DP_t *dp, uint16_t addr);

static uint32_t adiv5_jtagdp_error(ADIv5_DP_t *dp);

static uint32_t adiv5_jtagdp_low_access(ADIv5_DP_t *dp, uint8_t RnW,
					uint16_t addr, uint32_t value);

static void adiv5_jtagdp_abort(ADIv5_DP_t *dp, uint32_t abort);

void adiv5_jtag_dp_handler(jtag_dev_t *dev)
{
	ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));

	dp->dev = dev;
	dp->idcode = dev->idcode;

	dp->dp_read = adiv5_jtagdp_read;
	dp->error = adiv5_jtagdp_error;
	dp->low_access = adiv5_jtagdp_low_access;
	dp->abort = adiv5_jtagdp_abort;

	adiv5_dp_init(dp);
}

static uint32_t adiv5_jtagdp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	adiv5_jtagdp_low_access(dp, ADIV5_LOW_READ, addr, 0);
	return adiv5_jtagdp_low_access(dp, ADIV5_LOW_READ,
					ADIV5_DP_RDBUFF, 0);
}

static uint32_t adiv5_jtagdp_error(ADIv5_DP_t *dp)
{
	adiv5_jtagdp_low_access(dp, ADIV5_LOW_READ, ADIV5_DP_CTRLSTAT, 0);
	return adiv5_jtagdp_low_access(dp, ADIV5_LOW_WRITE,
				ADIV5_DP_CTRLSTAT, 0xF0000032) & 0x32;
}

static uint32_t adiv5_jtagdp_low_access(ADIv5_DP_t *dp, uint8_t RnW,
					uint16_t addr, uint32_t value)
{
	bool APnDP = addr & ADIV5_APnDP;
	addr &= 0xff;
	uint64_t request, response;
	uint8_t ack;
	platform_timeout timeout;

	request = ((uint64_t)value << 3) | ((addr >> 1) & 0x06) | (RnW?1:0);

	jtag_dev_write_ir(dp->dev, APnDP ? IR_APACC : IR_DPACC);

	platform_timeout_set(&timeout, 2000);
	do {
		jtag_dev_shift_dr(dp->dev, (uint8_t*)&response, (uint8_t*)&request, 35);
		ack = response & 0x07;
	} while(!platform_timeout_is_expired(&timeout) && (ack == JTAGDP_ACK_WAIT));

	if (ack == JTAGDP_ACK_WAIT)
		raise_exception(EXCEPTION_TIMEOUT, "JTAG-DP ACK timeout");

	if((ack != JTAGDP_ACK_OK))
		raise_exception(EXCEPTION_ERROR, "JTAG-DP invalid ACK");

	return (uint32_t)(response >> 3);
}

static void adiv5_jtagdp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	uint64_t request = (uint64_t)abort << 3;
	jtag_dev_write_ir(dp->dev, IR_ABORT);
	jtag_dev_shift_dr(dp->dev, NULL, (const uint8_t*)&request, 35);
}

