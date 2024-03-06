/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <tolstov_den@mail.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file implements AT32F43x target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * ARTERY doc - RM_AT32F435_437_EN_V2.04.pdf
 *   Reference manual - AT32F435/437 Series Reference Manual
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool at32f43_flash_prepare(target_flash_s *flash);
static bool at32f43_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool at32f43_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool at32f43_flash_done(target_flash_s *flash);
static bool at32f43_mass_erase(target_s *target);

/* Flash memory controller register map */
#define AT32F43x_FLASH_REG_BASE 0x40023c00U
#define AT32F43x_FLASH_UNLOCK   (AT32F43x_FLASH_REG_BASE + 0x04U)
#define AT32F43x_FLASH_STS      (AT32F43x_FLASH_REG_BASE + 0x0cU)
#define AT32F43x_FLASH_CTRL     (AT32F43x_FLASH_REG_BASE + 0x10U)
#define AT32F43x_FLASH_ADDR     (AT32F43x_FLASH_REG_BASE + 0x14U)
/* There is a second set of identical registers at +0x40 offset for Bank 2 */

#define AT32F43x_FLASH_BANK1_REG_OFFSET 0x00U
#define AT32F43x_FLASH_BANK2_REG_OFFSET 0x40U

/* Flash registers bit fields */
#define AT32F43x_FLASH_CTRL_FPRGM   (1U << 0U)
#define AT32F43x_FLASH_CTRL_SECERS  (1U << 1U)
#define AT32F43x_FLASH_CTRL_BANKERS (1U << 2U)
#define AT32F43x_FLASH_CTRL_ERSTR   (1U << 6U)
#define AT32F43x_FLASH_CTRL_OPLK    (1U << 7U)
/* CTRL bits [8:11] are reserved, parallellism x8/x16/x32 (don't care) */

/* OBF is BSY, ODF is EOP */
#define AT32F43x_FLASH_STS_OBF     (1U << 0U)
#define AT32F43x_FLASH_STS_PRGMERR (1U << 2U)
#define AT32F43x_FLASH_STS_ODF     (1U << 5U)

#define AT32F43x_FLASH_KEY1 0x45670123U
#define AT32F43x_FLASH_KEY2 0xcdef89abU

#define DBGMCU_IDCODE 0xe0042000U

#define AT32F4x_IDCODE_SERIES_MASK 0xfffff000U
#define AT32F4x_IDCODE_PART_MASK   0x00000fffU
#define AT32F43_SERIES_4K          0x70084000U
#define AT32F43_SERIES_2K          0x70083000U

typedef struct at32f43_flash {
	target_flash_s target_flash;
	target_addr_t bank_split; /* Address of first page of bank 2 */
	uint32_t bank_reg_offset; /* Flash register offset for this bank */
} at32f43_flash_s;

static void at32f43_add_flash(target_s *const target, const target_addr_t addr, const size_t length,
	const size_t pagesize, const target_addr_t bank_split, const uint32_t bank_reg_offset)
{
	if (length == 0)
		return;

	at32f43_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = pagesize;
	target_flash->prepare = at32f43_flash_prepare;
	target_flash->erase = at32f43_flash_erase;
	target_flash->write = at32f43_flash_write;
	target_flash->done = at32f43_flash_done;
	target_flash->writesize = 1024U;
	target_flash->erased = 0xffU;
	flash->bank_split = bank_split;
	flash->bank_reg_offset = bank_reg_offset;
	target_add_flash(target, target_flash);
}

static bool at32f43_detect(target_s *target, const uint16_t part_id)
{
	/*
	 * AT32F435 EOPB0 ZW/NZW split reconfiguration unsupported,
	 * assuming default split ZW=256 SRAM=384.
	 * AT32F437 also have a working "EMAC" (Ethernet MAC)
	 */
	uint32_t flash_size_bank1 = 0;
	uint32_t flash_size_bank2 = 0;
	uint32_t sector_size = 0;
	switch (part_id) {
	// 0x70084000U parts with 4KB sectors:
	case 0x0540U: // LQFP144
	case 0x0543U: // LQFP100
	case 0x0546U: // LQFP64
	case 0x0549U: // LQFP48
	case 0x054cU: // QFN48
	case 0x054fU: // LQFP144 w/Eth
	case 0x0552U: // LQFP100 w/Eth
	case 0x0555U: // LQFP64 w/Eth
		// Flash (M): 4032 KB in 2 banks (2048+1984), 4KB per sector.
		flash_size_bank1 = 2048U * 1024U;
		flash_size_bank2 = 1984U * 1024U;
		sector_size = 4096;
		break;
	case 0x0598U: // LQFP144
	case 0x0599U: // LQFP100
	case 0x059aU: // LQFP64
	case 0x059bU: // LQFP48
	case 0x059cU: // QFN48
	case 0x059dU: // LQFP144 w/Eth
	case 0x059eU: // LQFP100 w/Eth
	case 0x059fU: // LQFP64 w/Eth
		// Flash (D): 448 KB, only bank 1, 4KB per sector.
		flash_size_bank1 = 448U * 1024U;
		sector_size = 4096;
		break;
	// 0x70083000U parts with 2KB sectors:
	case 0x0341U: // LQFP144
	case 0x0344U: // LQFP100
	case 0x0347U: // LQFP64
	case 0x034aU: // LQFP48
	case 0x034dU: // QFN48
	case 0x0350U: // LQFP144 w/Eth
	case 0x0353U: // LQFP100 w/Eth
	case 0x0356U: // LQFP64 w/Eth
		// Flash (G): 1024 KB in 2 banks (equal), 2KB per sector.
		flash_size_bank1 = 512U * 1024U;
		flash_size_bank2 = 512U * 1024U;
		sector_size = 2048;
		break;
	case 0x0242U: // LQFP144
	case 0x0245U: // LQFP100
	case 0x0248U: // LQFP64
	case 0x024bU: // LQFP48
	case 0x024eU: // QFN48
	case 0x0251U: // LQFP144 w/Eth
	case 0x0254U: // LQFP100 w/Eth
	case 0x0257U: // LQFP64 w/Eth
		// Flash (C): 256 KB, only bank 1, 2KB per sector.
		flash_size_bank1 = 256U * 1024U;
		sector_size = 2048;
		break;
	default:
		return false;
	}
	/*
	 * Arterytek F43x Flash controller has BLKERS (1<<3U).
	 * Block erase operates on 64 KB at once for all parts.
	 * Using here only sector erase (page erase) for compatibility.
	 */
	if (flash_size_bank2 > 0) {
		const uint32_t bank_split = 0x08000000 + flash_size_bank1;
		at32f43_add_flash(
			target, 0x08000000, flash_size_bank1, sector_size, bank_split, AT32F43x_FLASH_BANK1_REG_OFFSET);
		at32f43_add_flash(
			target, bank_split, flash_size_bank2, sector_size, bank_split, AT32F43x_FLASH_BANK2_REG_OFFSET);
	} else
		at32f43_add_flash(target, 0x08000000, flash_size_bank1, sector_size, 0, AT32F43x_FLASH_BANK1_REG_OFFSET);

	// SRAM1 (64KB) can be remapped to 0x10000000.
	target_add_ram(target, 0x20000000, 64U * 1024U);
	// SRAM2 (384-64=320 KB default).
	target_add_ram(target, 0x20010000, 320U * 1024U);
	/*
	 * SRAM total is adjustable between 128 KB and 512 KB (max).
	 * Out of 640 KB SRAM present on silicon, at least 128 KB are always
	 * dedicated to "zero-wait-state Flash". ZW region is limited by
	 * specific part flash capacity (for 256, 448 KB) or at 512 KB.
	 * AT32F435ZMT default EOPB0=0xffff05fa,
	 * EOPB[0:2]=0b010 for 384 KB SRAM + 256 KB zero-wait-state flash.
	 */
	target->driver = "AT32F435";
	target->mass_erase = at32f43_mass_erase;
	return true;
}

/* Identify AT32F43x "High Performance" line devices (Cortex-M4) */
bool at32f43x_probe(target_s *target)
{
	// Artery clones use Cortex M4 cores
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	// Artery chips use the complete idcode word for identification
	const uint32_t idcode = target_mem32_read32(target, DBGMCU_IDCODE);
	const uint32_t series = idcode & AT32F4x_IDCODE_SERIES_MASK;
	const uint16_t part_id = idcode & AT32F4x_IDCODE_PART_MASK;

	if (series == AT32F43_SERIES_4K || series == AT32F43_SERIES_2K)
		return at32f43_detect(target, part_id);
	return false;
}

static bool at32f43_flash_unlock(target_s *const target, const uint32_t bank_reg_offset)
{
	if (target_mem32_read32(target, AT32F43x_FLASH_CTRL + bank_reg_offset) & AT32F43x_FLASH_CTRL_OPLK) {
		/* Enable FLASH operations in requested bank */
		target_mem32_write32(target, AT32F43x_FLASH_UNLOCK + bank_reg_offset, AT32F43x_FLASH_KEY1);
		target_mem32_write32(target, AT32F43x_FLASH_UNLOCK + bank_reg_offset, AT32F43x_FLASH_KEY2);
	}
	const uint32_t ctrlx = target_mem32_read32(target, AT32F43x_FLASH_CTRL + bank_reg_offset);
	if (ctrlx & AT32F43x_FLASH_CTRL_OPLK)
		DEBUG_ERROR("%s failed, CTRLx: 0x%08" PRIx32 "\n", __func__, ctrlx);
	return !(ctrlx & AT32F43x_FLASH_CTRL_OPLK);
}

static bool at32f43_flash_lock(target_s *const target, const uint32_t bank_reg_offset)
{
	uint32_t ctrlx_temp = target_mem32_read32(target, AT32F43x_FLASH_CTRL + bank_reg_offset);
	if ((ctrlx_temp & AT32F43x_FLASH_CTRL_OPLK) == 0U) {
		/* Disable FLASH operations in requested bank */
		ctrlx_temp |= AT32F43x_FLASH_CTRL_OPLK;
		target_mem32_write32(target, AT32F43x_FLASH_CTRL + bank_reg_offset, ctrlx_temp);
	}
	const uint32_t ctrlx = target_mem32_read32(target, AT32F43x_FLASH_CTRL + bank_reg_offset);
	if ((ctrlx & AT32F43x_FLASH_CTRL_OPLK) == 0U)
		DEBUG_ERROR("%s failed, CTRLx: 0x%08" PRIx32 "\n", __func__, ctrlx);
	return (ctrlx & AT32F43x_FLASH_CTRL_OPLK);
}

static inline void at32f43_flash_clear_eop(target_s *const target, const uint32_t bank_reg_offset)
{
	const uint32_t status = target_mem32_read32(target, AT32F43x_FLASH_STS + bank_reg_offset);
	target_mem32_write32(
		target, AT32F43x_FLASH_STS + bank_reg_offset, status | AT32F43x_FLASH_STS_ODF); /* ODF is W1C */
}

static bool at32f43_flash_busy_wait(
	target_s *const target, const uint32_t bank_reg_offset, platform_timeout_s *const timeout)
{
	/* Read FLASH_STS to poll for Operation Busy Flag */
	uint32_t status = AT32F43x_FLASH_STS_OBF;
	/* Checking for ODF/EOP requires methodically clearing the ODF */
	while (!(status & AT32F43x_FLASH_STS_ODF) && (status & AT32F43x_FLASH_STS_OBF)) {
		status = target_mem32_read32(target, AT32F43x_FLASH_STS + bank_reg_offset);
		if (target_check_error(target)) {
			DEBUG_ERROR("Lost communications with target\n");
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	}
	if (status & AT32F43x_FLASH_STS_PRGMERR) {
		DEBUG_ERROR("at32f43 flash error, STS: 0x%" PRIx32 "\n", status);
		return false;
	}
	return !(status & AT32F43x_FLASH_STS_PRGMERR);
}

static bool at32f43_flash_prepare(target_flash_s *target_flash)
{
	target_s *target = target_flash->t;
	const at32f43_flash_s *const flash = (at32f43_flash_s *)target_flash;
	const uint32_t bank_reg_offset = flash->bank_reg_offset;
	return at32f43_flash_unlock(target, bank_reg_offset);
}

static bool at32f43_flash_done(target_flash_s *target_flash)
{
	target_s *target = target_flash->t;
	const at32f43_flash_s *const flash = (at32f43_flash_s *)target_flash;
	const uint32_t bank_reg_offset = flash->bank_reg_offset;
	return at32f43_flash_lock(target, bank_reg_offset);
}

static bool at32f43_flash_erase(target_flash_s *target_flash, target_addr_t addr, size_t len)
{
	target_s *target = target_flash->t;
	const at32f43_flash_s *const flash = (at32f43_flash_s *)target_flash;
	const uint32_t bank_reg_offset = flash->bank_reg_offset;

	if (len != target_flash->blocksize) {
		DEBUG_ERROR(
			"%s: Requested erase length %zu does not match blocksize %zu!\n", __func__, len, target_flash->blocksize);
		return false;
	}

	at32f43_flash_clear_eop(target, bank_reg_offset);

	/* Prepare for page/sector erase */
	target_mem32_write32(target, AT32F43x_FLASH_CTRL + bank_reg_offset, AT32F43x_FLASH_CTRL_SECERS);
	/* Select erased sector by its address */
	target_mem32_write32(target, AT32F43x_FLASH_ADDR + bank_reg_offset, addr);
	/* Start sector erase operation */
	target_mem32_write32(
		target, AT32F43x_FLASH_CTRL + bank_reg_offset, AT32F43x_FLASH_CTRL_SECERS | AT32F43x_FLASH_CTRL_ERSTR);

	/* Datasheet: page erase takes 50ms (typ), 500ms (max) */
	return at32f43_flash_busy_wait(target, bank_reg_offset, NULL);
}

static bool at32f43_flash_write(target_flash_s *target_flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = target_flash->t;
	const at32f43_flash_s *const flash = (at32f43_flash_s *)target_flash;
	const uint32_t bank_reg_offset = flash->bank_reg_offset;
	const align_e psize = ALIGN_32BIT;

	at32f43_flash_clear_eop(target, bank_reg_offset);

	/* Write to bank corresponding to flash region */
	target_mem32_write32(target, AT32F43x_FLASH_CTRL + bank_reg_offset, AT32F43x_FLASH_CTRL_FPRGM);
	cortexm_mem_write_aligned(target, dest, src, len, psize);

	/* Datasheet: flash programming takes 50us (typ), 200us (max) */
	return at32f43_flash_busy_wait(target, bank_reg_offset, NULL);
}

static bool at32f43_mass_erase_bank(
	target_s *const target, const uint32_t bank_reg_offset, platform_timeout_s *const timeout)
{
	/* Unlock this bank */
	if (!at32f43_flash_unlock(target, bank_reg_offset))
		return false;
	at32f43_flash_clear_eop(target, bank_reg_offset);

	/* Flash mass erase start instruction */
	target_mem32_write32(target, AT32F43x_FLASH_CTRL + bank_reg_offset, AT32F43x_FLASH_CTRL_BANKERS);
	target_mem32_write32(
		target, AT32F43x_FLASH_CTRL + bank_reg_offset, AT32F43x_FLASH_CTRL_BANKERS | AT32F43x_FLASH_CTRL_ERSTR);

	return at32f43_flash_busy_wait(target, bank_reg_offset, timeout);
}

static bool at32f43_mass_erase(target_s *target)
{
	/* Datasheet: bank erase takes seconds to complete */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	if (!at32f43_mass_erase_bank(target, AT32F43x_FLASH_BANK1_REG_OFFSET, &timeout))
		return false;

	/* For dual-bank targets, mass erase bank 2 as well */
	const at32f43_flash_s *const flash = (at32f43_flash_s *)target->flash;
	if (flash->bank_split)
		return at32f43_mass_erase_bank(target, AT32F43x_FLASH_BANK2_REG_OFFSET, &timeout);
	return true;
}
