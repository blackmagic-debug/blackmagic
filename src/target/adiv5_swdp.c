/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2020- 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

static unsigned int make_packet_request(uint8_t  RnW, uint16_t addr)
{
	bool APnDP = addr & ADIV5_APnDP;
	addr &= 0xff;
	unsigned int request = 0x81; /* Park and Startbit */
	if(APnDP) request ^= 0x22;
	if(RnW)   request ^= 0x24;

	addr &= 0xC;
	request |= (addr << 1) & 0x18;
	if((addr == 4) || (addr == 8))
		request ^= 0x20;
	return request;
}

/* Provide bare DP access functions without timeout and exception */

static void dp_line_reset(void)
{
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
	swd_proc.swdptap_seq_out(0x0FFFFFFF, 32);
}

static void dp_write(uint16_t addr, const uint32_t data)
{
	int bank = (addr >> 4) & 0xf;
	unsigned int request;
	if (bank)
		dp_write(ADIV5_DP_SELECT, bank);
	request =  make_packet_request(ADIV5_LOW_WRITE, addr & 0xf);
	swd_proc.swdptap_seq_out(request, 8);
	swd_proc.swdptap_seq_in(3);
	swd_proc.swdptap_seq_out_parity(data, 32);
	if (bank)
		dp_write(ADIV5_DP_SELECT, 0);
}

static bool dp_read(uint16_t addr, uint32_t *res)
{
	int bank = (addr >> 4) & 0xf;
	unsigned int request;
	if (bank)
		dp_write(ADIV5_DP_SELECT, bank);
	request =  make_packet_request(ADIV5_LOW_READ, addr & 0xf);
	swd_proc.swdptap_seq_out(request, 8);
	swd_proc.swdptap_seq_in(3);
	if (swd_proc.swdptap_seq_in_parity(res, 32)) {
		return true;
	}
	if (bank)
		dp_write(ADIV5_DP_SELECT, 0);
	return false;
}

/* Try first the dormant to SWD procedure.
 * If target id given, scan DPs 0 .. 15 on that device and return.
 * Otherwise
 */
int adiv5_swdp_scan(uint32_t targetid)
{
	target_list_free();
#if PC_HOSTED == 1
	if (platform_swdptap_init()) {
		exit(-1);
	}
#else
	if (swdptap_init()) {
		return -1;
	}
#endif

	/* DORMANT-> SWD sequence*/
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
	swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
	/* 128 bit selection alert sequence for SW-DP-V2 */
	swd_proc.swdptap_seq_out(0x6209f392, 32);
	swd_proc.swdptap_seq_out(0x86852d95, 32);
	swd_proc.swdptap_seq_out(0xe3ddafe9, 32);
	swd_proc.swdptap_seq_out(0x19bc0ea2, 32);
	/* 4 cycle low,
	 * 0x1a Arm CoreSight SW-DP activation sequence
	 * 20 bits start of reset another reset sequence*/
	swd_proc.swdptap_seq_out(0x1a0, 12);
	uint32_t idcode = 0;
	uint32_t target_id;
	bool is_v2 = true;
	if (!targetid) {
		/* Try to read ID */
		dp_line_reset();
		bool res = dp_read(ADIV5_DP_IDCODE, &idcode);
		if (res) {
			is_v2 = false;
			DEBUG_WARN("Trying old JTAG to SWD sequence\n");
			swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
			swd_proc.swdptap_seq_out(0xFFFFFFFF, 32);
			swd_proc.swdptap_seq_out(0xE79E, 16); /* 0b0111100111100111 */
			dp_line_reset();
			bool res = dp_read(ADIV5_DP_IDCODE, &idcode);
			if (res) {
				DEBUG_WARN("No usable DP found\n");
				return -1;
			}
		}
		if ((idcode & ADIV5_DP_VERSION_MASK) == ADIV5_DPv2) {
			is_v2 = true;
			bool res = dp_read(ADIV5_DP_TARGETID, &target_id);
			if (res) {
				DEBUG_WARN("Read Targetid failed\n");
				return -1;
			}
		} else {
			is_v2 = false;
		}
	} else {
		target_id = targetid;
	}
	int nr_dps = (is_v2) ? 16: 1;
	uint32_t dp_targetid;
	for (int i = 0; i < nr_dps; i++) {
		if (is_v2) {
			dp_line_reset();
			dp_targetid = (i << 28) | (target_id & 0x0fffffff);
			dp_write(ADIV5_DP_TARGETSEL, dp_targetid);
			bool res = dp_read(ADIV5_DP_IDCODE, &idcode);
			if (res)
				continue;
			if (dp_targetid == 0xf1002927) /* Fixme: Handle RP2040 rescue port */
				continue;
			DEBUG_WARN("DP %2d IDCODE %08" PRIx32 " TID 0x%08" PRIx32 "\n", i, idcode, dp_targetid);
		} else {
			dp_targetid = 0;
		}
		ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));
		if (!dp) {			/* calloc failed: heap exhaustion */
			DEBUG_WARN("calloc: failed in %s\n", __func__);
			continue;
		}
		dp->idcode = idcode;
		dp->targetid = dp_targetid;
#if HOSTED == 0
		dp->dp_read = firmware_swdp_read;
		dp->error = firmware_swdp_error;
		dp->low_access = firmware_swdp_low_access;
		dp->abort = firmware_swdp_abort;
		firmware_swdp_error(dp);
#else
		dp->dp_read = swd_proc->swdp_read;
		dp->error =  swd_proc->swdp_error;
		dp->low_access =  swd_proc->swdp_low_access;
		dp->abort =  swd_proc->swdp_abort;
		swd_proc->swdp_error();
#endif
		adiv5_dp_init(dp);

	}
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

	if ((dp->idcode & ADIV5_DP_VERSION_MASK) == ADIV5_DPv2) {
		/* On protocoll error target gets deselected.
		 * With DP Change, another target needs selection.
		 * => Reselect with right target! */
		dp_line_reset();
		dp_write(ADIV5_DP_TARGETSEL, dp->targetid);
		uint32_t dummy;
		dp_read(ADIV5_DP_IDCODE, &dummy);
	}
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
	uint32_t request = make_packet_request(RnW, addr);
	uint32_t response = 0;
	uint32_t ack;
	platform_timeout timeout;

	if((addr & ADIV5_APnDP) && dp->fault) return 0;

	platform_timeout_set(&timeout, 2000);
	do {
		swd_proc.swdptap_seq_out(request, 8);
		ack = swd_proc.swdptap_seq_in(3);
		if (ack == SWDP_ACK_FAULT) {
			/* On fault, abort() and repeat the command once.*/
			firmware_swdp_error(dp);
			swd_proc.swdptap_seq_out(request, 8);
			ack = swd_proc.swdptap_seq_in(3);
		}
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
		/* ARM Debug Interface Architecture Specification ADIv5.0 to ADIv5.2
		 * tells to clock the data through SW-DP to either :
		 * - immediate start a new transaction
		 * - continue to drive idle cycles
		 * - or clock at least 8 idle cycles
		 *
		 * Implement last option to favour correctness over
		 *   slight speed decrease
		 */
		swd_proc.swdptap_seq_out(0, 8);
	}
	return response;
}

void firmware_swdp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}
