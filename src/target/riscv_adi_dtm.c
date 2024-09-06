/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2024 1BitSquared <info@1bitsquared.com>
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

static bool riscv_adi_dmi_read(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
static bool riscv_adi_dmi_write(riscv_dmi_s *dmi, uint32_t address, uint32_t value);

void riscv_adi_dtm_handler(adiv5_access_port_s *const ap)
{
	riscv_dmi_ap_s *dmi_ap = calloc(1, sizeof(*dmi_ap));
	if (!dmi_ap) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	/* Setup and try to discover the DMI bus */
	dmi_ap->ap = ap;
	adiv5_ap_ref(ap);
	dmi_ap->dmi.dev_index = 0xffU;
	dmi_ap->dmi.idle_cycles = 0xffU;
	dmi_ap->dmi.designer_code = ap->dp->designer_code;
	dmi_ap->dmi.version = RISCV_DEBUG_0_13; /* The DMI version doesn't actually matter, so just make it spec v0.13 */
	dmi_ap->dmi.address_width = ap->flags & ADIV5_AP_FLAGS_64BIT ? 64U : 32U;

	dmi_ap->dmi.read = riscv_adi_dmi_read;
	dmi_ap->dmi.write = riscv_adi_dmi_write;
	riscv_dmi_init(&dmi_ap->dmi);

	/* If we failed to find any DMs or Harts, free the structure */
	if (!dmi_ap->dmi.ref_count) {
		adiv5_ap_unref(ap);
		free(dmi_ap);
	}
}

static bool riscv_adi_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	adiv5_access_port_s *const ap = ((riscv_dmi_ap_s *)dmi)->ap;
	adiv5_mem_read(ap, value, address << 2U, 4U);
	return adiv5_dp_error(ap->dp) == 0U;
}

static bool riscv_adi_dmi_write(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	adiv5_access_port_s *const ap = ((riscv_dmi_ap_s *)dmi)->ap;
	adiv5_mem_write(ap, address << 2U, &value, 4U);
	return adiv5_dp_error(ap->dp) == 0U;
}
