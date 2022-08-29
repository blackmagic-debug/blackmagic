/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * Based on prior work by Uwe Bones <bon@elektron.ikp.physik.tu-darmstadt.de>
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
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define LPC55xx_CHIPID 0x50000ff8U
#define LPC55_DMAP_IDR 0x002a0000U

#define LPC55_DMAP_BULK_ERASE          0x02U
#define LPC55_DMAP_START_DEBUG_SESSION 0x07U

static bool lpc55_dmap_cmd(adiv5_access_port_s *ap, uint32_t cmd);
static bool lpc55_dmap_mass_erase(target_s *target);
static void lpc55_dmap_ap_free(void *priv);

void lpc55_dp_prepare(adiv5_debug_port_s *const dp)
{
	/* Reading targetid again here upsets the LPC55 and STM32U5!*/
	/* UM11126, 51.6.1
	 * Debug session with uninitialized/invalid flash image or ISP mode
	 */
	adiv5_dp_abort(dp, ADIV5_DP_ABORT_DAPABORT);
	/* Set up a dummy Access Port on the stack */
	adiv5_access_port_s ap = {};
	ap.dp = dp;
	ap.apsel = 2;
	/* Read out the ID register and check it's the LPC55's Debug Mailbox ID */
	ap.idr = adiv5_ap_read(&ap, ADIV5_AP_IDR);
	if (ap.idr != LPC55_DMAP_IDR)
		return; /* Return early if this likely is not an LPC55 */

	/* Try reading out the AP 0 IDR */
	ap.apsel = 0;
	ap.idr = adiv5_ap_read(&ap, ADIV5_AP_IDR);
	/* If that failed, then we have to activate the debug mailbox */
	if (ap.idr == 0) {
		DEBUG_INFO("Running LPC55 activation sequence\n");
		ap.apsel = 2;
		adiv5_ap_write(&ap, ADIV5_AP_CSW, 0x21);
		lpc55_dmap_cmd(&ap, LPC55_DMAP_START_DEBUG_SESSION);
	}
	/* At this point we assume that we've got access to the debug mailbox and can continue normally. */
}

bool lpc55xx_probe(target_s *const target)
{
	adiv5_access_port_s *const ap = cortexm_ap(target);
	if (ap->apsel == 1)
		return false;
#ifdef ENABLE_DEBUG
	const uint32_t chipid = target_mem_read32(target, LPC55xx_CHIPID);
	DEBUG_WARN("Chip ID: %08" PRIx32 "\n", chipid);
#endif

	target_add_ram(target, 0x20000000, 0x44000);
	target->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	switch (target->cpuid & CPUID_PATCH_MASK) {
	case 3:
		target->driver = "LPC55";
		break;
	case 4:
		target->driver = "LPC551x";
		break;
	}
	return true;
}

bool lpc55_dmap_probe(adiv5_access_port_s *ap)
{
	if (ap->idr != LPC55_DMAP_IDR)
		return false;

	target_s *target = target_new();
	if (!target)
		return false;

	adiv5_ap_ref(ap);
	target->priv = ap;
	target->priv_free = lpc55_dmap_ap_free;

	target->driver = "LPC55 Debug Mailbox";
	target->regs_size = 0;
	target->mass_erase = lpc55_dmap_mass_erase;

	return true;
}

static void lpc55_dmap_ap_free(void *priv)
{
	adiv5_ap_unref(priv);
}

static bool lpc55_dmap_cmd(adiv5_access_port_s *const ap, const uint32_t cmd)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 20);
	while (true) {
		const uint32_t csw = adiv5_ap_read(ap, ADIV5_AP_CSW);
		if (csw == 0)
			break;
		if (platform_timeout_is_expired(&timeout))
			return false;
	}

	adiv5_ap_write(ap, ADIV5_AP_TAR, cmd);

	platform_timeout_set(&timeout, 20);
	while (true) {
		const uint16_t value = (uint16_t)adiv5_ap_read(ap, ADIV5_AP_DRW);
		if (value == 0)
			return true;
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("LPC55 cmd %" PRIx32 " failed\n", cmd);
			return false;
		}
	}
}

static bool lpc55_dmap_mass_erase(target_s *target)
{
	return lpc55_dmap_cmd(cortexm_ap(target), LPC55_DMAP_BULK_ERASE);
}
