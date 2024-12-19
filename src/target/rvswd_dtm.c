/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* This file implements RVSWD protocol support. TODO */

#include "general.h"
#include "rvswd.h"
#include "riscv_debug.h"
#include "maths_utils.h"
#include "jep106.h"

static void rvswd_wakeup_sequence(void)
{
	/*
	 * A wakeup sequence consist of 100 CLK cycles with DIO high followed by a STOP condition
	 */
	DEBUG_INFO("Performing RVSWD wakeup sequence\n");

	/* 100 CLK cycles with DIO high */
	for (size_t i = 0U; i < 100U; i += 32U) {
		const size_t cycles = 100U - i;                    /* Remaining cycles */
		rvswd_proc.seq_out(0xffffffffU, MAX(cycles, 32U)); /* Up to 32 cycles HIGH */
	}
	rvswd_proc.stop(); /* STOP condition */
}

static bool rvswd_transfer_dmi(const uint8_t operation, const uint32_t address, const uint32_t value,
	uint32_t *const result, uint8_t *const status)
{
	/*
	 * RVSWD "Long" packet format:
	 *
	 * 
	 */
	DEBUG_INFO("Performing RVSWD DMI transfer: operation %d, address 0x%08" PRIx32 ", value 0x%08" PRIx32 "\n",
		operation, address, value);

	/* Address limited to 7 bits for now, TODO: check if RVSWD supports more somehow */
	if (address & ~0x7fU) {
		DEBUG_ERROR("Address 0x%08" PRIx32 " is too large for RVSWD\n", address);
		return false;
	}

	/* TODO: This is just experimental code, it should packed into 32-bit words for efficiency */

	const uint8_t host_parity =
		calculate_odd_parity(address & 0x7fU) ^ calculate_odd_parity(value) ^ calculate_odd_parity(operation & 0x3U);

	/* Start condition */
	rvswd_proc.start();

	/* Host address */
	rvswd_proc.seq_out(address & 0x7fU, 7U);

	/* Host data */
	rvswd_proc.seq_out(value, 32U);

	/* Operation */
	rvswd_proc.seq_out(operation & 0x3U, 2U);

	/* Host parity */
	rvswd_proc.seq_out(host_parity, 1U);

	/* Target address */
	const uint32_t target_address = rvswd_proc.seq_in(7U);

	/* Target data */
	const uint32_t target_data = rvswd_proc.seq_in(32U);

	/* Status */
	const uint32_t target_status = rvswd_proc.seq_in(2U);

	/* Target parity */
	const uint32_t target_parity = rvswd_proc.seq_in(1U);

	/* Stop condition */
	rvswd_proc.stop();

	/* Check parity */
	const uint8_t calculated_target_parity = calculate_odd_parity(target_address & 0x7fU) ^
		calculate_odd_parity(target_data) ^ calculate_odd_parity(target_status & 0x3U);

	if (target_parity != calculated_target_parity) {
		DEBUG_ERROR("Parity error in RVSWD transfer: expected %u, got %u\n", (uint8_t)target_parity,
			(uint8_t)calculated_target_parity);
		// return false;
	}

	DEBUG_INFO("RVSWD DMI transfer result: status %u, address 0x%08" PRIx32 ", value 0x%08" PRIx32 "\n",
		(uint8_t)target_status, target_address, target_data);

	if (result)
		*result = target_data;
	if (status)
		*status = target_status;

	return true;
}

static bool rvswd_riscv_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	uint8_t status = 0;
	const bool result = rvswd_transfer_dmi(RV_DMI_READ, address, 0, value, &status);

	/* Translate error 1 into RV_DMI_FAILURE per the spec, also write RV_DMI_FAILURE if the transfer failed */
	dmi->fault = !result || status == 1U ? RV_DMI_FAILURE : status;
	return dmi->fault == RV_DMI_SUCCESS;
}

static bool rvswd_riscv_dmi_write(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	uint8_t status = 0;
	const bool result = rvswd_transfer_dmi(RV_DMI_WRITE, address, value, NULL, &status);

	/* Translate error 1 into RV_DMI_FAILURE per the spec, also write RV_DMI_FAILURE if the transfer failed */
	dmi->fault = !result || status == 1U ? RV_DMI_FAILURE : status;
	return dmi->fault == RV_DMI_SUCCESS;
}

static void rvswd_riscv_dtm_init(riscv_dmi_s *const dmi)
{
	/* WCH-Link doesn't have any mechanism to identify the DTM manufacturer, so we'll just assume it's WCH */
	dmi->designer_code = NOT_JEP106_MANUFACTURER_WCH;

	dmi->version = RISCV_DEBUG_0_13; /* Assumption, unverified */

	/* WCH-Link has a fixed address width of 8 bits, limited by the USB protocol (is RVSWD also fixed?) */
	dmi->address_width = 8U;

	dmi->read = rvswd_riscv_dmi_read;
	dmi->write = rvswd_riscv_dmi_write;

	riscv_dmi_init(dmi);
}

static void riscv_rvswd_dtm_handler(void)
{
	riscv_dmi_s *dmi = calloc(1, sizeof(*dmi));
	if (!dmi) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	rvswd_riscv_dtm_init(dmi);
	/* If we failed to find any DMs or Harts, free the structure */
	if (!dmi->ref_count)
		free(dmi);
}

bool rvswd_scan(void)
{
	/* Free the device list if any, and clean state ready */
	target_list_free();

#if CONFIG_BMDA == 0
	rvswd_init();
#endif

	platform_target_clk_output_enable(true);

	/* Run the wakeup sequence */
	rvswd_wakeup_sequence();

	/* Delegate to the RISC-V DTM handler */
	riscv_rvswd_dtm_handler();

	return false;
}
