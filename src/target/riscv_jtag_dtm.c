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
#include "riscv_debug.h"

#define IR_DTMCS  0x10U
#define IR_DMI    0x11U
#define IR_BYPASS 0x1fU

#define RV_DTMCS_NOOP           0x00000000U
#define RV_DTMCS_DMI_RESET      0x00010000U
#define RV_DTMCS_DMI_HARD_RESET 0x00020000U
#define RV_DTMCS_VERSION_MASK   0x0000000fU

static void riscv_jtag_dtm_init(riscv_dmi_s *dmi);
static uint32_t riscv_shift_dtmcs(const riscv_dmi_s *dmi, uint32_t control);
static riscv_debug_version_e riscv_dtmcs_version(uint32_t dtmcs);

void riscv_jtag_dtm_handler(const uint8_t dev_index)
{
	riscv_dmi_s *dmi = calloc(1, sizeof(*dmi));
	if (!dmi) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	dmi->idcode = jtag_devs[dev_index].jd_idcode;
	dmi->dev_index = dev_index;
	riscv_jtag_dtm_init(dmi);
	/* If we failed to find any DMs or Harts, free the structure */
	if (!dmi->ref_count)
		free(dmi);
	jtag_dev_write_ir(dev_index, IR_BYPASS);
}

static void riscv_jtag_dtm_init(riscv_dmi_s *const dmi)
{
	const uint32_t dtmcs = riscv_shift_dtmcs(dmi, RV_DTMCS_NOOP);
	dmi->version = riscv_dtmcs_version(dtmcs);
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

static riscv_debug_version_e riscv_dtmcs_version(const uint32_t dtmcs)
{
	switch (dtmcs & RV_DTMCS_VERSION_MASK) {
	case 0:
		DEBUG_INFO("RISC-V debug v0.11 DMI\n");
		return RISCV_DEBUG_0_11;
	case 1:
		/* The stable version of the spec (v1.0) does not currently provide a way to distinguish between */
		/* a device built against v0.13 of the spec or v1.0 of the spec. They use the same value here. */
		DEBUG_INFO("RISC-V debug v0.13/v1.0 DMI\n");
		return RISCV_DEBUG_0_13;
	}
	DEBUG_INFO(
		"Please report part with unknown RISC-V debug DMI version %x\n", (uint8_t)(dtmcs & RV_DTMCS_VERSION_MASK));
	return RISCV_DEBUG_UNKNOWN;
}
