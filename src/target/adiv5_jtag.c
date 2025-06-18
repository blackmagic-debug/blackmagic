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

/*
 * This file implements the JTAG-DP specific functions of the
 * ARM Debug Interface v5 Architecture Specification, ARM doc IHI0031A.
 */

#include "general.h"
#include "exception.h"
#include "jep106.h"
#include "adiv5.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "morse.h"

#define JTAG_ACK_WAIT        0x01U
#define JTAG_ADIv5_ACK_OK    0x02U
#define JTAG_ADIv6_ACK_FAULT 0x02U
#define JTAG_ADIv6_ACK_OK    0x04U

/* 35-bit registers that control the ADIv5 DP */
#define IR_ABORT 0x8U
#define IR_DPACC 0xaU
#define IR_APACC 0xbU

void adiv5_jtag_dp_handler(const uint8_t dev_index)
{
	adiv5_debug_port_s *dp = calloc(1, sizeof(*dp));
	if (!dp) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	dp->dev_index = dev_index;

	dp->dp_read = adiv5_jtag_read;
	dp->low_access = adiv5_jtag_raw_access;
	dp->error = adiv5_jtag_clear_error;
	dp->abort = adiv5_jtag_abort;
	dp->ensure_idle = adiv5_jtag_ensure_idle;
#if CONFIG_BMDA == 1
	bmda_jtag_dp_init(dp);
#endif

	/* Grab the ID code that was scanned */
	const uint32_t idcode = jtag_devs[dev_index].jd_idcode;
	/*
	 * Pulling out the designer code which will be used to attempt to detect a DPv0 DP.
	 * This will get overridden later by DPIDR if the DP turns out to be DPv1+.
	 */
	const uint16_t designer = (idcode & JTAG_IDCODE_DESIGNER_MASK) >> JTAG_IDCODE_DESIGNER_OFFSET;
	/*
	 * Now extract the part number and sort out the designer code.
	 * The JTAG ID code designer is in the form:
	 * Bits 10:7 - JEP-106 Continuation Code
	 * Bits 6:0 - JEP-106 Identity Code
	 * So here we convert that into our internal representation.
	 * See the JEP-106 code list (jep106.h) for more on that.
	 */
	dp->designer_code =
		((designer & ADIV5_DP_DESIGNER_JEP106_CONT_MASK) << 1U) | (designer & ADIV5_DP_DESIGNER_JEP106_CODE_MASK);
	dp->partno = (idcode & JTAG_IDCODE_PARTNO_MASK) >> JTAG_IDCODE_PARTNO_OFFSET;

	/* Check which version of DP we have here, if it's an ARM-made DP, and set up `dp->version` accordingly */
	if (dp->designer_code == JEP106_MANUFACTURER_ARM) {
		if (dp->partno == JTAG_IDCODE_PARTNO_SOC400_4BIT || dp->partno == JTAG_IDCODE_PARTNO_SOC400_8BIT)
			dp->version = 0U;
		else if (dp->partno == JTAG_IDCODE_PARTNO_SOC400_4BIT_CM33 ||
			dp->partno == JTAG_IDCODE_PARTNO_SOC400_4BIT_LPC43xx)
			dp->version = 1U;
		else if (dp->partno == JTAG_IDCODE_PARTNO_SOC600_4BIT || dp->partno == JTAG_IDCODE_PARTNO_SOC600_8BIT)
			dp->version = 3U;
		else
			DEBUG_WARN("Unknown JTAG-DP found, please report partno code %04x\n", dp->partno);
	}
	dp->quirks |= ADIV5_DP_JTAG;

	if (dp->version == 0)
		adiv5_dp_error(dp);
	else
		adiv5_dp_abort(dp, ADIV5_DP_ABORT_STKERRCLR);
	adiv5_dp_init(dp);
}

uint32_t adiv5_jtag_read(adiv5_debug_port_s *dp, uint16_t addr)
{
	adiv5_jtag_raw_access(dp, ADIV5_LOW_READ, addr, 0);
	return adiv5_jtag_raw_access(dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
}

uint32_t adiv5_jtag_clear_error(adiv5_debug_port_s *dp, const bool protocol_recovery)
{
	(void)protocol_recovery;
	const uint32_t status = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) & ADIV5_DP_CTRLSTAT_ERRMASK;
	dp->fault = 0;
	return adiv5_dp_low_access(dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, status) & 0x32U;
}

uint32_t adiv5_jtag_raw_access(
	adiv5_debug_port_s *const dp, const uint8_t rnw, const uint16_t addr, const uint32_t value)
{
	const uint8_t reg = addr & 0x0cU;
	const uint64_t request = ((uint64_t)value << 3U) | (reg >> 1U) | (rnw ? 1U : 0U);

	uint32_t result;
	uint8_t ack;

	/* Set the instruction to the correct one for the kind of access needed */
	jtag_dev_write_ir(dp->dev_index, (addr & ADIV5_APnDP) ? IR_APACC : IR_DPACC);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	do {
		uint64_t response;
		/* Send the request and see what response we get back */
		jtag_dev_shift_dr(dp->dev_index, (uint8_t *)&response, (const uint8_t *)&request, 35);
		/* Extract the data portion of the response */
		result = (uint32_t)(response >> 3U);
		/* Then the acknowledgement code */
		ack = (uint8_t)(response & 0x07U);
	} while (!platform_timeout_is_expired(&timeout) && ack == JTAG_ACK_WAIT);

	/*
	 * If even after waiting for the 250ms we still get a WAIT response,
	 * we're done - abort the request, mark it failed.
	 */
	if (ack == JTAG_ACK_WAIT) {
		DEBUG_ERROR("JTAG access resulted in wait, aborting\n");
		dp->abort(dp, ADIV5_DP_ABORT_DAPABORT);
		/* Use the SWD ack codes for the fault code to be completely consistent between JTAG-vs-SWD */
		dp->fault = SWD_ACK_WAIT;
		return 0;
	}

	/* If this is an ADIv6 JTAG-DPv1, check for fault */
	if (dp->version > 2 && ack == JTAG_ADIv6_ACK_FAULT) {
		DEBUG_ERROR("JTAG access resulted in fault\n");
		/* Use the SWD ack codes for the fault code to be completely consistent between JTAG-vs-SWD */
		dp->fault = SWD_ACK_FAULT;
		return 0;
	}

	/* Check for a not-OK ack under ADIv5 JTAG-DPv0, or ADIv6 JTAG-DPv1 */
	if ((dp->version < 3 && ack != JTAG_ADIv5_ACK_OK) || (dp->version > 2 && ack != JTAG_ADIv6_ACK_OK)) {
		DEBUG_ERROR("JTAG access resulted in: %" PRIx32 ":%x\n", result, ack);
		raise_exception(EXCEPTION_ERROR, "JTAG-DP invalid ACK");
	}

	/* ADIv6 needs 8 idle cycles run after we get done to ensure the state machine is idle */
	if (dp->version > 2)
		jtag_proc.jtagtap_cycle(false, false, 8);
	return result;
}

void adiv5_jtag_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	uint64_t request = (uint64_t)abort << 3U;
	jtag_dev_write_ir(dp->dev_index, IR_ABORT);
	jtag_dev_shift_dr(dp->dev_index, NULL, (const uint8_t *)&request, 35);
}

void adiv5_jtag_ensure_idle(adiv5_debug_port_s *dp)
{
	/*
	 * On devices where nRST pulls TRST, the JTAG-DP's IR is reset
	 * from DPACC/APACC to IDCODE. We want BYPASS in case of daisy-chaining.
	 */
	jtag_devs[dp->dev_index].current_ir = 0xffU;
	/* Go from TLR to RTI. */
	jtagtap_return_idle(1);
}
