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
#include "jtag_scan.h"
#include "jtagtap.h"
#include "riscv_debug.h"
#include "adiv5.h"

#define IR_DTMCS  0x10U
#define IR_DMI    0x11U
#define IR_BYPASS 0x1fU

#define RV_DTMCS_NOOP              0x00000000U
#define RV_DTMCS_DMI_RESET         0x00010000U
#define RV_DTMCS_DMI_HARD_RESET    0x00020000U
#define RV_DTMCS_IDLE_CYCLES_MASK  0x00007000U
#define RV_DTMCS_IDLE_CYCLES_SHIFT 12U
#define RV_DTMCS_DMI_STATUS_MASK   0x00000c00U
#define RV_DTMCS_DMI_STATUS_SHIFT  10U
#define RV_DTMCS_ADDRESS_MASK      0x000003f0U
#define RV_DTMCS_ADDRESS_SHIFT     4U

#define RV_DMI_NOOP     0U
#define RV_DMI_READ     1U
#define RV_DMI_WRITE    2U
#define RV_DMI_SUCCESS  0U
#define RV_DMI_FAILURE  2U
#define RV_DMI_TOO_SOON 3U

static void riscv_jtag_dtm_init(riscv_dmi_s *dmi);
static uint32_t riscv_shift_dtmcs(const riscv_dmi_s *dmi, uint32_t control);
static bool riscv_jtag_dmi_read(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
static bool riscv_jtag_dmi_write(riscv_dmi_s *dmi, uint32_t address, uint32_t value);
static riscv_debug_version_e riscv_dtmcs_version(uint32_t dtmcs);

static void riscv_jtag_prepare(target_s *target);
static void riscv_jtag_quiesce(target_s *target);

void riscv_jtag_dtm_handler(const uint8_t dev_index)
{
	riscv_dmi_s *dmi = calloc(1, sizeof(*dmi));
	if (!dmi) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	/* Setup and try to discover the DMI bus */
	dmi->dev_index = dev_index;
	/*
	 * The code in JTAG_IDCODE_DESIGNER is in the form
	 * Bits 10:7 - JEP-106 Continuation code
	 * Bits 6:0 - JEP-106 Identity code
	 * here we convert it to our internal representation, See JEP-106 code list
	 */
	const uint16_t designer =
		(jtag_devs[dev_index].jd_idcode & JTAG_IDCODE_DESIGNER_MASK) >> JTAG_IDCODE_DESIGNER_OFFSET;
	dmi->designer_code =
		(designer & JTAG_IDCODE_DESIGNER_JEP106_CONT_MASK) << 1U | (designer & JTAG_IDCODE_DESIGNER_JEP106_CODE_MASK);

	riscv_jtag_dtm_init(dmi);
	/* If we failed to find any DMs or Harts, free the structure */
	if (!dmi->ref_count)
		free(dmi);

	/* Reset the JTAG machinery back to bypass to scan the next device in the chain */
	jtag_dev_write_ir(dev_index, IR_BYPASS);
}

static void riscv_jtag_dtm_init(riscv_dmi_s *const dmi)
{
	const uint32_t dtmcs = riscv_shift_dtmcs(dmi, RV_DTMCS_NOOP);
	dmi->version = riscv_dtmcs_version(dtmcs);
	/* Configure the TAP idle cylces based on what we've read */
	dmi->idle_cycles = (dtmcs & RV_DTMCS_IDLE_CYCLES_MASK) >> RV_DTMCS_IDLE_CYCLES_SHIFT;
	/* And figure out how many address bits the DMI address space has */
	dmi->address_width = (dtmcs & RV_DTMCS_ADDRESS_MASK) >> RV_DTMCS_ADDRESS_SHIFT;
	/* Switch into DMI access mode for speed */
	jtag_dev_write_ir(dmi->dev_index, IR_DMI);

	dmi->prepare = riscv_jtag_prepare;
	dmi->quiesce = riscv_jtag_quiesce;
	dmi->read = riscv_jtag_dmi_read;
	dmi->write = riscv_jtag_dmi_write;

	riscv_dmi_init(dmi);
}

/* Shift (read + write) the Debug Transport Module Control/Status (DTMCS) register */
uint32_t riscv_shift_dtmcs(const riscv_dmi_s *const dmi, const uint32_t control)
{
	jtag_dev_write_ir(dmi->dev_index, IR_DTMCS);
	uint32_t status = 0;
	jtag_dev_shift_dr(dmi->dev_index, (uint8_t *)&status, (const uint8_t *)&control, 32);
	return status;
}

static void riscv_dmi_reset(const riscv_dmi_s *const dmi)
{
	riscv_shift_dtmcs(dmi, RV_DTMCS_DMI_RESET);
	jtag_dev_write_ir(dmi->dev_index, IR_DMI);
}

static uint8_t riscv_shift_dmi(riscv_dmi_s *const dmi, const uint8_t operation, const uint32_t address,
	const uint32_t data_in, uint32_t *const data_out)
{
	jtag_dev_s *device = &jtag_devs[dmi->dev_index];
	jtagtap_shift_dr();
	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);
	/* Shift out the 2 bits for the operation, and get the status bits for the previous back */
	uint8_t status = 0;
	jtag_proc.jtagtap_tdi_tdo_seq(&status, false, &operation, 2);
	/* Then the data component */
	if (data_out)
		jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)data_out, false, (const uint8_t *)&data_in, 32);
	else
		jtag_proc.jtagtap_tdi_seq(false, (const uint8_t *)&data_in, 32);
	/* And finally the address component */
	jtag_proc.jtagtap_tdi_seq(!device->dr_postscan, (const uint8_t *)&address, dmi->address_width);
	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
	jtagtap_return_idle(dmi->idle_cycles);
	/* Translate error 1 into RV_DMI_FAILURE per the spec */
	if (status == 1U)
		return RV_DMI_FAILURE;
	return status;
}

static bool riscv_dmi_transfer(riscv_dmi_s *const dmi, const uint8_t operation, const uint32_t address,
	const uint32_t data_in, uint32_t *const data_out)
{
	/* Try the transfer */
	uint8_t status = riscv_shift_dmi(dmi, operation, address, data_in, data_out);

	/* Handle status == 3 (RV_DMI_TOO_SOON) */
	if (status == RV_DMI_TOO_SOON) {
		/*
		 * If we got RV_DMI_TOO_SOON and we're under 8 idle cycles, increase the number
		 * of idle cycles used to compensate and have the outer code re-run the transnfers
		 */
		if (dmi->idle_cycles < 8)
			++dmi->idle_cycles;
		/*
		 * Otherwise we've hit 8 idle cycles, it doesn't matter if we get another
		 * RV_DMI_TOO_SOON, treat that as a hard error and bail out.
		 */
		else
			status = RV_DMI_FAILURE;
	}

	dmi->fault = status;
	/* If we get straight failure, do a DMI reset */
	if (status == RV_DMI_FAILURE || status == RV_DMI_TOO_SOON)
		riscv_dmi_reset(dmi);
	return status == RV_DMI_SUCCESS;
}

static bool riscv_jtag_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	bool result = true;
	do {
		/* Setup the location to read from */
		result = riscv_dmi_transfer(dmi, RV_DMI_READ, address, 0, NULL);
		if (result)
			/* If that worked, read back the value and check the operation status */
			result = riscv_dmi_transfer(dmi, RV_DMI_NOOP, 0, 0, value);
	} while (dmi->fault == RV_DMI_TOO_SOON);

	if (!result)
		DEBUG_WARN("DMI read at 0x%08" PRIx32 " failed with status %u\n", address, dmi->fault);
	return result;
}

static bool riscv_jtag_dmi_write(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	bool result = true;
	do {
		/* Write a value to the requested register */
		result = riscv_dmi_transfer(dmi, RV_DMI_WRITE, address, value, NULL);
		if (result)
			/* If that worked, read back the operation status to ensure the write actually worked */
			result = riscv_dmi_transfer(dmi, RV_DMI_NOOP, 0, 0, NULL);
	} while (dmi->fault == RV_DMI_TOO_SOON);

	if (!result)
		DEBUG_WARN("DMI write at 0x%08" PRIx32 " failed with status %u\n", address, dmi->fault);
	return result;
}

static riscv_debug_version_e riscv_dtmcs_version(const uint32_t dtmcs)
{
	uint8_t version = dtmcs & RV_STATUS_VERSION_MASK;
	switch (version) {
	case 0:
		DEBUG_INFO("RISC-V debug v0.11 DMI\n");
		return RISCV_DEBUG_0_11;
	case 1:
		/* The stable version of the spec (v1.0) does not currently provide a way to distinguish between */
		/* a device built against v0.13 of the spec or v1.0 of the spec. They use the same value here. */
		DEBUG_INFO("RISC-V debug v0.13/v1.0 DMI\n");
		return RISCV_DEBUG_0_13;
	}
	DEBUG_INFO("Please report part with unknown RISC-V debug DMI version %x\n", version);
	return RISCV_DEBUG_UNKNOWN;
}

static void riscv_jtag_prepare(target_s *const target)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* We put the TAP into bypass at the end of the JTAG handler, so put it back into DMI */
	jtag_dev_write_ir(hart->dbg_module->dmi_bus->dev_index, IR_DMI);
}

static void riscv_jtag_quiesce(target_s *const target)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* On detaching, stick the TAP back into bypass */
	jtag_dev_write_ir(hart->dbg_module->dmi_bus->dev_index, IR_BYPASS);
}
