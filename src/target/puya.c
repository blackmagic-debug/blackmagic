// SPDX-License-Identifier: BSD-3-Clause
/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 ArcaneNibble, jediminer543
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
#include "adiv5.h"
#include "buffer_utils.h"

/* Flash */
#define PUYA_FLASH_START     0x08000000U
#define PUYA_FLASH_PAGE_SIZE 128

/* Pile of timing parameters needed to make sure flash works,
 * see section "4.4. Flash configuration bytes" of the RM.
 */
#define PUYA_FLASH_TIMING_CAL_BASE 0x1fff0f1cU
/* This config word is undocumented, but the Puya-ISP boot code
 * uses it to determine the valid flash/ram size.
 * (yes, this *does* include undocumented free extra flash/ram in the 002A)
 *
 * bits[2:0] => flash size in multiples of 0x2000 bytes, minus 1
 * bits[5:4] => RAM size in multiples of 0x800 bytes, minus 1
 */
#define PUYA_FLASH_RAM_SZ     0x1fff0ffcU
#define PUYA_FLASH_SZ_SHIFT   0U
#define PUYA_FLASH_SZ_MASK    7U
#define PUYA_FLASH_UNIT_SHIFT 13U
#define PUYA_RAM_SZ_SHIFT     4U
#define PUYA_RAM_SZ_MASK      3U
#define PUYA_RAM_UNIT_SHIFT   11U

/* Flash control registers */
#define PUYA_FLASH_BASE      0x40022000U
#define PUYA_FLASH_KEYR      (PUYA_FLASH_BASE + 0x008U)
#define PUYA_FLASH_KEYR_KEY1 0x45670123U
#define PUYA_FLASH_KEYR_KEY2 0xcdef89abU

#define PUYA_FLASH_SR        (PUYA_FLASH_BASE + 0x010U)
#define PUYA_FLASH_SR_BSY    (1U << 16U)
#define PUYA_FLASH_SR_WRPERR (1U << 4U)

#define PUYA_FLASH_CR        (PUYA_FLASH_BASE + 0x014U)
#define PUYA_FLASH_CR_LOCK   (1U << 31U)
#define PUYA_FLASH_CR_PGSTRT (1U << 19U)
#define PUYA_FLASH_CR_PER    (1U << 1U)
#define PUYA_FLASH_CR_PG     (1U << 0U)

#define PUYA_FLASH_TS0     (PUYA_FLASH_BASE + 0x100U)
#define PUYA_FLASH_TS1     (PUYA_FLASH_BASE + 0x104U)
#define PUYA_FLASH_TS2P    (PUYA_FLASH_BASE + 0x108U)
#define PUYA_FLASH_TPS3    (PUYA_FLASH_BASE + 0x10cU)
#define PUYA_FLASH_TS3     (PUYA_FLASH_BASE + 0x110U)
#define PUYA_FLASH_PERTPE  (PUYA_FLASH_BASE + 0x114U)
#define PUYA_FLASH_SMERTPE (PUYA_FLASH_BASE + 0x118U)
#define PUYA_FLASH_PRGTPE  (PUYA_FLASH_BASE + 0x11cU)
#define PUYA_FLASH_PRETPE  (PUYA_FLASH_BASE + 0x120U)

/* RAM */
#define PUYA_RAM_START 0x20000000U

/* RCC */
#define PUYA_RCC_BASE               0x40021000U
#define PUYA_RCC_ICSCR              (PUYA_RCC_BASE + 0x04U)
#define PUYA_RCC_ICSCR_HSI_FS_SHIFT 13U
#define PUYA_RCC_ICSCR_HSI_FS_MASK  7U

/* DBG */
#define PUYA_DBG_BASE   0x40015800U
#define PUYA_DBG_IDCODE (PUYA_DBG_BASE + 0x00U)

/*
 * Flash functions
 */
static bool puya_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool puya_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool puya_flash_prepare(target_flash_s *flash);
static bool puya_flash_done(target_flash_s *flash);

bool puya_probe(target_s *target)
{
	uint32_t ram_size = 0U;
	size_t flash_size = 0U;

	const uint32_t dbg_idcode = target_mem32_read32(target, PUYA_DBG_IDCODE);
	if ((dbg_idcode & 0xfffU) == 0) {
		const uint32_t flash_ram_sz = target_mem32_read32(target, PUYA_FLASH_RAM_SZ);
		flash_size = (((flash_ram_sz >> PUYA_FLASH_SZ_SHIFT) & PUYA_FLASH_SZ_MASK) + 1) << PUYA_FLASH_UNIT_SHIFT;
		ram_size = (((flash_ram_sz >> PUYA_RAM_SZ_SHIFT) & PUYA_RAM_SZ_MASK) + 1) << PUYA_RAM_UNIT_SHIFT;
		// TODO: which part families does this actually correspond to?
		// Tested with a PY32F002AW15U which returns 0x60001000 in IDCODE
		target->driver = "PY32Fxxx";
	} else {
		DEBUG_TARGET("Unknown PY32 device %08" PRIx32 "\n", dbg_idcode);
		return false;
	}

	target_add_ram32(target, PUYA_RAM_START, ram_size);
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	flash->start = PUYA_FLASH_START;
	flash->length = flash_size;
	flash->blocksize = PUYA_FLASH_PAGE_SIZE;
	flash->writesize = PUYA_FLASH_PAGE_SIZE;
	flash->erase = puya_flash_erase;
	flash->write = puya_flash_write;
	flash->prepare = puya_flash_prepare;
	flash->done = puya_flash_done;
	flash->erased = 0xffU;
	target_add_flash(target, flash);

	return true;
}

static bool puya_flash_prepare(target_flash_s *flash)
{
	target_mem32_write32(flash->t, PUYA_FLASH_KEYR, PUYA_FLASH_KEYR_KEY1);
	target_mem32_write32(flash->t, PUYA_FLASH_KEYR, PUYA_FLASH_KEYR_KEY2);

	uint8_t hsi_fs =
		(target_mem32_read32(flash->t, PUYA_RCC_ICSCR) >> PUYA_RCC_ICSCR_HSI_FS_SHIFT) & PUYA_RCC_ICSCR_HSI_FS_MASK;
	if (hsi_fs > 4)
		hsi_fs = 0;
	DEBUG_TARGET("HSI frequency selection is %d\n", hsi_fs);

	const uint32_t eppara0 = target_mem32_read32(flash->t, PUYA_FLASH_TIMING_CAL_BASE + hsi_fs * 20 + 0);
	const uint32_t eppara1 = target_mem32_read32(flash->t, PUYA_FLASH_TIMING_CAL_BASE + hsi_fs * 20 + 4);
	const uint32_t eppara2 = target_mem32_read32(flash->t, PUYA_FLASH_TIMING_CAL_BASE + hsi_fs * 20 + 8);
	const uint32_t eppara3 = target_mem32_read32(flash->t, PUYA_FLASH_TIMING_CAL_BASE + hsi_fs * 20 + 12);
	const uint32_t eppara4 = target_mem32_read32(flash->t, PUYA_FLASH_TIMING_CAL_BASE + hsi_fs * 20 + 16);
	DEBUG_TARGET("PY32 flash timing cal 0: %08" PRIx32 "\n", eppara0);
	DEBUG_TARGET("PY32 flash timing cal 1: %08" PRIx32 "\n", eppara1);
	DEBUG_TARGET("PY32 flash timing cal 2: %08" PRIx32 "\n", eppara2);
	DEBUG_TARGET("PY32 flash timing cal 3: %08" PRIx32 "\n", eppara3);
	DEBUG_TARGET("PY32 flash timing cal 4: %08" PRIx32 "\n", eppara4);

	target_mem32_write32(flash->t, PUYA_FLASH_TS0, eppara0 & 0xffU);
	target_mem32_write32(flash->t, PUYA_FLASH_TS1, (eppara0 >> 16U) & 0x1ffU);
	target_mem32_write32(flash->t, PUYA_FLASH_TS3, (eppara0 >> 8U) & 0xffU);
	target_mem32_write32(flash->t, PUYA_FLASH_TS2P, eppara1 & 0xffU);
	target_mem32_write32(flash->t, PUYA_FLASH_TPS3, (eppara1 >> 16U) & 0x7ffU);
	target_mem32_write32(flash->t, PUYA_FLASH_PERTPE, eppara2 & 0x1ffffU);
	target_mem32_write32(flash->t, PUYA_FLASH_SMERTPE, eppara3 & 0x1ffffU);
	target_mem32_write32(flash->t, PUYA_FLASH_PRGTPE, eppara4 & 0xffffU);
	target_mem32_write32(flash->t, PUYA_FLASH_PRETPE, (eppara4 >> 16U) & 0x3fffU);

	return true;
}

static bool puya_flash_done(target_flash_s *flash)
{
	target_mem32_write32(flash->t, PUYA_FLASH_CR, PUYA_FLASH_CR_LOCK);
	return true;
}

static bool puya_wait_flash(target_s *const target, platform_timeout_s *const timeout)
{
	while (target_mem32_read32(target, PUYA_FLASH_SR) & PUYA_FLASH_SR_BSY) {
		if (target_check_error(target))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool puya_check_flash_no_error(target_s *const target)
{
	uint32_t status = target_mem32_read32(target, PUYA_FLASH_SR);
	if (status & PUYA_FLASH_SR_WRPERR)
		DEBUG_ERROR("puya flash erase error: sr 0x%" PRIx32 "\n", status);
	return !((status & PUYA_FLASH_SR_WRPERR));
}

static bool puya_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)len;
	target_mem32_write32(flash->t, PUYA_FLASH_CR, PUYA_FLASH_CR_PER);
	target_mem32_write32(flash->t, addr, 0);
	if (!puya_wait_flash(flash->t, NULL))
		return false;
	return puya_check_flash_no_error(flash->t);
}

static bool puya_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_mem32_write32(flash->t, PUYA_FLASH_CR, PUYA_FLASH_CR_PG);
	for (size_t i = 0; i < len; i += 4) {
		if (i == len - 4)
			target_mem32_write32(flash->t, PUYA_FLASH_CR, PUYA_FLASH_CR_PG | PUYA_FLASH_CR_PGSTRT);
		target_mem32_write32(flash->t, dest + i, read_le4(src, i));
	}
	if (!puya_wait_flash(flash->t, NULL))
		return false;
	return puya_check_flash_no_error(flash->t);
}
