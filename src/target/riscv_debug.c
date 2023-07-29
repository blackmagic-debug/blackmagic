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
#include "gdb_reg.h"
#include "riscv_debug.h"

#include <assert.h>

/*
 * Links to exact specifications used in this code are listed here for ease:
 * riscv-debug-spec v0.11 as used by SiFive:
 * https://static.dev.sifive.com/riscv-debug-spec-0.11nov12.pdf
 * riscv-debug-spec v0.13.2 (release):
 * https://raw.githubusercontent.com/riscv/riscv-debug-spec/v0.13-release/riscv-debug-release.pdf
 * riscv-debug-spec v1.0 (stable):
 * https://github.com/riscv/riscv-debug-spec/blob/master/riscv-debug-stable.pdf
 */

#define RV_DM_CONTROL 0x10U
#define RV_DM_STATUS  0x11U
#define RV_DM_NEXT_DM 0x1dU

#define RV_DM_CTRL_ACTIVE          0x00000001U
#define RV_DM_CTRL_HARTSEL_MASK    0x03ffffc0U
#define RV_DM_CTRL_HARTSELLO_MASK  0x03ff0000U
#define RV_DM_CTRL_HARTSELHI_MASK  0x0000ffc0U
#define RV_DM_CTRL_HALT_REQ        0x80000000U
#define RV_DM_CTRL_RESUME_REQ      0x40000000U
#define RV_DM_CTRL_HART_RESET      0x20000000U
#define RV_DM_CTRL_HART_ACK_RESET  0x10000000U
#define RV_DM_CTRL_SYSTEM_RESET    0x00000002U
#define RV_DM_CTRL_HARTSELLO_SHIFT 16U
#define RV_DM_CTRL_HARTSELHI_SHIFT 4U

#define RV_DM_STAT_ALL_RESUME_ACK 0x00020000U
#define RV_DM_STAT_NON_EXISTENT   0x00004000U
#define RV_DM_STAT_ALL_HALTED     0x00000200U
#define RV_DM_STAT_ALL_RESET      0x00080000U

#define RV_DM_ABST_STATUS_BUSY       0x00001000U
#define RV_DM_ABST_STATUS_DATA_COUNT 0x0000000fU

#define RV_CSR_FORCE_MASK   0xc000U
#define RV_CSR_FORCE_32_BIT 0x4000U
#define RV_CSR_FORCE_64_BIT 0x8000U

/* The following is a set of CSR address definitions */
/* misa -> The Hart's machine ISA register */
#define RV_ISA 0x301U
/* dcsr -> Debug Control/Status Register */
#define RV_DCSR 0x7b0U
/* mvendorid -> The JEP-106 code for the vendor implementing this Hart */
#define RV_VENDOR_ID 0xf11U
/* marchid -> The RISC-V International architecture ID code */
#define RV_ARCH_ID 0xf12U
/* mimplid -> Hart's processor implementation ID */
#define RV_IMPL_ID 0xf13U
/* mhartid -> machine ID of the Hart */
#define RV_HART_ID 0xf14U

/* tselect -> Trigger selection register */
#define RV_TRIG_SELECT 0x7a0U
/* tinfo -> selected trigger information register */
#define RV_TRIG_INFO 0x7a4U
/* tdata1 -> selected trigger configuration register 1 */
#define RV_TRIG_DATA_1 0x7a1U
/* tdata2 -> selected trigger configuration register 2 */
#define RV_TRIG_DATA_2 0x7a2U

#define RV_ISA_EXTENSIONS_MASK 0x03ffffffU

#define RV_VENDOR_JEP106_CONT_MASK 0x7fffff80U
#define RV_VENDOR_JEP106_CODE_MASK 0x7fU

#define RV_DCSR_STEP       0x00000004U
#define RV_DCSR_CAUSE_MASK 0x000001c0U
#define RV_DCSR_STEPIE     0x00000800U

#define RV_GPRS_COUNT 32U

/* This enum defines the set of currently known and valid halt causes */
typedef enum riscv_halt_cause {
	/* Halt was caused by an `ebreak` instruction executing */
	RV_HALT_CAUSE_EBREAK = (1U << 6U),
	/* Halt was caused by a breakpoint or watchpoint (set in the trigger module) */
	RV_HALT_CAUSE_TRIGGER = (2U << 6U),
	/* Halt was caused by debugger request (haltreq) */
	RV_HALT_CAUSE_REQUEST = (3U << 6U),
	/* Halt was caused by single-step execution */
	RV_HALT_CAUSE_STEP = (4U << 6U),
	/* Halt was caused by request out of reset (resethaltreq) */
	RV_HALT_CAUSE_RESET = (5U << 6U),
} riscv_halt_cause_e;

// clang-format off
/* General-purpose register name strings */
static const char *const riscv_gpr_names[RV_GPRS_COUNT] = {
	"zero", "ra", "sp", "gp",
	"tp", "t0", "t1", "t2",
	"fp", "s1", "a0", "a1",
	"a2", "a3", "a4", "a5",
	"a6", "a7", "s2", "s3",
	"s4", "s5", "s6", "s7",
	"s8", "s9", "s10", "s11",
	"t3", "t4", "t5", "t6",
};
// clang-format on

/* General-purpose register types */
static const gdb_reg_type_e riscv_gpr_types[RV_GPRS_COUNT] = {
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_CODE_PTR,
	GDB_TYPE_DATA_PTR,
	GDB_TYPE_DATA_PTR,
	GDB_TYPE_DATA_PTR,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_DATA_PTR,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
	GDB_TYPE_UNSPECIFIED,
};

// clang-format off
static_assert(ARRAY_LENGTH(riscv_gpr_names) == ARRAY_LENGTH(riscv_gpr_types),
	"GPR array length mismatch! GPR type array should have the same length as GPR name array."
);
// clang-format on

static void riscv_dm_init(riscv_dm_s *dbg_module);
static bool riscv_hart_init(riscv_hart_s *hart);
static void riscv_hart_free(void *priv);
static bool riscv_dmi_read(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
static bool riscv_dmi_write(riscv_dmi_s *dmi, uint32_t address, uint32_t value);
static void riscv_dm_ref(riscv_dm_s *dbg_module);
static void riscv_dm_unref(riscv_dm_s *dbg_module);
static riscv_debug_version_e riscv_dm_version(uint32_t status);

static uint32_t riscv_hart_discover_isa(riscv_hart_s *hart);
static void riscv_hart_discover_triggers(riscv_hart_s *hart);

static bool riscv_attach(target_s *target);
static void riscv_detach(target_s *target);

static const char *riscv_target_description(target_s *target);

static bool riscv_check_error(target_s *target);
static void riscv_halt_request(target_s *target);
static void riscv_halt_resume(target_s *target, bool step);
static target_halt_reason_e riscv_halt_poll(target_s *target, target_addr_t *watch);
static void riscv_reset(target_s *target);

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

		/* If the DM is not unimplemented, allocate a structure for it and do further processing */
		if (dm_version != RISCV_DEBUG_UNIMPL) {
			riscv_dm_s *dbg_module = calloc(1, sizeof(*dbg_module));
			if (!dbg_module) { /* calloc failed: heap exhaustion */
				DEBUG_WARN("calloc: failed in %s\n", __func__);
				return;
			}
			/* Setup and try to discover the DM's Harts */
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

static void riscv_hart_read_ids(riscv_hart_s *const hart)
{
	/* Read out the vendor ID */
	riscv_csr_read(hart, RV_VENDOR_ID | RV_CSR_FORCE_32_BIT, &hart->vendorid);
	/* Adjust the value to fit our view of JEP-106 codes */
	hart->vendorid =
		((hart->vendorid & RV_VENDOR_JEP106_CONT_MASK) << 1U) | (hart->vendorid & RV_VENDOR_JEP106_CODE_MASK);
	/* Depending on the bus width, read out the other IDs suitably */
	if (hart->access_width == 32U) {
		riscv_csr_read(hart, RV_ARCH_ID, &hart->archid);
		riscv_csr_read(hart, RV_IMPL_ID, &hart->implid);
		riscv_csr_read(hart, RV_HART_ID, &hart->hartid);
	} else if (hart->access_width == 64U) {
		/* For now, on rv64, we just truncate these down after read */
		uint64_t ident = 0;
		riscv_csr_read(hart, RV_ARCH_ID, &ident);
		hart->archid = ident & 0xffffffffU;
		riscv_csr_read(hart, RV_IMPL_ID, &ident);
		hart->implid = ident & 0xffffffffU;
		riscv_csr_read(hart, RV_HART_ID, &ident);
		hart->hartid = ident & 0xffffffffU;
	}
	/* rv128 is unimpl. */
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
	riscv_hart_read_ids(hart);

	DEBUG_INFO("Hart %" PRIx32 ": %u-bit RISC-V (arch = %08" PRIx32 "), vendor = %" PRIx32 ", impl = %" PRIx32
			   ", exts = %08" PRIx32 "\n",
		hart->hartid, hart->access_width, hart->archid, hart->vendorid, hart->implid, hart->extensions);

	/* We don't support rv128, so tell the user and fast-quit on this target. */
	if (hart->access_width == 128U) {
		target->core = "(unsup) rv128";
		DEBUG_WARN("rv128 is unsupported, ignoring this hart\n");
		return true;
	}

	/* If the hart implements mvendorid, this gives us the JEP-106, otherwise use the DTM designer code */
	target->designer_code = hart->vendorid ? hart->vendorid : hart->dbg_module->dmi_bus->designer_code;
	target->cpuid = hart->archid;

	riscv_hart_discover_triggers(hart);

	/* Setup core-agnostic target functions */
	target->attach = riscv_attach;
	target->detach = riscv_detach;

	target->regs_description = riscv_target_description;

	target->check_error = riscv_check_error;
	target->halt_request = riscv_halt_request;
	target->halt_resume = riscv_halt_resume;
	target->halt_poll = riscv_halt_poll;
	target->reset = riscv_reset;

	if (hart->access_width == 32U) {
		DEBUG_INFO("-> riscv32_probe\n");
		if (!riscv32_probe(target))
			DEBUG_INFO("Probing failed, please report unknown RISC-V 32 device\n");
	}
	if (hart->access_width == 64U) {
		DEBUG_INFO("-> riscv64_probe\n");
		if (!riscv64_probe(target))
			DEBUG_INFO("Probing failed, please report unknown RISC-V 64 device\n");
	}
	riscv_halt_resume(target, false);
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

bool riscv_dm_read(riscv_dm_s *dbg_module, const uint8_t address, uint32_t *const value)
{
	return riscv_dmi_read(dbg_module->dmi_bus, dbg_module->base + address, value);
}

bool riscv_dm_write(riscv_dm_s *dbg_module, const uint8_t address, const uint32_t value)
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
		switch (hart->status) {
		case RISCV_HART_BUS_ERROR:
		case RISCV_HART_EXCEPTION:
		case RISCV_HART_NOT_SUPP: // WCH CH32Vx chips reply that
			break;
		default:
			return 0;
			break;
		}
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

static uint32_t riscv_hart_access_width(uint8_t access_width)
{
	if (access_width == 128U)
		return RV_REG_ACCESS_128_BIT;
	if (access_width == 64U)
		return RV_REG_ACCESS_64_BIT;
	return RV_REG_ACCESS_32_BIT;
}

static uint32_t riscv_csr_access_width(const uint16_t reg)
{
	const uint16_t access_width = reg & RV_CSR_FORCE_MASK;
	if (access_width == RV_CSR_FORCE_32_BIT)
		return 32U;
	if (access_width == RV_CSR_FORCE_64_BIT)
		return 64U;
	return 128U;
}

bool riscv_command_wait_complete(riscv_hart_s *const hart)
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
	if (hart->status != RISCV_HART_NO_ERROR)
		DEBUG_WARN("CSR access failed: %u\n", hart->status);
	/* If the command failed, return the failure */
	return hart->status == RISCV_HART_NO_ERROR;
}

bool riscv_csr_read(riscv_hart_s *const hart, const uint16_t reg, void *const data)
{
	const uint8_t access_width = (reg & RV_CSR_FORCE_MASK) ? riscv_csr_access_width(reg) : hart->access_width;
	DEBUG_TARGET("Reading %u-bit CSR %03x\n", access_width, reg & ~RV_CSR_FORCE_MASK);
	/* Set up the register read and wait for it to complete */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND,
			RV_DM_ABST_CMD_ACCESS_REG | RV_ABST_READ | RV_REG_XFER | riscv_hart_access_width(access_width) |
				(reg & ~RV_CSR_FORCE_MASK)) ||
		!riscv_command_wait_complete(hart))
		return false;
	uint32_t *const value = (uint32_t *)data;
	/* If we're doing a 128-bit read, grab the upper-most 2 uint32_t's */
	if (access_width == 128U &&
		!(riscv_dm_read(hart->dbg_module, RV_DM_DATA3, value + 3) &&
			riscv_dm_read(hart->dbg_module, RV_DM_DATA2, value + 2)))
		return false;
	/* If we're doing at least a 64-bit read, grab the next uint32_t */
	if (access_width >= 64U && !riscv_dm_read(hart->dbg_module, RV_DM_DATA1, value + 1))
		return false;
	/* Finally grab the last and lowest uint32_t */
	return riscv_dm_read(hart->dbg_module, RV_DM_DATA0, value);
}

bool riscv_csr_write(riscv_hart_s *const hart, const uint16_t reg, const void *const data)
{
	const uint8_t access_width = (reg & RV_CSR_FORCE_MASK) ? riscv_csr_access_width(reg) : hart->access_width;
	DEBUG_TARGET("Writing %u-bit CSR %03x\n", access_width, reg & ~RV_CSR_FORCE_MASK);
	/* Set up the data registers based on the Hart native access size */
	const uint32_t *const value = (const uint32_t *)data;
	if (!riscv_dm_write(hart->dbg_module, RV_DM_DATA0, value[0]))
		return false;
	if (access_width >= 64U && !riscv_dm_write(hart->dbg_module, RV_DM_DATA1, value[1]))
		return false;
	if (access_width == 128 &&
		!(riscv_dm_write(hart->dbg_module, RV_DM_DATA2, value[2]) &&
			riscv_dm_write(hart->dbg_module, RV_DM_DATA3, value[3])))
		return false;
	/* Configure and run the write */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND,
			RV_DM_ABST_CMD_ACCESS_REG | RV_ABST_WRITE | RV_REG_XFER | riscv_hart_access_width(access_width) |
				(reg & ~RV_CSR_FORCE_MASK)))
		return false;
	return riscv_command_wait_complete(hart);
}

uint8_t riscv_mem_access_width(const riscv_hart_s *const hart, const target_addr_t address, const size_t length)
{
	/* Grab the Hart's most maxmimally aligned possible write width */
	uint8_t access_width = riscv_hart_access_width(hart->address_width) >> RV_MEM_ACCESS_SHIFT;
	/* Convert the hart access width to a mask - for example, for 32-bit harts, this gives (1U << 2U) - 1U = 3U */
	uint8_t align_mask = (1U << access_width) - 1U;
	/* Mask out the bottom bits of both the address and length - anything that determines the alignment */
	const uint8_t addr_bits = address & align_mask;
	const uint8_t len_bits = length & align_mask;
	/* bitwise-OR together the result so, for example, an odd address 2-byte read results in a pattern like 0bxx11 */
	const uint8_t align = addr_bits | len_bits;
	/* Loop down through the possible access widths till we find one suitably aligned */
	for (; access_width; --access_width) {
		if ((align & align_mask) == 0)
			return access_width;
		align_mask >>= 1U;
	}
	return access_width;
}

static void riscv_hart_discover_triggers(riscv_hart_s *const hart)
{
	/* Discover how many breakpoints this hart supports */
	hart->triggers = UINT32_MAX;
	if (!riscv_csr_write(hart, RV_TRIG_SELECT | RV_CSR_FORCE_32_BIT, &hart->triggers) ||
		!riscv_csr_read(hart, RV_TRIG_SELECT | RV_CSR_FORCE_32_BIT, &hart->triggers)) {
		hart->triggers = 0;
		return;
	}
	/*
	 * The value we read back will always be one less than the actual number supported
	 * as it represents the last valid index, rather than the last valid breakpoint.
	 */
	++hart->triggers;
	DEBUG_INFO("Hart has %" PRIu32 " trigger slots available\n", hart->triggers);
	/* If the hardware supports more slots than we do, cap it. */
	if (hart->triggers > RV_TRIGGERS_MAX)
		hart->triggers = RV_TRIGGERS_MAX;

	/* Next, go through each one and map what it supports out into the trigger_uses slots */
	for (uint32_t trigger = 0; trigger < hart->triggers; ++trigger) {
		/* Select the trigger */
		riscv_csr_write(hart, RV_TRIG_SELECT | RV_CSR_FORCE_32_BIT, &trigger);
		/* Try reading the trigger info */
		uint32_t info = 0;
		bool alternate = false;
		/* Some chips reply ok but returns 0 in the following call (WCH)*/
		if (!riscv_csr_read(hart, RV_TRIG_INFO | RV_CSR_FORCE_32_BIT, &info))
			alternate = true;
		else {
			if (!info)
				alternate = true;
		}
		if (alternate) {
			/*
			 * If that fails, it's probably because the tinfo register isn't implemented, so read
			 * the tdata1 register instead and extract the type from the MSb and build the info bitset from that
			 */
			if (hart->access_width == 32U) {
				uint32_t data = 0;
				riscv_csr_read(hart, RV_TRIG_DATA_1, &data);
				/* The last 4 bits contain the trigger info */
				info = data >> 28U;
			} else {
				uint64_t data = 0;
				riscv_csr_read(hart, RV_TRIG_DATA_1, &data);
				/* The last 4 bits contain the trigger info */
				info = data >> 60U;
			}
			/* Info now needs converting from a value from 0 to 15 to having the correct bit set */
			info = 1U << info;
		}
		/* If the 0th bit is set, this means the trigger is unsupported. Clear it to make testing easy */
		info &= RV_TRIGGER_SUPPORT_MASK;
		/* Now info's bottom 16 bits contain the supported trigger modes, so write this info to the slot in the hart */
		hart->trigger_uses[trigger] = info;
		DEBUG_TARGET("Hart trigger slot %" PRIu32 " modes: %04" PRIx32 "\n", trigger, info);
	}
}

riscv_match_size_e riscv_breakwatch_match_size(const size_t size)
{
	switch (size) {
	case 8U:
		return RV_MATCH_SIZE_8_BIT;
	case 16U:
		return RV_MATCH_SIZE_16_BIT;
	case 32U:
		return RV_MATCH_SIZE_32_BIT;
	case 48U:
		return RV_MATCH_SIZE_48_BIT;
	case 64U:
		return RV_MATCH_SIZE_64_BIT;
	case 80U:
		return RV_MATCH_SIZE_80_BIT;
	case 96U:
		return RV_MATCH_SIZE_96_BIT;
	case 112U:
		return RV_MATCH_SIZE_112_BIT;
	case 128U:
		return RV_MATCH_SIZE_128_BIT;
	}
	return 0;
}

bool riscv_config_trigger(riscv_hart_s *const hart, const uint32_t trigger, const riscv_trigger_state_e mode,
	const void *const config, const void *const address)
{
	/*
	 * Select the trigger and write the new configuration to it provided by config.
	 * tdata1 (RV_TRIG_DATA_1) becomes mcontrol (match control) for this -
	 * see §5.2.9 pg53 of the RISC-V debug spec v0.13.2 for more details.
	 */
	const bool result = riscv_csr_write(hart, RV_TRIG_SELECT | RV_CSR_FORCE_32_BIT, &trigger) &&
		riscv_csr_write(hart, RV_TRIG_DATA_1, config) && riscv_csr_write(hart, RV_TRIG_DATA_2, address);
	if (result) {
		/* If that succeeds, update the slot with the new mode it's in */
		hart->trigger_uses[trigger] &= ~RV_TRIGGER_MODE_MASK;
		hart->trigger_uses[trigger] |= mode;
	}
	return result;
}

static bool riscv_attach(target_s *const target)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* If the DMI requires special preparation, do that first */
	if (hart->dbg_module->dmi_bus->prepare)
		hart->dbg_module->dmi_bus->prepare(target);
	/* We then also need to select the Hart again so we're poking with the right one on the target */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel))
		return false;
	/* We then need to halt the hart so the attach process can function */
	riscv_halt_request(target);
	return true;
}

static void riscv_detach(target_s *const target)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Once we get done and the user's asked us to detach, we need to resume the hart */
	riscv_halt_resume(target, false);
	/* If the DMI needs steps done to quiesce it, finsh up with that */
	if (hart->dbg_module->dmi_bus->quiesce)
		hart->dbg_module->dmi_bus->quiesce(target);
}

static bool riscv_check_error(target_s *const target)
{
	return riscv_hart_struct(target)->status != RISCV_HART_NO_ERROR;
}

static bool riscv_dm_poll_state(riscv_dm_s *const dbg_module, const uint32_t state)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500U);
	/* Poll for the requested state to become set */
	uint32_t status = 0;
	while (!(status & state)) {
		if (!riscv_dm_read(dbg_module, RV_DM_STATUS, &status) || platform_timeout_is_expired(&timeout))
			return false;
	}
	return true;
}

static void riscv_halt_request(target_s *const target)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Request the hart to halt */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel | RV_DM_CTRL_HALT_REQ))
		return;
	/* Poll for the hart to become halted */
	if (!riscv_dm_poll_state(hart->dbg_module, RV_DM_STAT_ALL_HALTED))
		return;
	/* Clear the request now we've got it halted */
	(void)riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel);
}

static void riscv_halt_resume(target_s *target, const bool step)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Configure the debug controller for single-stepping as appropriate */
	uint32_t stepping_config = 0U;
	if (!riscv_csr_read(hart, RV_DCSR | RV_CSR_FORCE_32_BIT, &stepping_config))
		return;
	if (step)
		stepping_config |= RV_DCSR_STEP | RV_DCSR_STEPIE;
	else
		stepping_config &= ~(RV_DCSR_STEP | RV_DCSR_STEPIE);
	if (!riscv_csr_write(hart, RV_DCSR | RV_CSR_FORCE_32_BIT, &stepping_config))
		return;
	/* Request the hart to resume */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel | RV_DM_CTRL_RESUME_REQ))
		return;
	/* Poll for the hart to become resumed */
	if (!riscv_dm_poll_state(hart->dbg_module, RV_DM_STAT_ALL_RESUME_ACK))
		return;
	/* Clear the request now we've got it resumed */
	(void)riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel);
}

static target_halt_reason_e riscv_halt_poll(target_s *const target, target_addr_t *const watch)
{
	(void)watch;
	riscv_hart_s *const hart = riscv_hart_struct(target);
	uint32_t status = 0;
	/* Check if the hart is currently halted */
	if (!riscv_dm_read(hart->dbg_module, RV_DM_STATUS, &status))
		return TARGET_HALT_ERROR;
	/* If the hart is currently running, exit out early */
	if (!(status & RV_DM_STAT_ALL_HALTED))
		return TARGET_HALT_RUNNING;
	/* Read out DCSR to find out why we're halted */
	if (!riscv_csr_read(hart, RV_DCSR, &status))
		return TARGET_HALT_ERROR;
	status &= RV_DCSR_CAUSE_MASK;
	/* Dispatch on the cause code */
	switch (status) {
	case RV_HALT_CAUSE_TRIGGER:
		/* XXX: Need to read out the triggers to find the one causing this, and grab the watch value */
		return TARGET_HALT_BREAKPOINT;
	case RV_HALT_CAUSE_STEP:
		return TARGET_HALT_STEPPING;
	default:
		break;
	}
	/* In the default case, assume it was by request (ebreak, haltreq, resethaltreq) */
	return TARGET_HALT_REQUEST;
}

/* Do note that this can be used with a riscv_halt_request() call to initiate halt-on-reset debugging */
static void riscv_reset(target_s *const target)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* If the target does not have the nRST pin inhibited, use that to initiate reset */
	if (!(target->target_options & RV_TOPT_INHIBIT_NRST)) {
		platform_nrst_set_val(true);
		riscv_dm_poll_state(hart->dbg_module, RV_DM_STAT_ALL_RESET);
		platform_nrst_set_val(false);
		/* In theory we're done at this point and no debug state was perturbed */
	} else {
		/*
		 * Otherwise, if nRST is not usable, use instead reset via dmcontrol. In this case,
		 * when reset is requested, use the ndmreset bit to perform a system reset
		 */
		riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel | RV_DM_CTRL_SYSTEM_RESET);
		riscv_dm_poll_state(hart->dbg_module, RV_DM_STAT_ALL_RESET);
		/* Complete the reset by resetting ndmreset */
		riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel);
	}
	/* Acknowledge the reset */
	riscv_dm_write(hart->dbg_module, RV_DM_CONTROL, hart->hartsel | RV_DM_CTRL_HART_ACK_RESET);
	target_check_error(target);
}

static const char *riscv_fpu_ext_string(const uint32_t extensions)
{
	if (extensions & RV_ISA_EXT_QUAD_FLOAT)
		return "q";
	if (extensions & RV_ISA_EXT_DOUBLE_FLOAT)
		return "d";
	if (extensions & RV_ISA_EXT_SINGLE_FLOAT)
		return "f";
	return "";
}

/*
 * This function creates the target description XML string for a RISC-V part.
 * This is done this way to decrease string duplication and thus code size, making it
 * unfortunately much less readable than the string literal it is equivalent to.
 *
 * This string it creates is the XML-equivalent to the following:
 * "<?xml version=\"1.0\"?>"
 * "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
 * "<target>"
 * "	<architecture>riscv:rv[address_width][exts]</architecture>"
 * "	<feature name=\"org.gnu.gdb.riscv.cpu\">"
 * "		<reg name=\"zero\" bitsize=\"[address_width]\" regnum=\"0\"/>"
 * "		<reg name=\"ra\" bitsize=\"[address_width]\" type=\"code_ptr\"/>"
 * "		<reg name=\"sp\" bitsize=\"[address_width]\" type=\"data_ptr\"/>"
 * "		<reg name=\"gp\" bitsize=\"[address_width]\" type=\"data_ptr\"/>"
 * "		<reg name=\"tp\" bitsize=\"[address_width]\" type=\"data_ptr\"/>"
 * "		<reg name=\"t0\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"t1\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"t2\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"fp\" bitsize=\"[address_width]\" type=\"data_ptr\"/>"
 * "		<reg name=\"s1\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a0\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a1\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a2\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a3\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a4\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a5\" bitsize=\"[address_width]\"/>"
 * The following are only generated for an I core, not an E:
 * "		<reg name=\"a6\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"a7\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s2\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s3\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s4\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s5\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s6\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s7\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s8\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s9\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s10\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"s11\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"t3\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"t4\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"t5\" bitsize=\"[address_width]\"/>"
 * "		<reg name=\"t6\" bitsize=\"[address_width]\"/>"
 * Both are then continued with:
 * "		<reg name=\"pc\" bitsize=\"[address_width]\" type=\"code_ptr\"/>"
 * "	</feature>"
 * "</target>"
 */
static size_t riscv_build_target_description(
	char *const buffer, size_t max_length, const uint8_t address_width, const uint32_t extensions)
{
	const bool embedded = extensions & RV_ISA_EXT_EMBEDDED;
	const uint32_t fpu = extensions & RV_ISA_EXT_ANY_FLOAT;

	size_t print_size = max_length;
	/* Start with the "preamble" chunks, which are mostly common across targets save for 2 words. */
	int offset = snprintf(buffer, print_size, "%s target %sriscv:rv%u%c%s%s <feature name=\"org.gnu.gdb.riscv.cpu\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, address_width, embedded ? 'e' : 'i', riscv_fpu_ext_string(fpu),
		gdb_xml_preamble_third);

	const uint8_t gprs = embedded ? 16U : 32U;
	/* Then build the general purpose register descriptions using the arrays at top of file */
	/* Note that in a device using the embedded (E) extension, we only generate the first 16. */
	for (uint8_t i = 0; i < gprs; ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		const char *const name = riscv_gpr_names[i];
		const gdb_reg_type_e type = riscv_gpr_types[i];

		offset += snprintf(buffer + offset, print_size, "<reg name=\"%s\" bitsize=\"%u\"%s%s/>", name, address_width,
			gdb_reg_type_strings[type], i == 0 ? " regnum=\"0\"" : "");
	}

	/* Then build the program counter register description, which has the same bitsize as the GPRs. */
	if (max_length != 0)
		print_size = max_length - (size_t)offset;
	offset += snprintf(buffer + offset, print_size, "<reg name=\"pc\" bitsize=\"%u\"%s/>", address_width,
		gdb_reg_type_strings[GDB_TYPE_CODE_PTR]);

	/* XXX: TODO - implement generation of the FPU feature and registers */

	/* Add the closing tags required */
	if (max_length != 0)
		print_size = max_length - (size_t)offset;

	offset += snprintf(buffer + offset, print_size, "</feature></target>");
	/* offset is now the total length of the string created, discard the sign and return it. */
	return (size_t)offset;
}

static const char *riscv_target_description(target_s *const target)
{
	const riscv_hart_s *const hart = riscv_hart_struct(target);
	const size_t description_length =
		riscv_build_target_description(NULL, 0, hart->address_width, hart->extensions) + 1U;
	char *const description = malloc(description_length);
	if (description)
		(void)riscv_build_target_description(description, description_length, hart->address_width, hart->extensions);
	return description;
}
