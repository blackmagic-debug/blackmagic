/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#include "general.h"
#include "riscv_debug.h"

#define RV_DM_CONTROL 0x10U
#define RV_DM_STATUS  0x11U
#define RV_DM_NEXT_DM 0x1dU

static void riscv_dm_init(riscv_dm_s *dbg_module);
static bool riscv_dmi_read(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
static riscv_debug_version_e riscv_dm_version(uint32_t status);

void riscv_dmi_init(riscv_dmi_s *const dmi)
{
	/* If we don't currently know how to talk to this DMI, warn and fail */
	if (dmi->version == RISCV_DEBUG_UNKNOWN)
		return;
	if (dmi->version == RISCV_DEBUG_0_11) {
		DEBUG_INFO("RISC-V debug v0.11 not presently supported\n");
		return;
	}

	/* Iterate through the possible DMs and probe implemented ones */
	/* The first DM is always at base address 0 */
	uint32_t base_addr = 0U;
	do {
		/* Read out the DM's status register */
		uint32_t dm_status = 0;
		if (!riscv_dmi_read(dmi, base_addr + RV_DM_STATUS, &dm_status)) {
			/* If we fail to read the status register, abort */
			break;
		}
		const riscv_debug_version_e dm_version = riscv_dm_version(dm_status);

		if (dm_version != RISCV_DEBUG_UNIMPL) {
			riscv_dm_s *dbg_module = calloc(1, sizeof(*dbg_module));
			if (!dbg_module) { /* calloc failed: heap exhaustion */
				DEBUG_WARN("calloc: failed in %s\n", __func__);
				return;
			}
			/* Setup and try to discover the DM */
			dbg_module->dmi_bus = dmi;
			dbg_module->base = base_addr;
			dbg_module->version = dm_version;
			riscv_dm_init(dbg_module);
			/* If we failed to discover any Harts, free the structure */
			if (!dbg_module->ref_count)
				free(dbg_module);
		}

		/* Read out the address of the next DM */
		if (!riscv_dmi_read(dmi, base_addr + RV_DM_NEXT_DM, &base_addr)) {
			/* If this fails then abort further scanning */
			DEBUG_INFO("Error while reading the next DM base address\n");
			break;
		}
		/* A new base address of 0 indicates this is the last one on the chain and we should stop. */
	} while (base_addr != 0U);
}

static void riscv_dm_init(riscv_dm_s *const dbg_module)
{
	(void)dbg_module;
}

static bool riscv_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	return dmi->read(dmi, address, value);
}

static riscv_debug_version_e riscv_dm_version(const uint32_t status)
{
	switch (status & RV_STATUS_VERSION_MASK) {
	case 0:
		return RISCV_DEBUG_UNIMPL;
	case 1:
		DEBUG_INFO("RISC-V debug v0.11 DM\n");
		return RISCV_DEBUG_0_11;
	case 2:
		DEBUG_INFO("RISC-V debug v0.13 DM\n");
		return RISCV_DEBUG_0_13;
	case 3:
		DEBUG_INFO("RISC-V debug v1.0 DM\n");
		return RISCV_DEBUG_1_0;
	}
	DEBUG_INFO(
		"Please report part with unknown RISC-V debug DM version %x\n", (uint8_t)(status & RV_STATUS_VERSION_MASK));
	return RISCV_DEBUG_UNKNOWN;
}

static inline void riscv_dmi_ref(riscv_dmi_s *const dmi)
{
	++dmi->ref_count;
}

static inline void riscv_dmi_unref(riscv_dmi_s *const dmi)
{
	--dmi->ref_count;
	if (!dmi->ref_count)
		free(dmi);
}

void riscv_dm_ref(riscv_dm_s *const dbg_module)
{
	if (!dbg_module->ref_count)
		riscv_dmi_ref(dbg_module->dmi_bus);
	++dbg_module->ref_count;
}

void riscv_dm_unref(riscv_dm_s *const dbg_module)
{
	--dbg_module->ref_count;
	if (!dbg_module->ref_count) {
		riscv_dmi_unref(dbg_module->dmi_bus);
		free(dbg_module);
	}
}
