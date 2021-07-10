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
#include "target.h"
#include "target_internal.h"

unsigned int make_packet_request(uint8_t RnW, uint16_t addr)
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

static void dp_line_reset(ADIv5_DP_t *dp)
{
	dp->seq_out(0xFFFFFFFF, 32);
	dp->seq_out(0x0FFFFFFF, 32);
}

bool firmware_dp_low_write(ADIv5_DP_t *dp, uint16_t addr, const uint32_t data)
{
	unsigned int request =  make_packet_request(ADIV5_LOW_WRITE, addr & 0xf);
	dp->seq_out(request, 8);
	int res = dp->seq_in(3);
	dp->seq_out_parity(data, 32);
	return (res != 1);
}

static bool firmware_dp_low_read(ADIv5_DP_t *dp, uint16_t addr, uint32_t *res)
{
	unsigned int request =  make_packet_request(ADIV5_LOW_READ, addr & 0xf);
	dp->seq_out(request, 8);
	dp->seq_in(3);
    return dp->seq_in_parity(res, 32);
}

/* Try first the dormant to SWD procedure.
 * If target id given, scan DPs 0 .. 15 on that device and return.
 * Otherwise
 */
int adiv5_swdp_scan(uint32_t targetid)
{
	target_list_free();
	ADIv5_DP_t idp, *initial_dp = &idp;
	memset(initial_dp, 0, sizeof(ADIv5_DP_t));
	if (swdptap_init(initial_dp))
		return -1;
	/* Set defaults when no procedure given*/
	if (!initial_dp->dp_low_write)
		initial_dp->dp_low_write = firmware_dp_low_write;
	if (!initial_dp->dp_low_read)
		initial_dp->dp_low_read = firmware_dp_low_read;
	if (!initial_dp->error)
		initial_dp->error = firmware_swdp_error;
	if (!initial_dp->dp_read)
		initial_dp->dp_read = firmware_swdp_read;
	if (!initial_dp->error)
		initial_dp->error = firmware_swdp_error;
	if (!initial_dp->low_access)
       initial_dp->low_access = firmware_swdp_low_access;
	if (!initial_dp->abort)
       initial_dp->abort = firmware_swdp_abort;
	/* DORMANT-> SWD sequence*/
	initial_dp->seq_out(0xFFFFFFFF, 32);
	initial_dp->seq_out(0xFFFFFFFF, 32);
	/* 128 bit selection alert sequence for SW-DP-V2 */
	initial_dp->seq_out(0x6209f392, 32);
	initial_dp->seq_out(0x86852d95, 32);
	initial_dp->seq_out(0xe3ddafe9, 32);
	initial_dp->seq_out(0x19bc0ea2, 32);
	/* 4 cycle low,
	 * 0x1a Arm CoreSight SW-DP activation sequence
	 * 20 bits start of reset another reset sequence*/
	initial_dp->seq_out(0x1a0, 12);
	uint32_t idcode = 0;
	volatile uint32_t target_id;
	bool is_v2 = true;
	if (!targetid || (initial_dp->error != firmware_swdp_error)) {
		/* No targetID given on the command line or probe can not
		 * handle multi-drop. Try to read ID */
		dp_line_reset(initial_dp);
		volatile struct exception e;
		TRY_CATCH (e, EXCEPTION_ALL) {
			idcode = initial_dp->low_access(initial_dp, ADIV5_LOW_READ,
										   ADIV5_DP_IDCODE, 0);
		}
		if (e.type || initial_dp->fault) {
			is_v2 = false;
			DEBUG_WARN("Trying old JTAG to SWD sequence\n");
			initial_dp->seq_out(0xFFFFFFFF, 32);
			initial_dp->seq_out(0xFFFFFFFF, 32);
			initial_dp->seq_out(0xE79E, 16); /* 0b0111100111100111 */
			dp_line_reset(initial_dp);
			initial_dp->fault = 0;
			volatile struct exception e2;
			TRY_CATCH (e2, EXCEPTION_ALL) {
				idcode = initial_dp->low_access(initial_dp, ADIV5_LOW_READ,
									  ADIV5_DP_IDCODE, 0);
			}
			if (e2.type) {
				DEBUG_WARN("No usable DP found\n");
				return -1;
			}
		}
		if ((idcode & ADIV5_DP_VERSION_MASK) == ADIV5_DPv2) {
			is_v2 = true;
			/* Read TargetID. Can be done with device in WFI, sleep or reset!*/
			adiv5_dp_write(initial_dp, ADIV5_DP_SELECT, 2);
			target_id = adiv5_dp_read(initial_dp, ADIV5_DP_CTRLSTAT);
			adiv5_dp_write(initial_dp, ADIV5_DP_SELECT, 0);
			DEBUG_INFO("TARGETID %08" PRIx32 "\n", target_id);
			switch (target_id) {
			case 0x01002927: /* RP2040 */
				/* Release evt. handing RESCUE DP reset*/
				adiv5_dp_write(initial_dp, ADIV5_DP_CTRLSTAT, 0);
				break;
			}
			if (initial_dp->error != firmware_swdp_error) {
				DEBUG_WARN("CMSIS_DAP < V1.2 can not handle multi-drop!\n");
				/* E.g. CMSIS_DAP < V1.2 can not handle multi-drop!*/
				is_v2 = false;
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
			dp_line_reset(initial_dp);
			dp_targetid = (i << 28) | (target_id & 0x0fffffff);
			initial_dp->dp_low_write(initial_dp, ADIV5_DP_TARGETSEL,
									dp_targetid);
			if (initial_dp->dp_low_read(initial_dp, ADIV5_DP_IDCODE,
										&idcode)) {
				continue;
			}
		} else {
			dp_targetid = 0;
		}
		ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));
		if (!dp) {			/* calloc failed: heap exhaustion */
			DEBUG_WARN("calloc: failed in %s\n", __func__);
			continue;
		}
		memcpy(dp, initial_dp, sizeof(ADIv5_DP_t));
		dp->idcode = idcode;
		dp->targetid = dp_targetid;
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
		dp_line_reset(dp);
		dp->dp_low_write(dp, ADIV5_DP_TARGETSEL, dp->targetid);
		uint32_t dummy;
		dp->dp_low_read(dp, ADIV5_DP_IDCODE, &dummy);
	}
	err = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) &
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

	platform_timeout_set(&timeout, 20);
	do {
		dp->seq_out(request, 8);
		ack = dp->seq_in(3);
		if (ack == SWDP_ACK_FAULT) {
			dp->fault = 1;
			return 0;
		}
	} while (ack == SWDP_ACK_WAIT && !platform_timeout_is_expired(&timeout));

	if (ack == SWDP_ACK_WAIT) {
		dp->abort(dp, ADIV5_DP_ABORT_DAPABORT);
		dp->fault = 1;
		return 0;
	}

	if(ack == SWDP_ACK_FAULT) {
		dp->fault = 1;
		return 0;
	}

	if(ack != SWDP_ACK_OK)
		raise_exception(EXCEPTION_ERROR, "SWDP invalid ACK");

	if(RnW) {
		if(dp->seq_in_parity(&response, 32))  /* Give up on parity error */
			raise_exception(EXCEPTION_ERROR, "SWDP Parity error");
	} else {
		dp->seq_out_parity(value, 32);
		/* ARM Debug Interface Architecture Specification ADIv5.0 to ADIv5.2
		 * tells to clock the data through SW-DP to either :
		 * - immediate start a new transaction
		 * - continue to drive idle cycles
		 * - or clock at least 8 idle cycles
		 *
		 * Implement last option to favour correctness over
		 *   slight speed decrease
		 */
	dp->seq_out(0, 8);
	}
	return response;
}

void firmware_swdp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}
