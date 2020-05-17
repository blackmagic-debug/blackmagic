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

/* This file implements the SW-DP specific functions of the
 * ARM Debug Interface v5 Architecure Specification, ARM doc IHI0031A.
 */

#include "general.h"
#include "exception.h"
#include "adiv5.h"
#include "swdptap.h"
#include "target.h"
#include "target_internal.h"

#define SWDP_ACK_OK    0x01
#define SWDP_ACK_WAIT  0x02
#define SWDP_ACK_FAULT 0x04

int adiv5_swdp_scan(void)
{
	uint32_t ack;

	target_list_free();
	ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));
	if (!dp) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return -1;
	}

#if PC_HOSTED == 1
	if (platform_swdptap_init())
#else
	if (swdptap_init())
#endif
		return -1;

	/* Switch from JTAG to SWD mode */
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 16);
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 18);
	swd_proc.swdptap_seq_out(0xE79E, 16); /* 0b0111100111100111 */
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 18);
	swd_proc.swdptap_seq_out(0, 16);

	/* Read the SW-DP IDCODE register to syncronise */
	/* This could be done with adiv_swdp_low_access(), but this doesn't
	 * allow the ack to be checked here. */
	swd_proc.swdptap_seq_out(0xA5, 8);
	ack = swd_proc.swdptap_seq_in(3);
	if((ack != SWDP_ACK_OK) || swd_proc.swdptap_seq_in_parity(&dp->idcode, 32)) {
		DEBUG_WARN("Read SW-DP IDCODE failed %1" PRIx32 "\n", ack);
		free(dp);
		return -1;
	}

	dp->dp_read = firmware_swdp_read;
	dp->error = firmware_swdp_error;
	dp->low_access = firmware_swdp_low_access;
	dp->abort = firmware_swdp_abort;

	firmware_swdp_error(dp);
	adiv5_dp_init(dp);

	return target_list?1:0;
}

uint32_t firmware_swdp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	if (addr & ADIV5_APnDP) {
		adiv5_dp_low_access(dp, ADIV5_LOW_READ, addr, 0);
		return adiv5_dp_low_access(dp, ADIV5_LOW_READ,
		                           ADIV5_DP_RDBUFF, 0);
	} else {
		return firmware_swdp_low_access(dp, ADIV5_LOW_READ, addr, 0);
	}
}

 uint32_t firmware_swdp_error(ADIv5_DP_t *dp)
{
	uint32_t err, clr = 0;

	err = firmware_swdp_read(dp, ADIV5_DP_CTRLSTAT) &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP |
		ADIV5_DP_CTRLSTAT_STICKYERR | ADIV5_DP_CTRLSTAT_WDATAERR);

	if(err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if(err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if(err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if(err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;

	adiv5_dp_write(dp, ADIV5_DP_ABORT, clr);
	dp->fault = 0;

	return err;
}

uint32_t firmware_swdp_low_access(ADIv5_DP_t *dp, uint8_t RnW,
				      uint16_t addr, uint32_t value)
{
	bool APnDP = addr & ADIV5_APnDP;
	addr &= 0xff;
	uint32_t request = 0x81;
	uint32_t response = 0;
	uint32_t ack;
	platform_timeout timeout;

	if(APnDP && dp->fault) return 0;

	if(APnDP) request ^= 0x22;
	if(RnW)   request ^= 0x24;

	addr &= 0xC;
	request |= (addr << 1) & 0x18;
	if((addr == 4) || (addr == 8))
		request ^= 0x20;

	platform_timeout_set(&timeout, 2000);
	do {
		swd_proc.swdptap_seq_out(request, 8);
		ack = swd_proc.swdptap_seq_in(3);
	} while (ack == SWDP_ACK_WAIT && !platform_timeout_is_expired(&timeout));

	if (ack == SWDP_ACK_WAIT)
		raise_exception(EXCEPTION_TIMEOUT, "SWDP ACK timeout");

	if(ack == SWDP_ACK_FAULT) {
		dp->fault = 1;
		return 0;
	}

	if(ack != SWDP_ACK_OK)
		raise_exception(EXCEPTION_ERROR, "SWDP invalid ACK");

	if(RnW) {
		if(swd_proc.swdptap_seq_in_parity(&response, 32))  /* Give up on parity error */
			raise_exception(EXCEPTION_ERROR, "SWDP Parity error");
	} else {
		swd_proc.swdptap_seq_out_parity(value, 32);
		/* RM0377 Rev. 8 Chapter 27.5.4 for STM32L0x1 states:
		 * Because of the asynchronous clock domains SWCLK and HCLK,
		 * two extra SWCLK cycles are needed after a write transaction
		 * (after the parity bit) to make the write effective
		 * internally. These cycles should be applied while driving
		 * the line low (IDLE state)
		 * This is particularly important when writing the CTRL/STAT
		 * for a power-up request. If the next transaction (requiring
		 * a power-up) occurs immediately, it will fail.
		 */
		swd_proc.swdptap_seq_out(0, 2);
	}

	return response;
}

void firmware_swdp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}
