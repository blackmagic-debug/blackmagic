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
#include "target_internal.h"
#include "riscv_debug.h"

#define RV_DM_CONTROL            0x10U
#define RV_DM_STATUS             0x11U
#define RV_DM_NEXT_DM            0x1dU
#define RV_DM_SYS_BUS_CTRLSTATUS 0x38U

#define RV_DM_CTRL_ACTIVE          0x00000001U
#define RV_DM_CTRL_HARTSEL_MASK    0x03ffffc0U
#define RV_DM_CTRL_HARTSELLO_MASK  0x03ff0000U
#define RV_DM_CTRL_HARTSELHI_MASK  0x0000ffc0U
#define RV_DM_CTRL_HARTSELLO_SHIFT 16U
#define RV_DM_CTRL_HARTSELHI_SHIFT 4U

#define RV_DM_STAT_NON_EXISTENT 0x00004000U

#define RV_DM_SYS_BUS_ADDRESS_MASK  0x00000fe0U
#define RV_DM_SYS_BUS_ADDRESS_SHIFT 5U

static void riscv_dm_init(riscv_dm_s *dbg_module);
static bool riscv_hart_init(riscv_hart_s *hart);
static void riscv_hart_free(void *priv);
static bool riscv_dmi_read(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
static bool riscv_dmi_write(riscv_dmi_s *dmi, uint32_t address, uint32_t value);
static void riscv_dm_ref(riscv_dm_s *dbg_module);
static void riscv_dm_unref(riscv_dm_s *dbg_module);
static inline bool riscv_dm_read(riscv_dm_s *dbg_module, uint8_t address, uint32_t *value);
static inline bool riscv_dm_write(riscv_dm_s *dbg_module, uint8_t address, uint32_t value);
static riscv_debug_version_e riscv_dm_version(uint32_t status);
static riscv_debug_version_e riscv_sys_bus_version(uint32_t status);

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
	/* Attempt to activate the DM */
	if (!riscv_dm_write(dbg_module, RV_DM_CONTROL, RV_DM_CTRL_ACTIVE))
		return;
	/* Now find out how many hartsel bits are present */
	uint32_t control = RV_DM_CTRL_ACTIVE | RV_DM_CTRL_HARTSEL_MASK;
	if (!riscv_dm_write(dbg_module, RV_DM_CONTROL, control) || !riscv_dm_read(dbg_module, RV_DM_CONTROL, &control))
		return;
	/* Extract the maximum number of harts present and iterate through the harts */
	const uint32_t harts_max = ((control & RV_DM_CTRL_HARTSELLO_MASK) >> RV_DM_CTRL_HARTSELLO_SHIFT) |
		((control & RV_DM_CTRL_HARTSELHI_MASK) << RV_DM_CTRL_HARTSELHI_SHIFT);
	for (uint32_t hart_idx = 0; hart_idx <= harts_max; ++hart_idx) {
		/* Select the hart */
		control = ((hart_idx << RV_DM_CTRL_HARTSELLO_SHIFT) & RV_DM_CTRL_HARTSELLO_MASK) |
			((hart_idx >> RV_DM_CTRL_HARTSELHI_SHIFT) & RV_DM_CTRL_HARTSELHI_MASK) | RV_DM_CTRL_ACTIVE;
		uint32_t status = 0;
		if (!riscv_dm_write(dbg_module, RV_DM_CONTROL, control) || !riscv_dm_read(dbg_module, RV_DM_STATUS, &status))
			return;
		/* If the hart doesn't exist, the spec says to terminate scan */
		if (status & RV_DM_STAT_NON_EXISTENT)
			break;

		riscv_hart_s *hart = calloc(1, sizeof(*hart));
		if (!hart) { /* calloc failed: heap exhaustion */
			DEBUG_WARN("calloc: failed in %s\n", __func__);
			return;
		}
		/* Setup the hart structure and discover the target core */
		hart->dbg_module = dbg_module;
		hart->hart_idx = hart_idx;
		hart->hartsel = control;
		if (!riscv_hart_init(hart))
			free(hart);
	}
}

static bool riscv_hart_init(riscv_hart_s *const hart)
{
	uint32_t bus_status = 0;
	if (!riscv_dm_read(hart->dbg_module, RV_DM_SYS_BUS_CTRLSTATUS, &bus_status))
		return false;
	hart->version = riscv_sys_bus_version(bus_status);
	hart->address_width = (bus_status & RV_DM_SYS_BUS_ADDRESS_MASK) >> RV_DM_SYS_BUS_ADDRESS_SHIFT;

	target_s *target = target_new();
	if (!target)
		return false;

	riscv_dm_ref(hart->dbg_module);
	target->cpuid = hart->dbg_module->dmi_bus->idcode;
	target->driver = "RISC-V";
	target->priv = hart;
	target->priv_free = riscv_hart_free;
	return true;
}

static void riscv_hart_free(void *const priv)
{
	riscv_dm_unref(((riscv_hart_s *)priv)->dbg_module);
	free(priv);
}

static bool riscv_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	return dmi->read(dmi, address, value);
}

static bool riscv_dmi_write(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	return dmi->write(dmi, address, value);
}

static inline bool riscv_dm_read(riscv_dm_s *dbg_module, const uint8_t address, uint32_t *const value)
{
	return riscv_dmi_read(dbg_module->dmi_bus, dbg_module->base + address, value);
}

static inline bool riscv_dm_write(riscv_dm_s *dbg_module, const uint8_t address, const uint32_t value)
{
	return riscv_dmi_write(dbg_module->dmi_bus, dbg_module->base + address, value);
}

static riscv_debug_version_e riscv_dm_version(const uint32_t status)
{
	uint8_t version = status & RV_STATUS_VERSION_MASK;
	switch (version) {
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
	DEBUG_INFO("Please report part with unknown RISC-V debug DM version %x\n", version);
	return RISCV_DEBUG_UNKNOWN;
}

static riscv_debug_version_e riscv_sys_bus_version(uint32_t status)
{
	uint8_t version = (status >> 29) & RV_STATUS_VERSION_MASK;
	switch (version) {
	case 0:
		return RISCV_DEBUG_0_11;
	case 1:
		return RISCV_DEBUG_0_13;
	}
	DEBUG_INFO("Please report part with unknown RISC-V system bus version %x\n", version);
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

static void riscv_dm_ref(riscv_dm_s *const dbg_module)
{
	if (!dbg_module->ref_count)
		riscv_dmi_ref(dbg_module->dmi_bus);
	++dbg_module->ref_count;
}

static void riscv_dm_unref(riscv_dm_s *const dbg_module)
{
	--dbg_module->ref_count;
	if (!dbg_module->ref_count) {
		riscv_dmi_unref(dbg_module->dmi_bus);
		free(dbg_module);
	}
}
