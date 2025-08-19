/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2024 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <11005378+ALTracer@users.noreply.github.com>
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
 * AT32F435/437 Series Reference Manual
 *   https://www.arterychip.com/download/RM/RM_AT32F435_437_EN_V2.04.pdf
 * AT32F402/405 Series Reference Manual
 *   https://www.arterychip.com/download/RM/RM_AT32F402_405_EN_V2.01.pdf
 * AT32F423 Series Reference Manual
 *   https://www.arterychip.com/download/RM/RM_AT32F423_EN_V2.03.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"

static bool at32f43_cmd_option(target_s *target, int argc, const char **argv);
static bool at32f43_cmd_uid(target_s *target, int argc, const char **argv);

const command_s at32f43_cmd_list[] = {
	{"option", at32f43_cmd_option, "Manipulate option bytes"},
	{"uid", at32f43_cmd_uid, "Print unique device ID"},
	{NULL, NULL, NULL},
};

static bool at32f43_flash_prepare(target_flash_s *flash);
static bool at32f43_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool at32f43_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool at32f43_flash_done(target_flash_s *flash);
static bool at32f43_mass_erase(target_s *target, platform_timeout_s *print_progess);

/* Flash memory controller register map */
#define AT32F43x_FLASH_REG_BASE   0x40023c00U
#define AT32F43x_FLASH_UNLOCK     (AT32F43x_FLASH_REG_BASE + 0x04U)
#define AT32F43x_FLASH_USD_UNLOCK (AT32F43x_FLASH_REG_BASE + 0x08U)
#define AT32F43x_FLASH_STS        (AT32F43x_FLASH_REG_BASE + 0x0cU)
#define AT32F43x_FLASH_CTRL       (AT32F43x_FLASH_REG_BASE + 0x10U)
#define AT32F43x_FLASH_ADDR       (AT32F43x_FLASH_REG_BASE + 0x14U)
#define AT32F43x_FLASH_USD        (AT32F43x_FLASH_REG_BASE + 0x1cU)
/* There is a second set of identical registers at +0x40 offset for Bank 2 */

#define AT32F43x_FLASH_BANK1_REG_OFFSET 0x00U
#define AT32F43x_FLASH_BANK2_REG_OFFSET 0x40U

/* Flash registers bit fields */
#define AT32F43x_FLASH_CTRL_FPRGM   (1U << 0U)
#define AT32F43x_FLASH_CTRL_SECERS  (1U << 1U)
#define AT32F43x_FLASH_CTRL_BANKERS (1U << 2U)
#define AT32F43x_FLASH_CTRL_USDPRGM (1U << 4U)
#define AT32F43x_FLASH_CTRL_USDERS  (1U << 5U)
#define AT32F43x_FLASH_CTRL_ERSTR   (1U << 6U)
#define AT32F43x_FLASH_CTRL_OPLK    (1U << 7U)
#define AT32F43x_FLASH_CTRL_USDULKS (1U << 9U)
/* CTRL bits 8, 11, [13:31] are reserved, parallellism x8/x16/x32 (don't care) */

/* OBF is BSY, ODF is EOP */
#define AT32F43x_FLASH_STS_OBF     (1U << 0U)
#define AT32F43x_FLASH_STS_PRGMERR (1U << 2U)
#define AT32F43x_FLASH_STS_EPPERR  (1U << 4U)
#define AT32F43x_FLASH_STS_ODF     (1U << 5U)

#define AT32F43x_FLASH_USD_RDP (1U << 1U)

#define AT32F43x_FLASH_KEY1 0x45670123U
#define AT32F43x_FLASH_KEY2 0xcdef89abU

#define AT32F43x_USD_BASE 0x1fffc000U
/* First option byte value for disabled read protection: 0x00a5 */
#define AT32F43x_USD_RDP_KEY 0x5aa5U
/* Extended Option Byte 0 default value for "On-chip 384 KB SRAM+256 KB zero-wait-state Flash" */
#define AT32F43x_USD_EOPB0_DEFAULT 0x05faU

#define AT32F43x_2K_OB_COUNT 256U
#define AT32F43x_4K_OB_COUNT 2048U

#define AT32F405_USD_BASE 0x1ffff800U
#define AT32F405_OB_COUNT 256U

/*
 * refman: DEBUG has 5 registers, of which CTRL, APB1_PAUSE, APB2_PAUSE are
 * "asynchronously reset by POR Reset (not reset by system reset). It can be written by the debugger under reset."
 * Note that it has no TRACE_IOEN and SWO is controlled by GPIO IOMUX (AF) instead.
 */
#define AT32F43x_DBGMCU_BASE       0xe0042000U
#define AT32F43x_DBGMCU_IDCODE     (AT32F43x_DBGMCU_BASE + 0x00U)
#define AT32F43x_DBGMCU_CTRL       (AT32F43x_DBGMCU_BASE + 0x04U)
#define AT32F43x_DBGMCU_APB1_PAUSE (AT32F43x_DBGMCU_BASE + 0x08U)
#define AT32F43x_DBGMCU_APB2_PAUSE (AT32F43x_DBGMCU_BASE + 0x0cU)
#define AT32F43x_DBGMCU_SER_ID     (AT32F43x_DBGMCU_BASE + 0x20U)

#define AT32F43x_DBGMCU_CTRL_SLEEP_DEBUG     (1U << 0U)
#define AT32F43x_DBGMCU_CTRL_DEEPSLEEP_DEBUG (1U << 1U)
#define AT32F43x_DBGMCU_CTRL_STANDBY_DEBUG   (1U << 2U)
#define AT32F43x_DBGMCU_CTRL_SLEEP_MASK \
	(AT32F43x_DBGMCU_CTRL_SLEEP_DEBUG | AT32F43x_DBGMCU_CTRL_DEEPSLEEP_DEBUG | AT32F43x_DBGMCU_CTRL_STANDBY_DEBUG)

#define AT32F43x_DBGMCU_APB1_PAUSE_WWDT (1U << 11U)
#define AT32F43x_DBGMCU_APB1_PAUSE_WDT  (1U << 12U)

#define AT32F4x_IDCODE_SERIES_MASK 0xfffff000U
#define AT32F4x_IDCODE_PART_MASK   0x00000fffU
#define AT32F43_SERIES_4K          0x70084000U
#define AT32F43_SERIES_2K          0x70083000U
#define AT32F405_SERIES_256KB      0x70053000U
#define AT32F405_SERIES_128KB      0x70042000U
#define AT32F423_SERIES_256KB      0x700a3000U
#define AT32F423_SERIES_128KB      0x700a2000U
#define AT32F423_SERIES_64KB       0x70032000U

#define AT32F4x_UID_BASE   0x1ffff7e8U
#define AT32F4x_PROJECT_ID 0x1ffff7f3U
#define AT32F4x_FLASHSIZE  0x1ffff7e0U

typedef struct at32f43_flash {
	target_flash_s target_flash;
	uint32_t bank_reg_offset; /* Flash register offset for this bank */
} at32f43_flash_s;

static void at32f43_add_flash(target_s *const target, const target_addr_t addr, const size_t length,
	const size_t pagesize, const uint32_t bank_reg_offset)
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
	flash->bank_reg_offset = bank_reg_offset;
	target_add_flash(target, target_flash);
}

static void at32f43_configure_dbgmcu(target_s *target)
{
	/*
	 * Enable sleep state emulation (clocks fed by HICK)
	 * and make both watchdogs pause during core halts so that they
	 * don't issue extra resets when we're doing e.g. flash reprogramming
	 */
	const uint32_t dbgmcu_ctrl = target_mem32_read32(target, AT32F43x_DBGMCU_CTRL);
	if ((dbgmcu_ctrl & AT32F43x_DBGMCU_CTRL_SLEEP_MASK) != AT32F43x_DBGMCU_CTRL_SLEEP_MASK)
		target_mem32_write32(target, AT32F43x_DBGMCU_CTRL,
			dbgmcu_ctrl | AT32F43x_DBGMCU_CTRL_SLEEP_DEBUG | AT32F43x_DBGMCU_CTRL_DEEPSLEEP_DEBUG |
				AT32F43x_DBGMCU_CTRL_STANDBY_DEBUG);

	const uint32_t dbgmcu_apb1_pause_mask = AT32F43x_DBGMCU_APB1_PAUSE_WWDT | AT32F43x_DBGMCU_APB1_PAUSE_WDT;
	const uint32_t dbgmcu_apb1_pause = target_mem32_read32(target, AT32F43x_DBGMCU_APB1_PAUSE);
	if ((dbgmcu_apb1_pause & dbgmcu_apb1_pause_mask) != dbgmcu_apb1_pause_mask)
		target_mem32_write32(target, AT32F43x_DBGMCU_APB1_PAUSE, dbgmcu_apb1_pause | dbgmcu_apb1_pause_mask);
}

static bool at32f43_attach(target_s *target)
{
	if (!cortexm_attach(target))
		return false;

	at32f43_configure_dbgmcu(target);
	return true;
}

static void at32f43_detach(target_s *target)
{
	const uint32_t dbgmcu_ctrl = target_mem32_read32(target, AT32F43x_DBGMCU_CTRL);
	const uint32_t dbgmcu_apb1_pause = target_mem32_read32(target, AT32F43x_DBGMCU_APB1_PAUSE);
	target_mem32_write32(target, AT32F43x_DBGMCU_CTRL, dbgmcu_ctrl & ~AT32F43x_DBGMCU_CTRL_SLEEP_MASK);
	target_mem32_write32(target, AT32F43x_DBGMCU_APB1_PAUSE,
		dbgmcu_apb1_pause & ~(AT32F43x_DBGMCU_APB1_PAUSE_WWDT | AT32F43x_DBGMCU_APB1_PAUSE_WDT));

	cortexm_detach(target);
}

/* Identify AT32F43x "High Performance" line devices */
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
	at32f43_add_flash(target, 0x08000000, flash_size_bank1, sector_size, AT32F43x_FLASH_BANK1_REG_OFFSET);
	if (flash_size_bank2 > 0)
		at32f43_add_flash(
			target, 0x08000000 + flash_size_bank1, flash_size_bank2, sector_size, AT32F43x_FLASH_BANK2_REG_OFFSET);

	// SRAM1 (64KB) can be remapped to 0x10000000.
	target_add_ram32(target, 0x20000000, 64U * 1024U);
	// SRAM2 (384-64=320 KB default).
	target_add_ram32(target, 0x20010000, 320U * 1024U);
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
	target_add_commands(target, at32f43_cmd_list, target->driver);
	target->attach = at32f43_attach;
	target->detach = at32f43_detach;

	at32f43_configure_dbgmcu(target);
	return true;
}

/* Identify AT32F405 Mainstream devices */
static bool at32f405_detect(target_s *target, const uint32_t series)
{
	/*
	 * AT32F405/F402 always contain 1 bank with 128 sectors
	 * Flash (C): 256 KiB, 2 KiB per sector, 0x7005_3000
	 * Flash (B): 128 KiB, 1 KiB per sector, 0x7004_2000
	 */
	const uint16_t flash_size = target_mem32_read16(target, AT32F4x_FLASHSIZE);
	const uint16_t sector_size = series == AT32F405_SERIES_128KB ? 1024U : 2048U;
	at32f43_add_flash(target, 0x08000000, flash_size * 1024U, sector_size, AT32F43x_FLASH_BANK1_REG_OFFSET);

	/*
	 * Either 96 or 102 KiB of SRAM, depending on USD bit 7 nRAM_PRT_CHK:
	 * when first 48 KiB are protected by odd parity, last 6 KiB are reserved for this purpose
	 */
	target_add_ram32(target, 0x20000000, 102U * 1024U);
	target->driver = "AT32F405";
	target->mass_erase = at32f43_mass_erase;

	/* 512 byte User System Data area at 0x1fff_f800 (different USD_BASE, no EOPB0) */
	//target_add_commands(target, at32f43_cmd_list, target->driver);

	/* Same registers and freeze bits in DBGMCU as F437 */
	target->attach = at32f43_attach;
	target->detach = at32f43_detach;
	at32f43_configure_dbgmcu(target);

	return true;
}

/* Identify AT32F423 Value line devices */
static bool at32f423_detect(target_s *target, const uint32_t series)
{
	/*
	 * AT32F423 always has 48 KiB of SRAM and one of
	 * Flash (C): 256 KiB, 2 KiB per sector, 0x700a_3000
	 * Flash (B): 128 KiB, 1 KiB per sector, 0x700a_2000
	 * Flash (8):  64 KiB, 1 KiB per sector, 0x7003_2000
	 */
	const uint16_t flash_size = target_mem32_read16(target, AT32F4x_FLASHSIZE);
	const uint16_t sector_size = series == AT32F423_SERIES_256KB ? 2048U : 1024U;
	at32f43_add_flash(target, 0x08000000, flash_size * 1024U, sector_size, AT32F43x_FLASH_BANK1_REG_OFFSET);

	target_add_ram32(target, 0x20000000, 48U * 1024U);
	target->driver = "AT32F423";
	target->mass_erase = at32f43_mass_erase;

	/* 512 byte User System Data area at 0x1fff_f800 (different USD_BASE, no EOPB0) */
	//target_add_commands(target, at32f43_cmd_list, target->driver);

	/* Same registers and freeze bits in DBGMCU as F437 */
	target->attach = at32f43_attach;
	target->detach = at32f43_detach;
	at32f43_configure_dbgmcu(target);

	return true;
}

/* Identify any Arterytek devices with Cortex-M4 and FPEC at 0x4002_3c00 */
bool at32f43x_probe(target_s *target)
{
	// Artery clones use Cortex M4 cores
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	// Artery chips use the complete idcode word for identification
	const uint32_t idcode = target_mem32_read32(target, AT32F43x_DBGMCU_IDCODE);
	const uint32_t series = idcode & AT32F4x_IDCODE_SERIES_MASK;
	const uint16_t part_id = idcode & AT32F4x_IDCODE_PART_MASK;
	// ... and another word from PPB
	const uint32_t debug_ser_id = target_mem32_read32(target, AT32F43x_DBGMCU_SER_ID);
	const uint8_t project_id = (debug_ser_id >> 8U) & 0xffU;

#ifndef DEBUG_TARGET_IS_NOOP
	// ... and/or highest byte of UID, which reads as 0xff under Read Protection
	const uint8_t uid_byte = target_mem32_read8(target, AT32F4x_PROJECT_ID);
	const uint32_t flash_usd = target_mem32_read32(target, AT32F43x_FLASH_USD);
	const bool read_protected = (flash_usd & AT32F43x_FLASH_USD_RDP) == AT32F43x_FLASH_USD_RDP;
	if (read_protected)
		DEBUG_TARGET("%s: Flash Access Protection enabled, UID reads as 0x%02x\n", __func__, uid_byte);

	DEBUG_TARGET("%s: idcode = %08" PRIx32 ", uid_byte = %02x, debug_ser_id = %08" PRIx32 "\n", __func__, idcode,
		uid_byte, debug_ser_id);
#endif

	/* 0x0e: F437 (has EMAC), 0x0d: F435 (no EMAC). 4K/2K describe sector sizes, not total flash capacity. */
	if ((series == AT32F43_SERIES_4K || series == AT32F43_SERIES_2K) && (project_id == 0x0dU || project_id == 0x0eU))
		return at32f43_detect(target, part_id);
	/* 0x13: F405 (has USB HS), 0x14: F402 (no USB HS) */
	if ((series == AT32F405_SERIES_256KB || series == AT32F405_SERIES_128KB) &&
		(project_id == 0x13U || project_id == 0x14U))
		return at32f405_detect(target, series);
	if ((series == AT32F423_SERIES_256KB || series == AT32F423_SERIES_128KB || series == AT32F423_SERIES_64KB) &&
		project_id == 0x12U)
		return at32f423_detect(target, series);

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
	DEBUG_TARGET("%s: 0x%08" PRIX32 "+%" PRIu32 " reg_base 0x%08" PRIX32 "\n", __func__, addr, (uint32_t)len,
		bank_reg_offset + AT32F43x_FLASH_REG_BASE);

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
	DEBUG_TARGET("%s: 0x%08" PRIX32 "+%" PRIu32 " reg_base 0x%08" PRIX32 "\n", __func__, dest, (uint32_t)len,
		bank_reg_offset + AT32F43x_FLASH_REG_BASE);

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

static bool at32f43_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	/* Datasheet: bank erase takes seconds to complete */
	if (!at32f43_mass_erase_bank(target, AT32F43x_FLASH_BANK1_REG_OFFSET, print_progess))
		return false;

	/* For dual-bank targets, mass erase bank 2 as well */
	if (target->flash->next)
		return at32f43_mass_erase_bank(target, AT32F43x_FLASH_BANK2_REG_OFFSET, print_progess);
	return true;
}

/* Borrow macros from adiv5_jtag.c */
#define JTAGDP_ACK_OK   0x02U
#define JTAGDP_ACK_WAIT 0x01U

#define IR_DPACC 0xaU
#define IR_APACC 0xbU

uint32_t adiv5_jtag_raw_access_noabort(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value)
{
	const bool is_ap = addr & ADIV5_APnDP;
	addr &= 0xffU;

	const uint64_t request = ((uint64_t)value << 3U) | ((addr >> 1U) & 0x06U) | (rnw ? 1U : 0U);

	uint32_t result = 0U;
	uint8_t ack = JTAGDP_ACK_WAIT;

	jtag_dev_write_ir(dp->dev_index, is_ap ? IR_APACC : IR_DPACC);

	platform_timeout_s timeout_progressbar;
	platform_timeout_set(&timeout_progressbar, 500U);
	platform_timeout_s timeout_erase;
	platform_timeout_set(&timeout_erase, 15000U);
	while (ack == JTAGDP_ACK_WAIT && !platform_timeout_is_expired(&timeout_erase)) {
		uint64_t response;
		jtag_dev_shift_dr(dp->dev_index, (uint8_t *)&response, (const uint8_t *)&request, 35);
		result = response >> 3U;
		ack = response & 0x07U;
		platform_delay(5U);
		target_print_progress(&timeout_progressbar);
	}

	if (ack != JTAGDP_ACK_OK) {
		DEBUG_ERROR("JTAG access resulted in: %" PRIx32 ":%x\n", result, ack);
		raise_exception(EXCEPTION_ERROR, "JTAG-DP invalid ACK");
	}

	return result;
}

static bool adiv5_swd_raw_access_noabort(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value)
{
	const uint8_t request = make_packet_request(rnw, addr);
	uint32_t response = 0;
	uint8_t ack = SWDP_ACK_WAIT;
	platform_timeout_s timeout_progressbar;
	platform_timeout_set(&timeout_progressbar, 500U);
	platform_timeout_s timeout_erase;
	platform_timeout_set(&timeout_erase, 15000U);
	while ((ack == SWDP_ACK_WAIT) && !platform_timeout_is_expired(&timeout_erase)) {
		swd_proc.seq_out(request, 8U);
		ack = swd_proc.seq_in(3U);
		/* No data phase */
		platform_delay(5U);
		target_print_progress(&timeout_progressbar);
	}

	if (ack != SWDP_ACK_OK) {
		DEBUG_ERROR("SWD access has invalid ack %x\n", ack);
		raise_exception(EXCEPTION_ERROR, "SWD invalid ACK");
	}

	if (platform_timeout_is_expired(&timeout_erase)) {
		DEBUG_ERROR("%s timed out after %u ms\n", __func__, 15000U);
		raise_exception(EXCEPTION_TIMEOUT, "SWD WAIT");
	}

	if (rnw) {
		if (!swd_proc.seq_in_parity(&response, 32U)) {
			dp->fault = 1U;
			DEBUG_ERROR("SWD access resulted in parity error\n");
			raise_exception(EXCEPTION_ERROR, "SWD parity error");
		}
	} else
		swd_proc.seq_out_parity(value, 32U);
	/* Idle cycles */
	swd_proc.seq_out(0, 8U);
	return response;
}

static bool at32f43x_mem_write_noabort(target_s *target, target_addr32_t dest, uint16_t val)
{
	const uint32_t src_bytes = val;
	const void *src = &src_bytes;
	const align_e align = ALIGN_16BIT;
	adiv5_access_port_s *ap = cortex_ap(target);

	//adi_ap_mem_access_setup(ap, dest, align);
	uint32_t csw = ap->csw | ADIV5_AP_CSW_ADDRINC_SINGLE | ADIV5_AP_CSW_SIZE_HALFWORD;
	adiv5_ap_write(ap, ADIV5_AP_CSW, csw);
	adiv5_dp_write(ap->dp, ADIV5_AP_TAR_LOW, (uint32_t)dest);

	uint32_t value = 0;
	adiv5_pack_data(dest, src, &value, align);
	/* Submit the memory write */
	adiv5_dp_write(ap->dp, ADIV5_AP_DRW, value);

	/* Poll for completion (RDBUFF will be responding with WAITs) */
	volatile uint32_t rdbuff = 0;
	TRY (EXCEPTION_ALL) {
		//ack = ap->dp->low_access(dp, rnw, addr, value)
		if (ap->dp->low_access == adiv5_swd_raw_access)
			rdbuff = adiv5_swd_raw_access_noabort(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
		else if (ap->dp->low_access == adiv5_jtag_raw_access)
			rdbuff = adiv5_jtag_raw_access_noabort(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
	}
	CATCH () {
	case EXCEPTION_TIMEOUT:
		DEBUG_TARGET("Timeout during scan. Is target stuck in WFI?\n");
		break;
	case EXCEPTION_ERROR:
		DEBUG_TARGET("Exception: %s\n", exception_frame.msg);
		break;
	default:
		return false;
	}
	(void)rdbuff;

	return true;
}

static bool at32f43_option_erase(target_s *target)
{
	/* bank_reg_offset is 0, option bytes belong to first bank */
	at32f43_flash_clear_eop(target, 0);
	DEBUG_TARGET("%s\n", __func__);

	/* Wipe User System Data */
	target_mem32_write32(target, AT32F43x_FLASH_CTRL, AT32F43x_FLASH_CTRL_USDERS | AT32F43x_FLASH_CTRL_USDULKS);
	target_mem32_write32(target, AT32F43x_FLASH_CTRL,
		AT32F43x_FLASH_CTRL_USDERS | AT32F43x_FLASH_CTRL_USDULKS | AT32F43x_FLASH_CTRL_ERSTR);

	return at32f43_flash_busy_wait(target, 0, NULL);
}

static bool at32f43_option_write_erased(target_s *const target, const size_t offset, const uint16_t value)
{
	if (value == 0xffffU)
		return true;

	at32f43_flash_clear_eop(target, 0);

	/* Enable writing User System Data */
	target_mem32_write32(target, AT32F43x_FLASH_CTRL, AT32F43x_FLASH_CTRL_USDPRGM | AT32F43x_FLASH_CTRL_USDULKS);

	const uint32_t addr = AT32F43x_USD_BASE + (offset * 2U);
	DEBUG_TARGET("%s: 0x%08" PRIX32 " <- 0x%04X\n", __func__, addr, value);
	const uint32_t time_start = platform_time_ms();
	target_mem32_write16(target, addr, value);

	const bool result = at32f43_flash_busy_wait(target, 0, NULL);
	const uint32_t time_end = platform_time_ms();
	const uint32_t time_spent = time_end - time_start;
	if (time_spent > 20U)
		DEBUG_TARGET("%s: took %" PRIu32 " ms\n", __func__, time_spent);
	if (result || offset != 0U)
		return result;

	/* For error on offset 0, that is RDP byte, signal back the failure to erase RDP (?) */
	const uint8_t status =
		target_mem32_read32(target, AT32F43x_FLASH_STS) & (AT32F43x_FLASH_STS_PRGMERR | AT32F43x_FLASH_STS_EPPERR);
	return status == AT32F43x_FLASH_STS_PRGMERR;
}

static bool at32f43_option_overwrite(target_s *const target, const uint16_t *const opt_val, const uint16_t ob_count)
{
	if (!at32f43_option_erase(target))
		return false;

	/* Write changed values using 16-bit accesses. */
	for (size_t i = 0U; i < ob_count; ++i) {
		if (!at32f43_option_write_erased(target, i, opt_val[i]))
			return false;
	}

	return true;
}

static bool at32f43_option_write(target_s *const target, const uint32_t addr, const uint16_t value)
{
	/* Arterytek F435/F437 has either 512 bytes or 4 KiB worth of USD */
	const target_flash_s *target_flash = target->flash;
	const uint16_t ob_count = target_flash->blocksize == 4096U ? AT32F43x_4K_OB_COUNT : AT32F43x_2K_OB_COUNT;

	const uint32_t index = (addr - AT32F43x_USD_BASE) >> 1U;
	/* If this underflows, then address is out of USD range */
	if (index > ob_count - 1U)
		return false;

	bool erase_needed = false;
	uint16_t opt_val_single = target_mem32_read16(target, addr);
	/* No change pending */
	if (opt_val_single == value)
		return true;
	/* Check whether erase is needed */
	if (opt_val_single != 0xffffU)
		erase_needed = true;
	/* Flip single pair-of-bytes from 0xffff to desired value and exit */
	if (!erase_needed)
		return at32f43_option_write_erased(target, index, value);

	uint16_t *const opt_val = calloc(ob_count, sizeof(uint16_t));
	if (!opt_val) {
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	DEBUG_TARGET("%s: full overwrite triggered\n", __func__);

	/* Save current values */
	for (size_t i = 0U; i < ob_count * 2U; i += 4U) {
		const size_t offset = i >> 1U;
		uint32_t val = target_mem32_read32(target, AT32F43x_USD_BASE + i);
		opt_val[offset] = val & 0xffffU;
		opt_val[offset + 1U] = val >> 16U;
	}
	/* Update requested entry locally */
	opt_val[index] = value;

	/* Wipe everything and write back. Writing matching values without an erase raises a PRGMERR. */
	const bool result = at32f43_option_overwrite(target, opt_val, ob_count);

	free(opt_val);
	return result;
}

static bool at32f43_cmd_option(target_s *target, int argc, const char **argv)
{
	const uint32_t read_protected = target_mem32_read32(target, AT32F43x_FLASH_USD) & AT32F43x_FLASH_USD_RDP;
	const bool erase_requested = argc == 2 && strcmp(argv[1], "erase") == 0;
	/* Fast-exit if the Flash is not readable and the user didn't ask us to erase the option bytes */
	if (read_protected && !erase_requested) {
		tc_printf(target, "Device is Read Protected\nUse `monitor option erase` to unprotect and erase device\n");
		return true;
	}

	/* Unprotect the option bytes so we can modify them */
	if (!at32f43_flash_unlock(target, 0))
		return false;
	target_mem32_write32(target, AT32F43x_FLASH_USD_UNLOCK, AT32F43x_FLASH_KEY1);
	target_mem32_write32(target, AT32F43x_FLASH_USD_UNLOCK, AT32F43x_FLASH_KEY2);

	if (erase_requested) {
		/* When the user asks us to erase the option bytes, kick off an erase */
		if (!at32f43_option_erase(target))
			return false;
		/*
		 * Write the option bytes Flash readable key.
		 * FIXME: this transaction only completes after typ. 15 seconds (mass erase of both banks of 4032 KiB chip)
		 * and if BMD ABORTs it after 250 ms, then chip considers erase as incomplete and stays read-protected.
		 */
		at32f43_flash_clear_eop(target, 0);
		target_mem32_write32(target, AT32F43x_FLASH_CTRL, AT32F43x_FLASH_CTRL_USDPRGM | AT32F43x_FLASH_CTRL_USDULKS);
		if (!at32f43x_mem_write_noabort(target, AT32F43x_USD_BASE, AT32F43x_USD_RDP_KEY))
			return false;

		/* Set EOPB0 to default 0b010 for 384 KB SRAM */
		if (!at32f43_option_write_erased(target, 8U, AT32F43x_USD_EOPB0_DEFAULT))
			return false;
	} else if (argc == 3) {
		/* If 3 arguments are given, assume the second is an address, and the third a value */
		const uint32_t addr = strtoul(argv[1], NULL, 0);
		const uint32_t val = strtoul(argv[2], NULL, 0);
		/* Try and program the new option value to the requested option byte */
		if (!at32f43_option_write(target, addr, val))
			return false;
		/* Display only changes */
		const uint16_t val_new = target_mem32_read16(target, addr);
		tc_printf(target, "0x%08" PRIX32 ": 0x%04X\n", addr, val_new);
		return true;
	} else
		tc_printf(target, "usage: monitor option erase\nusage: monitor option <addr> <value>\n");

	/* When all gets said and done, display the current option bytes values */
	const target_flash_s *target_flash = target->flash;
	const uint16_t ob_count = target_flash->blocksize == 4096U ? AT32F43x_4K_OB_COUNT : AT32F43x_2K_OB_COUNT;
	uint16_t values[8] = {0};
	for (size_t i = 0U; i < ob_count * 2U; i += 16U) {
		const uint32_t addr = AT32F43x_USD_BASE + i;
		target_mem32_read(target, values, addr, 8 * sizeof(uint16_t));
		tc_printf(target, "0x%08" PRIX32 ": 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X\n", addr, values[0],
			values[1], values[2], values[3], values[4], values[5], values[6], values[7]);
	}

	return true;
}

static bool at32f43_cmd_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return stm32_uid(target, AT32F4x_UID_BASE);
}
