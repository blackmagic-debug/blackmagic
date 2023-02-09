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
#include "target_probe.h"
#include "target_internal.h"
#include "riscv_debug.h"

#define RV_DM_DATA0           0x04U
#define RV_DM_DATA1           0x05U
#define RV_DM_DATA2           0x06U
#define RV_DM_DATA3           0x07U
#define RV_DM_CONTROL         0x10U
#define RV_DM_STATUS          0x11U
#define RV_DM_ABST_CTRLSTATUS 0x16U
#define RV_DM_ABST_COMMAND    0x17U
#define RV_DM_NEXT_DM         0x1dU

#define RV_DM_CTRL_ACTIVE          0x00000001U
#define RV_DM_CTRL_HARTSEL_MASK    0x03ffffc0U
#define RV_DM_CTRL_HARTSELLO_MASK  0x03ff0000U
#define RV_DM_CTRL_HARTSELHI_MASK  0x0000ffc0U
#define RV_DM_CTRL_HALT_REQ        0x80000000U
#define RV_DM_CTRL_RESUME_REQ      0x40000000U
#define RV_DM_CTRL_HARTSELLO_SHIFT 16U
#define RV_DM_CTRL_HARTSELHI_SHIFT 4U

#define RV_DM_STAT_ALL_RESUME_ACK 0x00020000U
#define RV_DM_STAT_NON_EXISTENT   0x00004000U
#define RV_DM_STAT_ALL_HALTED     0x00000200U

#define RV_DM_ABST_STATUS_BUSY       0x00001000U
#define RV_DM_ABST_STATUS_DATA_COUNT 0x0000000fU
#define RV_DM_ABST_CMD_ACCESS_REG    0x00000000U

#define RV_REG_READ           0x00000000U
#define RV_REG_WRITE          0x00010000U
#define RV_REG_XFER           0x00020000U
#define RV_REG_ACCESS_32_BIT  0x00200000U
#define RV_REG_ACCESS_64_BIT  0x00300000U
#define RV_REG_ACCESS_128_BIT 0x00400000U

/* The following is a set of CSR address definitions */
/* misa -> The Hart's machine ISA register */
#define RV_ISA 0x301U
/* mvendorid -> The JEP-106 code for the vendor implementing this Hart */
#define RV_VENDOR_ID 0xf11U
/* marchid -> The RISC-V International architecture ID code */
#define RV_ARCH_ID 0xf12U
/* mimplid -> Hart's processor implementation ID */
#define RV_IMPL_ID 0xf13U
/* mhartid -> machine ID of the Hart */
#define RV_HART_ID 0xf14U

#define RV_ISA_EXTENSIONS_MASK 0x03ffffffU

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

static uint32_t riscv_hart_discover_isa(riscv_hart_s *hart);

static void riscv_halt_request(target_s *target);
static void riscv_halt_resume(target_s *target, bool step);

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

static uint8_t riscv_isa_address_width(const uint32_t isa)
{
	switch (isa >> 30U) {
	case 1:
		return 32U;
	case 2:
		return 64U;
	case 3:
		return 128U;
	}
	DEBUG_INFO("Unknown address width, defaulting to 32\n");
	return 32U;
}

static bool riscv_hart_init(riscv_hart_s *const hart)
{
	/* Allocate a new target */
	target_s *target = target_new();
	if (!target)
		return false;

	/* Grab a reference to the DMI and DM structurues and do preliminary setup of the target structure */
	riscv_dm_ref(hart->dbg_module);
	target->driver = "RISC-V";
	target->priv = hart;
	target->priv_free = riscv_hart_free;

	/* Request halt and read certain key registers */
	riscv_halt_request(target);
	uint32_t isa = riscv_hart_discover_isa(hart);
	hart->address_width = riscv_isa_address_width(isa);
	hart->extensions = isa & RV_ISA_EXTENSIONS_MASK;
	riscv_csr_read(hart, RV_VENDOR_ID, &hart->vendorid);
	/* XXX: These will technically go wrong on rv64 - need some way to deal with that. */
	riscv_csr_read(hart, RV_ARCH_ID, &hart->archid);
	riscv_csr_read(hart, RV_IMPL_ID, &hart->implid);
	riscv_csr_read(hart, RV_HART_ID, &hart->hartid);
	riscv_halt_resume(target, false);

	DEBUG_INFO("Hart %" PRIx32 ": %u-bit RISC-V (arch = %08" PRIx32 "), vendor = %" PRIx32 ", impl = %" PRIx32
			   ", exts = %08" PRIx32 "\n",
		hart->hartid, hart->access_width, hart->archid, hart->vendorid, hart->implid, hart->extensions);
	/* If the hart implements mvendorid, this gives us the JEP-106, otherwise use the JTAG IDCode */
	target->designer_code = hart->vendorid ?
		hart->vendorid :
		((hart->dbg_module->dmi_bus->idcode & JTAG_IDCODE_DESIGNER_MASK) >> JTAG_IDCODE_DESIGNER_OFFSET);

	target->halt_request = riscv_halt_request;
	target->halt_resume = riscv_halt_resume;

	if (hart->access_width == 32U) {
		DEBUG_INFO("-> riscv32_probe\n");
		return riscv32_probe(target);
	}
	if (hart->access_width == 64U) {
		DEBUG_INFO("-> riscv64_probe\n");
		return riscv64_probe(target);
	}
	return true;
}

riscv_hart_s *riscv_hart_struct(target_s *const target)
{
	return (riscv_hart_s *)target->priv;
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

static uint32_t riscv_hart_discover_isa(riscv_hart_s *const hart)
{
	/* Read out the abstract command control/status register */
	uint32_t data_registers = 0;
	if (!riscv_dm_read(hart->dbg_module, RV_DM_ABST_CTRLSTATUS, &data_registers))
		return 0U;
	/* And use the data count bits to divine an initial guess on the platform width */
	data_registers &= RV_DM_ABST_STATUS_DATA_COUNT;
	DEBUG_INFO("Hart has %" PRIu32 " data registers\n", data_registers);
	/* Check we have at least enough data registers for arg0 */
	if (data_registers >= 4)
		hart->access_width = 128U;
	else if (data_registers >= 2)
		hart->access_width = 64U;
	else if (data_registers)
		hart->access_width = 32U;
	/* If the control/status register contains an invalid count, abort */
	else
		return 0;

	do {
		DEBUG_INFO("Attempting %u-bit read on misa\n", hart->access_width);
		/* Try reading the register on the guessed width */
		uint32_t isa_data[4] = {};
		bool result = riscv_csr_read(hart, RV_ISA, isa_data);
		if (result) {
			if (hart->access_width == 128U)
				return (isa_data[3] & 0xc0000000) | (isa_data[0] & 0x3fffffffU);
			if (hart->access_width == 64U)
				return (isa_data[1] & 0xc0000000) | (isa_data[0] & 0x3fffffffU);
			return isa_data[0];
		}
		/* If that failed, then find out why and instead try the next narrower width */
		if (hart->status != RISCV_HART_BUS_ERROR && hart->status != RISCV_HART_EXCEPTION)
			return 0;
		if (hart->access_width == 32U) {
			hart->access_width = 0U;
			return 0; /* We are unable to read the misa register */
		}
		if (hart->access_width == 64U)
			hart->access_width = 32U;
		if (hart->access_width == 128U)
			hart->access_width = 64U;
	} while (hart->access_width != 0U);
	DEBUG_WARN("Unable to read misa register\n");
	/* If the above loop failed, we're done.. */
	return 0U;
}

static uint32_t riscv_hart_access_width(const riscv_hart_s *const hart)
{
	if (hart->access_width == 128U)
		return RV_REG_ACCESS_128_BIT;
	if (hart->access_width == 64U)
		return RV_REG_ACCESS_64_BIT;
	return RV_REG_ACCESS_32_BIT;
}

static bool riscv_csr_wait_complete(riscv_hart_s *const hart)
{
	uint32_t status = RV_DM_ABST_STATUS_BUSY;
	while (status & RV_DM_ABST_STATUS_BUSY) {
		if (!riscv_dm_read(hart->dbg_module, RV_DM_ABST_CTRLSTATUS, &status))
			return false;
	}
	/* Shift out and mask off the command status, then reset the status on the Hart */
	hart->status = (status >> 8U) & RISCV_HART_OTHER;
	if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_CTRLSTATUS, RISCV_HART_OTHER << 8U))
		return false;
	/* If the command failed, return the failure */
	return hart->status == RISCV_HART_NO_ERROR;
}

bool riscv_csr_read(riscv_hart_s *const hart, const uint16_t reg, void *const data)
{
	/* Set up the register read and wait for it to complete */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND,
			RV_DM_ABST_CMD_ACCESS_REG | RV_REG_READ | RV_REG_XFER | riscv_hart_access_width(hart) | reg) ||
		!riscv_csr_wait_complete(hart))
		return false;
	uint32_t *const value = (uint32_t *)data;
	/* If we're doing a 128-bit read, grab the upper-most 2 uint32_t's */
	if (hart->access_width == 128U &&
		!(riscv_dm_read(hart->dbg_module, RV_DM_DATA3, value + 3) &&
			riscv_dm_read(hart->dbg_module, RV_DM_DATA2, value + 2)))
		return false;
	/* If we're doing at least a 64-bit read, grab the next uint32_t */
	if (hart->access_width >= 64U && !riscv_dm_read(hart->dbg_module, RV_DM_DATA1, value + 1))
		return false;
	/* Finally grab the last and lowest uint32_t */
	return riscv_dm_read(hart->dbg_module, RV_DM_DATA0, value);
}

bool riscv_csr_write(riscv_hart_s *const hart, const uint16_t reg, const void *const data)
{
	/* Set up the data registers based on the Hart native access size */
	const uint32_t *const value = (const uint32_t *)data;
	if (!riscv_dm_write(hart->dbg_module, RV_DM_DATA0, value[0]))
		return false;
	if (hart->access_width >= 64U && !riscv_dm_write(hart->dbg_module, RV_DM_DATA1, value[1]))
		return false;
	if (hart->access_width == 128 &&
		!(riscv_dm_write(hart->dbg_module, RV_DM_DATA2, value[2]) &&
			riscv_dm_write(hart->dbg_module, RV_DM_DATA3, value[3])))
		return false;
	/* Configure and run the write */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND,
			RV_DM_ABST_CMD_ACCESS_REG | RV_REG_WRITE | RV_REG_XFER | riscv_hart_access_width(hart) | reg))
		return false;
	return riscv_csr_wait_complete(hart);
}

static void riscv_halt_request(target_s *const target)
{
	riscv_hart_s *const hart = (riscv_hart_s *)target->priv;
	/* Request the hart to halt */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel | RV_DM_CTRL_HALT_REQ))
		return;
	uint32_t status = 0;
	/* Poll for the hart to become halted */
	while (!(status & RV_DM_STAT_ALL_HALTED)) {
		if (!riscv_dm_read(hart->dbg_module, RV_DM_STATUS, &status))
			return;
	}
	/* Clear the request now we've got it halted */
	(void)riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel);
}

static void riscv_halt_resume(target_s *target, const bool step)
{
	(void)step;
	riscv_hart_s *const hart = (riscv_hart_s *)target->priv;
	/* Request the hart to resume */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel | RV_DM_CTRL_RESUME_REQ))
		return;
	uint32_t status = 0;
	/* Poll for the hart to become resumed */
	while (!(status & RV_DM_STAT_ALL_RESUME_ACK)) {
		if (!riscv_dm_read(hart->dbg_module, RV_DM_STATUS, &status))
			return;
	}
	/* Clear the request now we've got it resumed */
	(void)riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel);
}
