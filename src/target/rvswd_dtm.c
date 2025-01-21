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
#include "platform.h"
#include "rvswd.h"
#include "riscv_debug.h"
#include "maths_utils.h"
#include "jep106.h"

// #define ENABLE_LONG_PACKET_PROBE 1

#define WCH_DTM_IDCODE 0x7fU /* Non-standard DM register which mirrors the target IDCODE register */

static void rvswd_wakeup_sequence(const bool stop_condition)
{
	/*
	 * A wakeup sequence consist of 100 CLK cycles with DIO high followed by a STOP condition
	 */
	DEBUG_INFO("Performing RVSWD wakeup sequence %s stop condition\n", stop_condition ? "with" : "without");

	platform_critical_enter();

	/* 100 CLK cycles with DIO high */
	rvswd_proc.seq_out(0xffffffffU, 32U); /* 32 */
	rvswd_proc.seq_out(0xffffffffU, 32U); /* 64 */
	rvswd_proc.seq_out(0xffffffffU, 32U); /* 96 */
	rvswd_proc.seq_out(0xffffffffU, 4U);  /* 100 */

	if (stop_condition)
		rvswd_proc.stop(); /* STOP condition */

	platform_critical_exit();
}

static bool rvswd_transfer_dmi_long(const uint8_t operation, const uint32_t address, const uint32_t value,
	uint32_t *const result, uint8_t *const status)
{
	/*
	 * RVSWD "Long" packet format:
	 *
	 * 
	 */
	// DEBUG_INFO("Performing RVSWD long DMI transfer: operation %d, address 0x%08" PRIx32 ", value 0x%08" PRIx32 "\n",
	// 	operation, address, value);

	/* RVSWD DTM Address space is limited to 7 bits */
	if (address & ~0x7fU) {
		DEBUG_ERROR("Address 0x%08" PRIx32 " is too large for RVSWD\n", address);
		return false;
	}

	/* TODO: This is just experimental code, it should packed into 32-bit words for efficiency */

	const uint8_t host_parity =
		calculate_odd_parity(address & 0x7fU) ^ calculate_odd_parity(value) ^ calculate_odd_parity(operation & 0x3U);

	platform_critical_enter();

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

	platform_critical_exit();

	/* Check parity */
	const uint8_t calculated_target_parity = calculate_odd_parity(target_address & 0x7fU) ^
		calculate_odd_parity(target_data) ^ calculate_odd_parity(target_status & 0x3U);

	if (target_parity != calculated_target_parity) {
		DEBUG_ERROR("Parity error in RVSWD long transfer: expected %u, got %u\n", (uint8_t)target_parity,
			(uint8_t)calculated_target_parity);
		return false;
	}

	// DEBUG_INFO("RVSWD long DMI transfer result: status %u, address 0x%08" PRIx32 ", value 0x%08" PRIx32 "\n",
	// 	(uint8_t)target_status, target_address, target_data);

	if (result)
		*result = target_data;
	if (status)
		*status = target_status;

	return true;
}

static bool rvswd_riscv_dmi_read_long(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	uint8_t status = 0;
	const bool result = rvswd_transfer_dmi_long(RV_DMI_OP_READ, address, 0, value, &status);

	/* Translate error 1 into RV_DMI_FAILURE per the spec, also write RV_DMI_FAILURE if the transfer failed */
	dmi->fault = !result || status == RV_DMI_RESERVED ? RV_DMI_FAILURE : status;
	return dmi->fault == RV_DMI_SUCCESS;
}

static bool rvswd_riscv_dmi_write_long(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	uint8_t status = 0;
	const bool result = rvswd_transfer_dmi_long(RV_DMI_OP_WRITE, address, value, NULL, &status);

	/* Translate error 1 into RV_DMI_FAILURE per the spec, also write RV_DMI_FAILURE if the transfer failed */
	dmi->fault = !result || status == RV_DMI_RESERVED ? RV_DMI_FAILURE : status;
	return dmi->fault == RV_DMI_SUCCESS;
}

static inline uint8_t rvswd_calculate_parity_short(const uint32_t value)
{
	/* Short packet parity is odd parity */
	const uint8_t parity = calculate_odd_parity(value);
	/* Short packet parity bit is duplicated for some strange reason */
	return parity << 1U | parity;
}

static bool rvswd_transfer_dmi_short(
	const bool write, const uint32_t address, const uint32_t value, uint32_t *const result, uint8_t *const status)
{
	/*
	 * RVSWD "Short" packet format:
	 *
	 * 
	 */
	// DEBUG_INFO("Performing RVSWD short DMI transfer: %s, address 0x%08" PRIx32 ", value 0x%08" PRIx32 "\n",
	// 	write ? "write" : "read", address, value);

	/* RVSWD DTM Address space is limited to 7 bits */
	if (address & ~0x7fU) {
		DEBUG_ERROR("Address 0x%08" PRIx32 " is too large for RVSWD\n", address);
		return false;
	}

	/* TODO: This is just experimental code, it should packed into 32-bit words for efficiency */

	/* Host parity */
	const uint8_t host_parity = rvswd_calculate_parity_short(((address & 0x7fU) << 1U) | (uint32_t)write);
	uint32_t data_parity = write ? rvswd_calculate_parity_short(value) : 0U;

	platform_critical_enter();

	/* Start condition */
	rvswd_proc.start();

	/* Host address */
	rvswd_proc.seq_out(address & 0x7fU, 7U);

	/* Operation */
	rvswd_proc.seq_out(write, 1U);

	/* Host parity */
	rvswd_proc.seq_out(host_parity, 2U);

	/* 4 zero (padding?) bits */
	rvswd_proc.seq_out(0U, 4U);

	if (write) {
		/* Host data */
		rvswd_proc.seq_out(value, 32U);

		/* Host data parity */
		rvswd_proc.seq_out(data_parity, 2U);
	} else {
		/* Target data */
		*result = rvswd_proc.seq_in(32U);

		/* Target parity */
		data_parity = rvswd_proc.seq_in(2U) & 0x3U;
	}

	/* Status (4 bits? also duplicated?) seems more like last 2 bits are padding 1 */
	const uint32_t raw_status = rvswd_proc.seq_in(4U) & 0xfU;

	/* Stop condition */
	rvswd_proc.stop();

	platform_critical_exit();

	/* Check parity */
	if (!write) {
		const uint8_t calculated_data_parity = rvswd_calculate_parity_short(*result);
		if (data_parity != calculated_data_parity) {
			DEBUG_ERROR("Parity error in RVSWD short transfer: expected %u, got %u\n", (uint8_t)data_parity,
				(uint8_t)calculated_data_parity);
			return false;
		}
	}

	/* discard the padding bits */
	*status = raw_status >> 2U;

	// DEBUG_INFO("RVSWD short DMI transfer result: status %u (raw %u), address 0x%08" PRIx32 ", value 0x%08" PRIx32 "\n",
	// 	(uint8_t)*status, (uint8_t)raw_status, address, write ? value : *result);

	return true;
}

static bool rvswd_riscv_dmi_read_short(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	uint8_t status = 0;
	const bool result = rvswd_transfer_dmi_short(false, address, 0, value, &status);

	/* Translate error 1 into RV_DMI_FAILURE per the spec, also write RV_DMI_FAILURE if the transfer failed */
	dmi->fault = !result || status == RV_DMI_RESERVED ? RV_DMI_FAILURE : status;
	return dmi->fault == RV_DMI_SUCCESS;
}

static bool rvswd_riscv_dmi_write_short(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	uint8_t status = 0;
	const bool result = rvswd_transfer_dmi_short(true, address, value, NULL, &status);

	/* Translate error 1 into RV_DMI_FAILURE per the spec, also write RV_DMI_FAILURE if the transfer failed */
	dmi->fault = !result || status == RV_DMI_RESERVED ? RV_DMI_FAILURE : status;
	return dmi->fault == RV_DMI_SUCCESS;
}

static void rvswd_riscv_dtm_init(riscv_dmi_s *const dmi, const bool short_packets)
{
	/* WCH-Link doesn't have any mechanism to identify the DTM manufacturer, so we'll just assume it's WCH */
	dmi->designer_code = NOT_JEP106_MANUFACTURER_WCH;

	dmi->version = RISCV_DEBUG_UNSPECIFIED; /* Not available, compatible with version 0.13 */

	/* WCH-Link RVSWD has a fixed address width of 7 bits */
	dmi->address_width = 7U;

	if (short_packets) {
		dmi->read = rvswd_riscv_dmi_read_short;
		dmi->write = rvswd_riscv_dmi_write_short;
	} else {
		dmi->read = rvswd_riscv_dmi_read_long;
		dmi->write = rvswd_riscv_dmi_write_long;
	}

	riscv_dmi_init(dmi);
}

static void riscv_rvswd_dtm_handler(const bool short_packets)
{
	riscv_dmi_s *const dmi = calloc(1, sizeof(*dmi));
	if (!dmi) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	rvswd_riscv_dtm_init(dmi, short_packets);
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
	rvswd_wakeup_sequence(true);

#ifdef ENABLE_LONG_PACKET_PROBE
	/* Look for a DTM with long packets */
	DEBUG_INFO("Scanning for RISC-V DTM with RVSWD long packets\n");
	/* WCH-Link attempts 202 times, we can probably do less */
	for (size_t i = 0; i < 202U; i++) {
		/* Read the DTM status register */
		uint8_t status = 0U;
		uint32_t value = 0U;
		if (!rvswd_transfer_dmi_long(RV_DMI_READ, RV_DM_STATUS, 0, &value, &status) || status != RV_DMI_SUCCESS)
			continue;

		/* A successful read of the status register means we found a DTM, probably? */
		if (value != 0U && value != 0xffffffffU) {
			/* Delegate to the RISC-V DTM handler */
			riscv_rvswd_dtm_handler(false);
			return true;
		}
	}
#endif

	/* Run the short packet wakeup sequence */
	rvswd_wakeup_sequence(false);

	/* Look for a DTM with short packets */
	DEBUG_INFO("Scanning for RISC-V DTM with RVSWD short packets\n");
	for (size_t i = 0; i < 10U; i++) {
		/* Enable the DM */
		/* TODO: verify the spec, is the STATUS register available in all states? if so use it */
		uint8_t status = 0U;
		if (!rvswd_transfer_dmi_short(true, RV_DM_CONTROL, RV_DM_CTRL_ACTIVE, NULL, &status) ||
			status != RV_DMI_SUCCESS)
			continue;
		if (!rvswd_transfer_dmi_short(true, RV_DM_CONTROL, RV_DM_CTRL_ACTIVE, NULL, &status) ||
			status != RV_DMI_SUCCESS)
			continue;

		/* Read the WCH IDCODE register */
		uint32_t idcode = 0U;
		if (!rvswd_transfer_dmi_short(false, WCH_DTM_IDCODE, 0U, &idcode, &status) || status != RV_DMI_SUCCESS)
			continue;

		/* A successful read of the IDCODE register means we found a DTM, probably */
		if (idcode != 0U && idcode != 0xffffffffU) {
			/* Put the DM back into reset so it's in a known good state */
			(void)rvswd_transfer_dmi_short(true, RV_DM_CONTROL, 0U, NULL, &status);

			/* Delegate to the RISC-V DTM handler */
			riscv_rvswd_dtm_handler(true);
			return true;
		}
	}

	return false;
}
