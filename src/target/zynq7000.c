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

/*
 * This file implement support for the Zynq-7000 series devices, providing
 * memory maps, and other utility routines.
 *
 * NB: This handles the ARM cores only, not the PL.
 *
 * References:
 * UG585 - Zynq 7000 SoC Technical Reference Manual
 *   https://docs.xilinx.com/r/en-US/ug585-zynq-7000-SoC-TRM
 *   https://docs.xilinx.com/api/khub/maps/mxcNFn1EFZjLI1eShoEn5w/attachments/pnoMLQXFIWQ6Jhoj0BUsTQ/content?Ft-Calling-App=ft%2Fturnkey-portal&Ft-Calling-App-Version=4.2.13&download=true
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex_internal.h"
#include "exception.h"

#define CORTEXA_DBG_IDR 0x000U

/* On-Chip Memory (OCM) region definitions */
#define ZYNQ7_OCM_LOW_BASE   0x00000000U
#define ZYNQ7_OCM_HIGH_BASE  0xfffc0000U
#define ZYNQ7_OCM_CHUNK_SIZE 0x00010000U

/* System Level Control Registers */
#define ZYNQ7_SLCR_BASE         0xf8000000U
#define ZYNQ7_SLCR_UNLOCK       (ZYNQ7_SLCR_BASE + 0x008U)
#define ZYNQ7_SLCR_PSS_RST_CTRL (ZYNQ7_SLCR_BASE + 0x200U)
#define ZYNQ7_SLCR_OCM_CFG      (ZYNQ7_SLCR_BASE + 0x910U)

/* UG585 Appendix A: Register Details, pg1639 */
#define ZYNQ7_SLCR_UNLOCK_KEY 0x0000df0dU
/* UG585 Appendix A: Register Details, pg1672 */
#define ZYNQ7_SLCR_PSS_RST_CTRL_SOFT_RESET (1U << 0U)

static void zynq7_reset(target_s *target);

#define ID_ZYNQ7020 0x3b2U

bool zynq7_probe(target_s *const target)
{
	if (target->part_id != ID_ZYNQ7020)
		return false;

	target->driver = "Zynq-7000";
	target->reset = zynq7_reset;

	/* Read back the OCM mapping status */
	const uint8_t ocm_mapping = target_mem32_read32(target, ZYNQ7_SLCR_OCM_CFG) & 0x0fU;
	/* For each of the 4 chunks, pull out if it's mapped low or high and define a mapping accordingly */
	for (uint8_t chunk = 0U; chunk < 4U; ++chunk) {
		const bool chunk_high = (ocm_mapping >> chunk) & 1U;
		const uint32_t chunk_offset = chunk * ZYNQ7_OCM_CHUNK_SIZE;
		target_add_ram32(
			target, (chunk_high ? ZYNQ7_OCM_HIGH_BASE : ZYNQ7_OCM_LOW_BASE) + chunk_offset, ZYNQ7_OCM_CHUNK_SIZE);
	}

	return true;
}

static void zynq7_reset(target_s *const target)
{
	/* Try to unlock the SLCR registers and issue the reset */
	target_mem32_write32(target, ZYNQ7_SLCR_UNLOCK, ZYNQ7_SLCR_UNLOCK_KEY);
	target_mem32_write32(target, ZYNQ7_SLCR_PSS_RST_CTRL, ZYNQ7_SLCR_PSS_RST_CTRL_SOFT_RESET);

	/* For good measure, also try pulsing the physical reset pin */
	platform_nrst_set_val(true);
	platform_nrst_set_val(false);

	/* Spin until the Zynq comes back up */
	platform_timeout_s reset_timeout;
	platform_timeout_set(&reset_timeout, 1000U);
	uint32_t type = EXCEPTION_ERROR;
	const char *message = NULL;
	while (type == EXCEPTION_ERROR && !platform_timeout_is_expired(&reset_timeout)) {
		/* Try doing a new read of the core's ID register */
		TRY (EXCEPTION_ALL) {
			cortex_dbg_read32(target, CORTEXA_DBG_IDR);
		}
		innermost_exception = exception_frame.outer;
		type = exception_frame.type;
		message = exception_frame.msg;
	}
	/* If that failed, propagate the error */
	if (type == EXCEPTION_ERROR)
		raise_exception(type, message);
}
