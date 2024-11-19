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

/*
 * Pile of timing parameters needed to make sure flash works, see section "4.5. Flash configuration bytes" of the RM.
 *
 * The layout is very similar across devices. The start address differs and sometimes the entries are 64-bit aligned
 * rather than just 32-bit but otherwise the offsets are identical for entries that are present.
 */
/* PY32F002A, PY32F003 */
#define PUYA_TIMING_INFO_START_002A_003 0x1fff0f00u

/* PY32F002B (starting at page 2) */
#define PUYA_TIMING_INFO_START_002B 0x1fff0100u

/* PY32[FM]07x */
#define PUYA_TIMING_INFO_START_07X 0x1fff3200u

/* Start index for HSI_TRIM calibration values. */
#define PUYA_FLASH_TIMING_HSITRIM_IDX 0x00U

/* Start index for EPPARA0…4 entries. */
#define PUYA_FLASH_TIMING_EPPARA0_IDX 0x07U

/* PY32F002A, PY32F003 and PY32[FM]07x have the same layout. */
#define PY32F0XX_EPPARA0_TS0_SHIFT     0U
#define PY32F0XX_EPPARA0_TS0_MASK      0xffU
#define PY32F0XX_EPPARA0_TS3_SHIFT     8U
#define PY32F0XX_EPPARA0_TS3_MASK      0xffU
#define PY32F0XX_EPPARA0_TS1_SHIFT     16U
#define PY32F0XX_EPPARA0_TS1_MASK      0x1ffU
#define PY32F0XX_EPPARA1_TS2P_SHIFT    0U
#define PY32F0XX_EPPARA1_TS2P_MASK     0xffU
#define PY32F0XX_EPPARA1_TPS3_SHIFT    16U
#define PY32F0XX_EPPARA1_TPS3_MASK     0x7ffU
#define PY32F0XX_EPPARA2_PERTPE_SHIFT  0U
#define PY32F0XX_EPPARA2_PERTPE_MASK   0x1ffffU
#define PY32F0XX_EPPARA3_SMERTPE_SHIFT 0U
#define PY32F0XX_EPPARA3_SMERTPE_MASK  0x1ffffU
#define PY32F0XX_EPPARA4_PRGTPE_SHIFT  0U
#define PY32F0XX_EPPARA4_PRGTPE_MASK   0xffffU
#define PY32F0XX_EPPARA4_PRETPE_SHIFT  16U
/* The English version of PY32F002A Reference Manual says EPPARA4 26:16 (11 bit) are PRETPE[11:0] (12 bit) and
 * FLASH_PRETPE is 14 bit wide (0:13). However the Chinese version consistently has 14 bits for PRETPE everywhere and
 * that's also how the hardware behaves. */
#define PY32F0XX_EPPARA4_PRETPE_MASK 0x3fffU

/* PY32F002B has a layout different from the other supported models. */
#define PY32F002B_EPPARA0_TS0_SHIFT     0U
#define PY32F002B_EPPARA0_TS0_MASK      0x1ffU
#define PY32F002B_EPPARA0_TS3_SHIFT     9U
#define PY32F002B_EPPARA0_TS3_MASK      0x1ffU
#define PY32F002B_EPPARA0_TS1_SHIFT     18U
#define PY32F002B_EPPARA0_TS1_MASK      0x3ffU
#define PY32F002B_EPPARA1_TS2P_SHIFT    0U
#define PY32F002B_EPPARA1_TS2P_MASK     0x1ffU
#define PY32F002B_EPPARA1_TPS3_SHIFT    16U
#define PY32F002B_EPPARA1_TPS3_MASK     0xfffU
#define PY32F002B_EPPARA2_PERTPE_SHIFT  0U
#define PY32F002B_EPPARA2_PERTPE_MASK   0x3ffffU
#define PY32F002B_EPPARA3_SMERTPE_SHIFT 0U
#define PY32F002B_EPPARA3_SMERTPE_MASK  0x3ffffU
#define PY32F002B_EPPARA4_PRGTPE_SHIFT  0U
#define PY32F002B_EPPARA4_PRGTPE_MASK   0xffffU
#define PY32F002B_EPPARA4_PRETPE_SHIFT  16U
#define PY32F002B_EPPARA4_PRETPE_MASK   0x3fffU

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
#define PUYA_RCC_BASE                 0x40021000U
#define PUYA_RCC_ICSCR                (PUYA_RCC_BASE + 0x04U)
#define PUYA_RCC_ICSCR_HSI_FS_SHIFT   13U
#define PUYA_RCC_ICSCR_HSI_FS_MASK    7U
#define PUYA_RCC_ICSCR_HSI_TRIM_SHIFT 0U
#define PUYA_RCC_ICSCR_HSI_TRIM_MASK  0x1fffU

/* DBG */
#define PUYA_DBG_BASE   0x40015800U
#define PUYA_DBG_IDCODE (PUYA_DBG_BASE + 0x00U)
/*
 * The format and values of the IDCODE register are undocumented but the vendor SDK splits IDCODE into 11:0 DEV_ID and
 * 31:16 REV_ID.
 */
#define PUYA_DBG_IDCODE_DEV_ID_SHIFT 0U
#define PUYA_DBG_IDCODE_DEV_ID_MASK  0xfffU
#define PUYA_DBG_IDCODE_REV_ID_SHIFT 16U
#define PUYA_DBG_IDCODE_REV_ID_MASK  0xffffU

/*
 * Observed IDCODE (0x40015800) and FLASH_RAM_SZ (0x1fff0ffc) values:
 *
 * | Model                     | IDCODE     | FLASH_RAM_SZ |
 * |---------------------------+------------+--------------|
 * | PY32F002AF15P6            | 0x60001000 | 0xffec0013   |
 * | PY32F002AL15S6            | 0x60001000 | 0xffec0013   |
 * | PY32F002AW15U?            | 0x60001000 | ?            |
 * | PY32F002BD15S6            | 0x20220064 | 0x00000000   |
 * | PY32F002BF15P6            | 0x20220064 | 0x00000000   |
 * | PY32F003L24D6             | 0x60001000 | 0xfffe0001   |
 * | PY32F030F18P6             | 0x60001000 | 0xffc80037   |
 * | PY32F030F38P6             | 0x60001000 | 0xffc80037   |
 * | PY32M070K1BU7-C           | 0x06188061 | n/a          |
 */
/* PY32F002A, PY32F003, PY32F030 */
#define PUYA_DEV_ID_PY32F0XX  0x000U
#define PUYA_DEV_ID_PY32F002B 0x064U
#define PUYA_DEV_ID_PY32X07X  0x061U

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
	const uint16_t dev_id = (dbg_idcode >> PUYA_DBG_IDCODE_DEV_ID_SHIFT) & PUYA_DBG_IDCODE_DEV_ID_MASK;
	switch (dev_id) {
	case PUYA_DEV_ID_PY32F0XX: {
		const uint32_t flash_ram_sz = target_mem32_read32(target, PUYA_FLASH_RAM_SZ);
		flash_size = (((flash_ram_sz >> PUYA_FLASH_SZ_SHIFT) & PUYA_FLASH_SZ_MASK) + 1) << PUYA_FLASH_UNIT_SHIFT;
		ram_size = (((flash_ram_sz >> PUYA_RAM_SZ_SHIFT) & PUYA_RAM_SZ_MASK) + 1) << PUYA_RAM_UNIT_SHIFT;
		target->driver = "PY32F0xx";
		break;
	}
	case PUYA_DEV_ID_PY32F002B:
		/*
		 * 0x1fff0ffc contains 0; did not find any other location that looks like it might contain the flash
		 * and RAM sizes. We'll hard-code the datasheet values for now. Both flash size and RAM size actually
		 * match the datasheet value, unlike PY32F002A which (sometimes?) has more RAM and flash than
		 * documented.
		 */
		flash_size = 24U * 1024U;
		ram_size = 3U * 1024U;
		target->driver = "PY32F002B";
		break;
	case PUYA_DEV_ID_PY32X07X:
		/* 0x1fff0ffc is in boot loader code. The vendor BSP references 0x1fff31fc as FLASHSIZE_BASE for
		 * PY32[FM]07x but that location contains 0xffffffff on the PY32M070K1BU7. Hardcode the values for
		 * now. */
		flash_size = 128U * 1024U;
		ram_size = 16U * 1024U;
		target->driver = "PY32x07x";
		break;
	default:
		DEBUG_TARGET("Unknown PY32 device %08" PRIx32 "\n", dbg_idcode);
		return false;
	}

	target->part_id = dev_id;
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

	target_s *target = flash->t;
	uint32_t cal_base;
	/* On most models the configuration "bytes" are 32-bit aligned. Exceptions (64-bit alignment) are handled
	 * below. */
	uint8_t cal_shift = 2;

	switch (target->part_id) {
	case PUYA_DEV_ID_PY32F0XX: {
		cal_base = PUYA_TIMING_INFO_START_002A_003;
		break;
	}
	case PUYA_DEV_ID_PY32F002B:
		cal_base = PUYA_TIMING_INFO_START_002B;
		break;
	case PUYA_DEV_ID_PY32X07X:
		cal_base = PUYA_TIMING_INFO_START_07X;
		/* configuration bytes are 64-bit aligned rather than just 32-bit */
		cal_shift = 3;
		break;
	default:
		/* Should have never made it past probe */
		DEBUG_TARGET("Unknown PY32 device %08" PRIx32 "\n", target->part_id);
		return false;
	}

	const uint32_t icscr_old = target_mem32_read32(flash->t, PUYA_RCC_ICSCR);
	/* Not all models support all frequencies but the mapping is
	 * the same for all of them. */
	uint8_t hsi_fs = (icscr_old >> PUYA_RCC_ICSCR_HSI_FS_SHIFT) & PUYA_RCC_ICSCR_HSI_FS_MASK;
	if (hsi_fs > 4)
		hsi_fs = 0;
	if (target->part_id == PUYA_DEV_ID_PY32F002B) {
		/* PY32F002B only supports 24MHz HSI. The HSI_TRIM and EPPARA0 entries for 24MHz are at offset 0,
		 * disregarding the HSI_FS value. */
		hsi_fs = 0;
	}
	DEBUG_TARGET("HSI frequency selection is %d\n", hsi_fs);

	const uint32_t hsi_trim =
		target_mem32_read32(flash->t, cal_base + ((PUYA_FLASH_TIMING_HSITRIM_IDX + (hsi_fs * 5) + 0) << cal_shift));
	uint32_t eppara[5];
	for (uint32_t idx = 0; idx < 5; idx++) {
		eppara[idx] = target_mem32_read32(
			flash->t, cal_base + ((PUYA_FLASH_TIMING_EPPARA0_IDX + (hsi_fs * 5) + idx) << cal_shift));
	}
	DEBUG_TARGET("PY32 HSI trim value: %08" PRIx32 "\n", hsi_trim);
	DEBUG_TARGET("PY32 flash timing cal 0: %08" PRIx32 "\n", eppara[0]);
	DEBUG_TARGET("PY32 flash timing cal 1: %08" PRIx32 "\n", eppara[1]);
	DEBUG_TARGET("PY32 flash timing cal 2: %08" PRIx32 "\n", eppara[2]);
	DEBUG_TARGET("PY32 flash timing cal 3: %08" PRIx32 "\n", eppara[3]);
	DEBUG_TARGET("PY32 flash timing cal 4: %08" PRIx32 "\n", eppara[4]);

	target_mem32_write32(flash->t, PUYA_RCC_ICSCR,
		(icscr_old & ~PUYA_RCC_ICSCR_HSI_TRIM_MASK) | (hsi_trim & PUYA_RCC_ICSCR_HSI_TRIM_MASK));
	switch (target->part_id) {
	case PUYA_DEV_ID_PY32F002B:
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS0, (eppara[0] >> PY32F002B_EPPARA0_TS0_SHIFT) & PY32F002B_EPPARA0_TS0_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS1, (eppara[0] >> PY32F002B_EPPARA0_TS1_SHIFT) & PY32F002B_EPPARA0_TS1_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS3, (eppara[0] >> PY32F002B_EPPARA0_TS3_SHIFT) & PY32F002B_EPPARA0_TS3_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS2P, (eppara[1] >> PY32F002B_EPPARA1_TS2P_SHIFT) & PY32F002B_EPPARA1_TS2P_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TPS3, (eppara[1] >> PY32F002B_EPPARA1_TPS3_SHIFT) & PY32F002B_EPPARA1_TPS3_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_PERTPE, (eppara[2] >> PY32F002B_EPPARA2_PERTPE_SHIFT) & PY32F002B_EPPARA2_PERTPE_MASK);
		target_mem32_write32(flash->t, PUYA_FLASH_SMERTPE,
			(eppara[3] >> PY32F002B_EPPARA3_SMERTPE_SHIFT) & PY32F002B_EPPARA3_SMERTPE_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_PRGTPE, (eppara[4] >> PY32F002B_EPPARA4_PRGTPE_SHIFT) & PY32F002B_EPPARA4_PRGTPE_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_PRETPE, (eppara[4] >> PY32F002B_EPPARA4_PRETPE_SHIFT) & PY32F002B_EPPARA4_PRETPE_MASK);
		break;
	default:
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS0, (eppara[0] >> PY32F0XX_EPPARA0_TS0_SHIFT) & PY32F0XX_EPPARA0_TS0_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS1, (eppara[0] >> PY32F0XX_EPPARA0_TS1_SHIFT) & PY32F0XX_EPPARA0_TS1_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS3, (eppara[0] >> PY32F0XX_EPPARA0_TS3_SHIFT) & PY32F0XX_EPPARA0_TS3_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TS2P, (eppara[1] >> PY32F0XX_EPPARA1_TS2P_SHIFT) & PY32F0XX_EPPARA1_TS2P_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_TPS3, (eppara[1] >> PY32F0XX_EPPARA1_TPS3_SHIFT) & PY32F0XX_EPPARA1_TPS3_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_PERTPE, (eppara[2] >> PY32F0XX_EPPARA2_PERTPE_SHIFT) & PY32F0XX_EPPARA2_PERTPE_MASK);
		target_mem32_write32(flash->t, PUYA_FLASH_SMERTPE,
			(eppara[3] >> PY32F0XX_EPPARA3_SMERTPE_SHIFT) & PY32F0XX_EPPARA3_SMERTPE_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_PRGTPE, (eppara[4] >> PY32F0XX_EPPARA4_PRGTPE_SHIFT) & PY32F0XX_EPPARA4_PRGTPE_MASK);
		target_mem32_write32(
			flash->t, PUYA_FLASH_PRETPE, (eppara[4] >> PY32F0XX_EPPARA4_PRETPE_SHIFT) & PY32F0XX_EPPARA4_PRETPE_MASK);
		break;
	}
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
