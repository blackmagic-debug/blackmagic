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
 * ARM Debug Interface v5 Architecture Specification, ARM doc IHI0031A.
 */

#include "general.h"
#include "exception.h"
#include "adiv5.h"
#include "swd.h"
#include "target.h"
#include "target_internal.h"

uint8_t make_packet_request(uint8_t RnW, uint16_t addr)
{
	bool APnDP = addr & ADIV5_APnDP;

	addr &= 0xffU;

	uint8_t request = 0x81U; /* Park and Startbit */

	if (APnDP)
		request ^= 0x22U;
	if (RnW)
		request ^= 0x24U;

	addr &= 0xcU;
	request |= (addr << 1U) & 0x18U;
	if (addr == 4U || addr == 8U)
		request ^= 0x20U;

	return request;
}

/* Provide bare DP access functions without timeout and exception */

static void swd_line_reset(void)
{
	swd_proc.seq_out(0xffffffffU, 32U);
	swd_proc.seq_out(0x0fffffffU, 32U);
}

bool firmware_dp_low_write(const uint16_t addr, const uint32_t data)
{
	const uint8_t request = make_packet_request(ADIV5_LOW_WRITE, addr);
	swd_proc.seq_out(request, 8);
	const uint8_t res = swd_proc.seq_in(3);
	swd_proc.seq_out_parity(data, 32);
	swd_proc.seq_out(0, 8);
	return res != SWDP_ACK_OK;
}

static uint32_t firmware_dp_low_read(const uint16_t addr)
{
	const uint8_t request = make_packet_request(ADIV5_LOW_READ, addr);
	swd_proc.seq_out(request, 8);
	const uint8_t res = swd_proc.seq_in(3);
	uint32_t data = 0;
	swd_proc.seq_in_parity(&data, 32);
	return res == SWDP_ACK_OK ? data : 0;
}

/* Try first the dormant to SWD procedure.
 * If target id given, scan DPs 0 .. 15 on that device and return.
 * Otherwise
 */
uint32_t adiv5_swdp_scan(uint32_t targetid)
{
	volatile exception_s e;

	target_list_free();

	adiv5_debug_port_s idp = {
		.dp_low_write = firmware_dp_low_write,
		.error = firmware_swdp_error,
		.dp_read = firmware_swdp_read,
		.low_access = firmware_swdp_low_access,
		.abort = firmware_swdp_abort,
	};
	adiv5_debug_port_s *initial_dp = &idp;

#if PC_HOSTED == 0
	swdptap_init();
#else
	if (!platform_swdptap_init(initial_dp))
		return 0;
#endif

	platform_target_clk_output_enable(true);
	/* DORMANT-> SWD sequence*/
	swd_proc.seq_out(0xffffffff, 32);
	swd_proc.seq_out(0xffffffff, 32);
	/* 128 bit selection alert sequence for SW-DP-V2 */
	swd_proc.seq_out(0x6209f392, 32);
	swd_proc.seq_out(0x86852d95, 32);
	swd_proc.seq_out(0xe3ddafe9, 32);
	swd_proc.seq_out(0x19bc0ea2, 32);
	/* 4 cycle low,
	 * 0x1a Arm CoreSight SW-DP activation sequence
	 * 20 bits start of reset another reset sequence*/
	swd_proc.seq_out(0x1a0, 12);

	bool scan_multidrop = true;
	volatile uint32_t dp_targetid = targetid;

	if (!dp_targetid) {
		/* No targetID given on the command line Try to read ID */

		scan_multidrop = false;

		swd_line_reset();

		volatile uint32_t dp_dpidr = 0;
		TRY_CATCH (e, EXCEPTION_ALL) {
			dp_dpidr = initial_dp->dp_read(initial_dp, ADIV5_DP_DPIDR);
		}
		if (e.type || initial_dp->fault) {
			DEBUG_WARN("Trying old JTAG to SWD sequence\n");
			swd_proc.seq_out(0xffffffff, 32);
			swd_proc.seq_out(0xffffffff, 32);
			swd_proc.seq_out(0xe79e, 16); /* 0b0111100111100111 */

			swd_line_reset();

			initial_dp->fault = 0;

			TRY_CATCH (e, EXCEPTION_ALL) {
				dp_dpidr = initial_dp->dp_read(initial_dp, ADIV5_DP_DPIDR);
			}
			if (e.type || initial_dp->fault) {
				DEBUG_ERROR("No usable DP found\n");
				return 0;
			}
		}

		const uint8_t dp_version = (dp_dpidr & ADIV5_DP_DPIDR_VERSION_MASK) >> ADIV5_DP_DPIDR_VERSION_OFFSET;
		if (dp_version >= 2) {
			scan_multidrop = true;
			// initial_dp must have the version field set so adiv5_dp_read() does protocol recovery correctly.
			initial_dp->version = dp_version;

			/* Read TargetID. Can be done with device in WFI, sleep or reset! */
			/* TARGETID is on bank 2 */
			adiv5_dp_write(initial_dp, ADIV5_DP_SELECT, ADIV5_DP_BANK2);
			dp_targetid = adiv5_dp_read(initial_dp, ADIV5_DP_TARGETID);
			adiv5_dp_write(initial_dp, ADIV5_DP_SELECT, ADIV5_DP_BANK0);
		}
	}

#if PC_HOSTED == 1
	if (!initial_dp->dp_low_write) {
		DEBUG_WARN("CMSIS_DAP < v1.2 can not handle multi-drop, disabling\n");
		scan_multidrop = false;
	}
#endif

	DEBUG_WARN("scan_multidrop: %s\n", scan_multidrop ? "true" : "false");

	const volatile size_t max_dp = scan_multidrop ? 16U : 1U;
	for (volatile size_t i = 0; i < max_dp; i++) {
		if (scan_multidrop) {
			initial_dp->fault = 0;
			swd_line_reset();

			initial_dp->dp_low_write(ADIV5_DP_TARGETSEL,
				i << ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET |
					(dp_targetid & (ADIV5_DP_TARGETID_TDESIGNER_MASK | ADIV5_DP_TARGETID_TPARTNO_MASK)) | 1U);

			volatile uint32_t target_id = 0;
			TRY_CATCH (e, EXCEPTION_ALL) {
				target_id = adiv5_dp_read(initial_dp, ADIV5_DP_DPIDR);
			}
			if (e.type || initial_dp->fault || !target_id)
				continue;
		}

		adiv5_debug_port_s *dp = calloc(1, sizeof(*dp));
		if (!dp) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			continue;
		}

		memcpy(dp, initial_dp, sizeof(adiv5_debug_port_s));
		dp->instance = i;

		adiv5_dp_abort(dp, ADIV5_DP_ABORT_STKERRCLR);
		adiv5_dp_init(dp, 0);
	}
	return target_list ? 1U : 0U;
}

uint32_t firmware_swdp_read(adiv5_debug_port_s *dp, uint16_t addr)
{
	if (addr & ADIV5_APnDP) {
		adiv5_dp_recoverable_access(dp, ADIV5_LOW_READ, addr, 0);
		return adiv5_dp_low_access(dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
	}
	return adiv5_dp_recoverable_access(dp, ADIV5_LOW_READ, addr, 0);
}

uint32_t firmware_swdp_error(adiv5_debug_port_s *dp, const bool protocol_recovery)
{
	/* Only do the comms reset dance on DPv2+ w/ fault or to perform protocol recovery. */
	if ((dp->version >= 2 && dp->fault) || protocol_recovery) {
		/*
		 * Note that on DPv2+ devices, during a protocol error condition
		 * the target becomes deselected during line reset. Once reset,
		 * we must then re-select the target to bring the device back
		 * into the expected state.
		 */
		swd_line_reset();
		if (dp->version >= 2)
			firmware_dp_low_write(ADIV5_DP_TARGETSEL, dp->targetsel);
		firmware_dp_low_read(ADIV5_DP_DPIDR);
		/* Exception here is unexpected, so do not catch */
	}
	const uint32_t err = firmware_dp_low_read(ADIV5_DP_CTRLSTAT) &
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

	if (clr)
		firmware_dp_low_write(ADIV5_DP_ABORT, clr);
	dp->fault = 0;
	return err;
}

uint32_t firmware_swdp_low_access(adiv5_debug_port_s *dp, const uint8_t RnW, const uint16_t addr, const uint32_t value)
{
	if ((addr & ADIV5_APnDP) && dp->fault)
		return 0;

	const uint8_t request = make_packet_request(RnW, addr);
	uint32_t response = 0;
	uint8_t ack = SWDP_ACK_WAIT;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	do {
		swd_proc.seq_out(request, 8);
		ack = swd_proc.seq_in(3);
		if (ack == SWDP_ACK_FAULT) {
			DEBUG_ERROR("SWD access resulted in fault, retrying\n");
			/* On fault, abort the request and repeat */
			/* Yes, this is self-recursive.. no, we can't think of a better option */
			adiv5_dp_write(dp, ADIV5_DP_ABORT,
				ADIV5_DP_ABORT_ORUNERRCLR | ADIV5_DP_ABORT_WDERRCLR | ADIV5_DP_ABORT_STKERRCLR |
					ADIV5_DP_ABORT_STKCMPCLR);
		}
	} while ((ack == SWDP_ACK_WAIT || ack == SWDP_ACK_FAULT) && !platform_timeout_is_expired(&timeout));

	if (ack == SWDP_ACK_WAIT) {
		DEBUG_ERROR("SWD access resulted in wait, aborting\n");
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

	if (RnW) {
		if (swd_proc.seq_in_parity(&response, 32)) { /* Give up on parity error */
			dp->fault = 1;
			DEBUG_ERROR("SWD access resulted in parity error\n");
			raise_exception(EXCEPTION_ERROR, "SWD parity error");
		}
	} else {
		swd_proc.seq_out_parity(value, 32);
		/* ARM Debug Interface Architecture Specification ADIv5.0 to ADIv5.2
		 * tells to clock the data through SW-DP to either :
		 * - immediate start a new transaction
		 * - continue to drive idle cycles
		 * - or clock at least 8 idle cycles
		 *
		 * Implement last option to favour correctness over
		 *   slight speed decrease
		 */
		swd_proc.seq_out(0, 8);
	}
	return response;
}

void firmware_swdp_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	adiv5_dp_write(dp, ADIV5_DP_ABORT, abort);
}
