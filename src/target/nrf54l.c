/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 Vegard Storheil Eriksen <zyp@jvnv.net>
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
 * This file implements support for nRF54L series devices, providing
 * memory maps and Flash programming routines.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"

#define NRF54L_FICR_INFO_RAM  0x00ffc328U
#define NRF54L_FICR_INFO_RRAM 0x00ffc32cU

#define NRF54L_RRAM 0x00000000U
#define NRF54L_UICR 0x00ffd000U
#define NRF54L_RAM  0x20000000U

#define NRF54L_RRAMC_READY                   0x5004b400U
#define NRF54L_RRAMC_READYNEXT               0x5004b404U
#define NRF54L_RRAMC_BUFSTATUS_WRITEBUFEMPTY 0x5004b418U
#define NRF54L_RRAMC_CONFIG                  0x5004b500U
#define NRF54L_RRAMC_ERASE_ERASEALL          0x5004b540U

#define NRF54L_RRAMC_READY_BUSY                    0U
#define NRF54L_RRAMC_READYNEXT_READY               1U
#define NRF54L_RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY 1U
#define NRF54L_RRAMC_CONFIG_WRITE_DISABLED         (0U << 0U)
#define NRF54L_RRAMC_CONFIG_WRITE_ENABLED          (1U << 0U)
#define NRF54L_RRAMC_CONFIG_WRITEBUFSIZE(size)     ((size) << 8U)
#define NRF54L_RRAMC_ERASE_ERASEALL_ERASE          1U

#define NRF54L_CTRL_AP_IDR_VALUE 0x32880000U

#define NRF54L_CTRL_AP_RESET            ADIV5_AP_REG(0x00U)
#define NRF54L_CTRL_AP_ERASEALL         ADIV5_AP_REG(0x04U)
#define NRF54L_CTRL_AP_ERASEALLSTATUS   ADIV5_AP_REG(0x08U)
#define NRF54L_CTRL_AP_APPROTECT_STATUS ADIV5_AP_REG(0x14U)

#define NRF54L_CTRL_AP_RESET_NORESET                            0U
#define NRF54L_CTRL_AP_RESET_HARDRESET                          2U
#define NRF54L_CTRL_AP_ERASEALL_ERASE                           1U
#define NRF54L_CTRL_AP_ERASEALLSTATUS_BUSY                      2U
#define NRF54L_CTRL_AP_APPROTECT_STATUS_APPROTECT_ENABLED       (1U << 0U)
#define NRF54L_CTRL_AP_APPROTECT_STATUS_SECUREAPPROTECT_ENABLED (1U << 1U)

#define ID_NRF54L 0x1c0U

static bool rram_prepare(target_flash_s *flash);
static bool rram_done(target_flash_s *flash);
static bool rram_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool rram_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool rram_mass_erase(target_s *target, platform_timeout_s *print_progess);

static void add_rram(target_s *const target, const uint32_t addr, const size_t length, const uint32_t writesize)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = writesize;
	flash->writesize = writesize;
	flash->erase = rram_erase;
	flash->write = rram_write;
	flash->prepare = rram_prepare;
	flash->done = rram_done;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool nrf54l_probe(target_s *const target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);

	if (ap->dp->version < 2U)
		return false;

	switch (ap->dp->target_partno) {
	case ID_NRF54L:
		target->driver = "nRF54L";
		target->target_options |= TOPT_INHIBIT_NRST;
		break;
	default:
		return false;
	}

	target->mass_erase = rram_mass_erase;

	const uint32_t info_ram = target_mem32_read32(target, NRF54L_FICR_INFO_RAM);
	const uint32_t info_rram = target_mem32_read32(target, NRF54L_FICR_INFO_RRAM);

	target_add_ram32(target, NRF54L_RAM, info_ram * 1024U);
	add_rram(target, NRF54L_RRAM, info_rram * 1024U, 512U);
	add_rram(target, NRF54L_UICR, 0x1000U, 4U);

	return true;
}

static bool rram_prepare(target_flash_s *const flash)
{
	uint32_t writebufsize = flash->writesize / 16U;
	target_mem32_write32(flash->t, NRF54L_RRAMC_CONFIG,
		NRF54L_RRAMC_CONFIG_WRITEBUFSIZE(writebufsize) | NRF54L_RRAMC_CONFIG_WRITE_ENABLED);
	return true;
}

static bool rram_done(target_flash_s *const flash)
{
	// Wait for writebuf to flush.
	while (target_mem32_read32(flash->t, NRF54L_RRAMC_BUFSTATUS_WRITEBUFEMPTY) !=
		NRF54L_RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY)
		continue;
	target_mem32_write32(flash->t, NRF54L_RRAMC_CONFIG, NRF54L_RRAMC_CONFIG_WRITE_DISABLED);
	return true;
}

static bool rram_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	(void)flash;
	(void)addr;
	(void)len;
	// RRAM doesn't need to be erased before being written, so we just return ok.
	return true;
}

static bool rram_write(target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t len)
{
	// Wait for rram to be ready for next write.
	while (target_mem32_read32(flash->t, NRF54L_RRAMC_READYNEXT) != NRF54L_RRAMC_READYNEXT_READY)
		continue;
	target_mem32_write(flash->t, dest, src, len);
	return true;
}

static bool rram_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	target_mem32_write32(target, NRF54L_RRAMC_ERASE_ERASEALL, NRF54L_RRAMC_ERASE_ERASEALL_ERASE);

	while (target_mem32_read32(target, NRF54L_RRAMC_READY) == NRF54L_RRAMC_READY_BUSY)
		target_print_progress(print_progess);

	return true;
}

static bool nrf54l_ctrl_ap_mass_erase(target_s *target, platform_timeout_s *print_progess);

bool nrf54l_ctrl_ap_probe(adiv5_access_port_s *const ap)
{
	switch (ap->idr) {
	case NRF54L_CTRL_AP_IDR_VALUE:
		break;
	default:
		return false;
	}

	target_s *const target = target_new();
	if (!target)
		return false;

	target->mass_erase = nrf54l_ctrl_ap_mass_erase;
	adiv5_ap_ref(ap);
	target->priv = ap;
	target->priv_free = (priv_free_func)adiv5_ap_unref;

	const uint32_t status = adiv5_ap_read(ap, NRF54L_CTRL_AP_APPROTECT_STATUS);

	if (!(status &
			(NRF54L_CTRL_AP_APPROTECT_STATUS_APPROTECT_ENABLED |
				NRF54L_CTRL_AP_APPROTECT_STATUS_SECUREAPPROTECT_ENABLED)))
		target->driver = "nRF54L Access Port";
	else
		target->driver = "nRF54L Access Port (protected)";
	target->regs_size = 0U;

	return true;
}

static bool nrf54l_ctrl_ap_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	adiv5_access_port_s *const ap = target->priv;

	const uint32_t ctrl = adiv5_dp_read(ap->dp, ADIV5_DP_CTRLSTAT);
	adiv5_dp_write(ap->dp, ADIV5_DP_CTRLSTAT, ctrl | ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);

	adiv5_ap_write(ap, NRF54L_CTRL_AP_ERASEALL, NRF54L_CTRL_AP_ERASEALL_ERASE);

	while (adiv5_ap_read(ap, NRF54L_CTRL_AP_ERASEALLSTATUS) == NRF54L_CTRL_AP_ERASEALLSTATUS_BUSY)
		target_print_progress(print_progess);

	// Assert reset.
	adiv5_ap_write(ap, NRF54L_CTRL_AP_RESET, NRF54L_CTRL_AP_RESET_HARDRESET);

	// Deassert reset.
	adiv5_ap_write(ap, NRF54L_CTRL_AP_RESET, NRF54L_CTRL_AP_RESET_NORESET);

	return true;
}
