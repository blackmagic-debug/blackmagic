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

#define DBGMCU_IDCODE 0xe0042000U

#define AT32F4x_IDCODE_SERIES_MASK 0xfffff000U
#define AT32F4x_IDCODE_PART_MASK   0x00000fffU
#define AT32F43_SERIES_4K          0x70084000U
#define AT32F43_SERIES_2K          0x70083000U

static void at32f43_add_flash(target_s *const t, const uint32_t addr, const size_t length, const size_t blocksize,
	const uint8_t base_sector, const uint8_t split)
{
	if (length == 0)
		return;

	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = NULL;
	f->write = NULL;
	(void)base_sector;
	(void)split;
	f->writesize = 1024;
	f->erased = 0xffU;
	target_add_flash(t, f);
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
		// Flash (G): 4032 KB in 2 banks (2048+1984), 4KB per sector.
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
		// Flash (M): 1024 KB in 2 banks (equal), 2KB per sector.
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
	at32f43_add_flash(target, 0x08000000, flash_size_bank1, sector_size, 0, 0);
	if (flash_size_bank2 > 0) {
		const uint16_t base_sector = flash_size_bank1 / sector_size;
		at32f43_add_flash(target, 0x08000000 + flash_size_bank1, flash_size_bank2, sector_size, base_sector, 0);
	}

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
	target->mass_erase = NULL;
	return true;
}

/* Identify AT32F43x "High Performance" line devices (Cortex-M4) */
bool at32f43x_probe(target_s *target)
{
	// Artery clones use Cortex M4 cores
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	// Artery chips use the complete idcode word for identification
	const uint32_t idcode = target_mem_read32(target, DBGMCU_IDCODE);
	const uint32_t series = idcode & AT32F4x_IDCODE_SERIES_MASK;
	const uint16_t part_id = idcode & AT32F4x_IDCODE_PART_MASK;

	if (series == AT32F43_SERIES_4K || series == AT32F43_SERIES_2K)
		return at32f43_detect(target, part_id);
	return false;
}
