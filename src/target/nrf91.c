/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 Vegard Storheil Eriksen <zyp@jvnv.net>
 * Copyright (C) 2024-2025 1BitSquared <info@1bitsquared.com>
 * Written by Vegard Storheil Eriksen <zyp@jvnv.net>
 * Modified by Rachel Mant <git@dragonmux.network>
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
 * This file implements support for nRF91 series devices, providing
 * memory maps and Flash programming routines.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"

/* Non-Volatile Memory Controller (NVMC) Registers */
#define NRF91_NVMC          0x50039000U
#define NRF91_NVMC_READY    (NRF91_NVMC + 0x400U)
#define NRF91_NVMC_CONFIG   (NRF91_NVMC + 0x504U)
#define NRF91_NVMC_ERASEALL (NRF91_NVMC + 0x50cU)

#define NRF91_NVMC_CONFIG_REN  0x0U // Read only access
#define NRF91_NVMC_CONFIG_WEN  0x1U // Write enable
#define NRF91_NVMC_CONFIG_EEN  0x2U // Erase enable
#define NRF91_NVMC_CONFIG_PEEN 0x3U // Partial erase enable

#define ID_NRF91 0x0090U

static bool nrf91_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool nrf91_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

static void nrf91_add_flash(target_s *target, uint32_t addr, size_t length, size_t erasesize)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = erasesize;
	flash->erase = nrf91_flash_erase;
	flash->write = nrf91_flash_write;
	flash->erased = 0xff;
	target_add_flash(target, flash);
}

bool nrf91_probe(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);

	if (ap->dp->version < 2U)
		return false;

	switch (ap->dp->target_partno) {
	case ID_NRF91:
		target->driver = "nRF9160";
		target->target_options |= TOPT_INHIBIT_NRST;
		target_add_ram32(target, 0x20000000, 256U * 1024U);
		nrf91_add_flash(target, 0, 4096U * 256U, 4096U);
		break;
	default:
		return false;
	}

	return true;
}

static bool nrf91_wait_ready(target_s *const target, platform_timeout_s *const timeout)
{
	/* Poll for NVMC_READY */
	while (target_mem32_read32(target, NRF91_NVMC_READY) == 0) {
		if (target_check_error(target))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool nrf91_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)len;
	target_s *target = flash->t;

	/* Enable erase */
	target_mem32_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_EEN);
	if (!nrf91_wait_ready(target, NULL))
		return false;

	/* Write all ones to first word in page to erase it */
	target_mem32_write32(target, addr, 0xffffffffU);

	if (!nrf91_wait_ready(target, NULL))
		return false;

	/* Return to read-only */
	target_mem32_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_REN);
	return nrf91_wait_ready(target, NULL);
}

static bool nrf91_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = flash->t;

	/* Enable write */
	target_mem32_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_WEN);
	if (!nrf91_wait_ready(target, NULL))
		return false;
	/* Write the data */
	target_mem32_write(target, dest, src, len);
	if (!nrf91_wait_ready(target, NULL))
		return false;
	/* Return to read-only */
	target_mem32_write32(target, NRF91_NVMC_CONFIG, NRF91_NVMC_CONFIG_REN);
	return true;
}
