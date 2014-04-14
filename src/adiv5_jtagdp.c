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
#include "platform.h"
#include "adiv5.h"
#include "jtag_scan.h"
#include "jtagtap.h"

#include <stdlib.h>

#define JTAGDP_ACK_OK	0x02
#define JTAGDP_ACK_WAIT	0x01

/* 35-bit registers that control the ADIv5 DP */
#define IR_ABORT	0x8
#define IR_DPACC	0xA
#define IR_APACC	0xB

static void adiv5_jtagdp_write(ADIv5_DP_t *dp, uint8_t addr, uint32_t value);
static uint32_t adiv5_jtagdp_read(ADIv5_DP_t *dp, uint8_t addr);

static uint32_t adiv5_jtagdp_error(ADIv5_DP_t *dp);

static uint32_t adiv5_jtagdp_low_access(ADIv5_DP_t *dp, uint8_t APnDP, uint8_t RnW,
					uint8_t addr, uint32_t value);


void adiv5_jtag_dp_handler(jtag_dev_t *dev)
{
	ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));

	dp->dev = dev;
	dp->idcode = dev->idcode;

	dp->dp_write = adiv5_jtagdp_write;
	dp->dp_read = adiv5_jtagdp_read;
	dp->error = adiv5_jtagdp_error;
	dp->low_access = adiv5_jtagdp_low_access;
	dp->idcode_sync = NULL;

	adiv5_dp_init(dp);
}

static void adiv5_jtagdp_write(ADIv5_DP_t *dp, uint8_t addr, uint32_t value)
{
	adiv5_jtagdp_low_access(dp, ADIV5_LOW_DP, ADIV5_LOW_WRITE, addr, value);
}

static uint32_t adiv5_jtagdp_read(ADIv5_DP_t *dp, uint8_t addr)
{
	adiv5_jtagdp_low_access(dp, ADIV5_LOW_DP, ADIV5_LOW_READ, addr, 0);
	return adiv5_jtagdp_low_access(dp, ADIV5_LOW_DP, ADIV5_LOW_READ,
					ADIV5_DP_RDBUFF, 0);
}

static uint32_t adiv5_jtagdp_error(ADIv5_DP_t *dp)
{
	adiv5_jtagdp_low_access(dp, ADIV5_LOW_DP, ADIV5_LOW_READ,
				ADIV5_DP_CTRLSTAT, 0);
	return adiv5_jtagdp_low_access(dp, ADIV5_LOW_DP, ADIV5_LOW_WRITE,
				ADIV5_DP_CTRLSTAT, 0xF0000032) & 0x32;
}

static uint32_t adiv5_jtagdp_low_access(ADIv5_DP_t *dp, uint8_t APnDP, uint8_t RnW,
					uint8_t addr, uint32_t value)
{
	uint64_t request, response;
	uint8_t ack;

	request = ((uint64_t)value << 3) | ((addr >> 1) & 0x06) | (RnW?1:0);

	jtag_dev_write_ir(dp->dev, APnDP?IR_APACC:IR_DPACC);

	int tries = 1000;
	do {
		jtag_dev_shift_dr(dp->dev, (uint8_t*)&response, (uint8_t*)&request, 35);
		ack = response & 0x07;
	} while(--tries && (ack == JTAGDP_ACK_WAIT));

	if (dp->allow_timeout && (ack == JTAGDP_ACK_WAIT))
		return 0;

	if((ack != JTAGDP_ACK_OK)) {
		/* Fatal error if invalid ACK response */
		PLATFORM_FATAL_ERROR(1);
	}

	return (uint32_t)(response >> 3);
}

