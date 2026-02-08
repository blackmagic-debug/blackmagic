/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024-2026 hardesk <hardesk17@gmail.com>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "buffer_utils.h"
#include "jep106.h"
#include "cortexm.h"

#define MSPM0_SRAM_BASE            0x20000000U
#define MSPM0_FLASH_MAIN           0x00000000U
#define MSPM0_FLASH_NONMAIN        0x41c00000U /* One Sector, BANK0. Device boot configuration (BCR, BSL) */
#define MSPM0_FLASH_FACTORY        0x41c40000U /* One Sector, BANK0. Non modifiable */
#define MSPM0_FLASH_DATA           0x41d00000U
#define MSPM0_FLASH_SECTOR_SZ      1024U
#define MSPM0_FLASH_WRITE_CHUNK_SZ MSPM0_FLASH_SECTOR_SZ

#define MSPM0_FACTORYREGION_DEVICEID  (MSPM0_FLASH_FACTORY + 0x4U)
#define MSPM0_FACTORYREGION_SRAMFLASH (MSPM0_FLASH_FACTORY + 0x18U)

#define MSPM0_DEVICEID_MANUFACTURER_MASK  0x00000ffeU
#define MSPM0_DEVICEID_MANUFACTURER_SHIFT 1U
#define MSPM0_DEVICEID_PARTNUM_MASK       0x0ffff000U
#define MSPM0_DEVICEID_PARTNUM_SHIFT      12U

#define MSPM0_FACTORYREGION_SRAMFLASH_MAINFLASH_SZ_MASK  0x00000fffU
#define MSPM0_FACTORYREGION_SRAMFLASH_MAINFLASH_SZ_SHIFT 0U
#define MSPM0_FACTORYREGION_SRAMFLASH_MAINNUMBANKS_MASK  0x00003000U
#define MSPM0_FACTORYREGION_SRAMFLASH_MAINNUMBANKS_SHIFT 12U
#define MSPM0_FACTORYREGION_SRAMFLASH_SRAM_SZ_MASK       0x03ff0000U
#define MSPM0_FACTORYREGION_SRAMFLASH_SRAM_SZ_SHIFT      16U
#define MSPM0_FACTORYREGION_SRAMFLASH_DATAFLASH_SZ_MASK  0xfc0000U
#define MSPM0_FACTORYREGION_SRAMFLASH_DATAFLASH_SZ_SHIFT 26U

#define MSPM0_FLASHCTL_BASE              0x400cd000U
#define MSPM0_FLASHCTL_CMDEXEC           (MSPM0_FLASHCTL_BASE + 0x1100U)
#define MSPM0_FLASHCTL_CMDTYPE           (MSPM0_FLASHCTL_BASE + 0x1104U)
#define MSPM0_FLASHCTL_CMDCTL            (MSPM0_FLASHCTL_BASE + 0x1108U)
#define MSPM0_FLASHCTL_CMDADDR           (MSPM0_FLASHCTL_BASE + 0x1120U)
#define MSPM0_FLASHCTL_BYTEN             (MSPM0_FLASHCTL_BASE + 0x1124U)
#define MSPM0_FLASHCTL_CMDDATAIDX        (MSPM0_FLASHCTL_BASE + 0x112cU)
#define MSPM0_FLASHCTL_STATCMD           (MSPM0_FLASHCTL_BASE + 0x13d0U)
#define MSPM0_FLASHCTL_CMDDATA0          (MSPM0_FLASHCTL_BASE + 0x1130U)
#define MSPM0_FLASHCTL_CMDDATA1          (MSPM0_FLASHCTL_BASE + 0x1134U)
#define MSPM0_FLASHCTL_CMDDATA2          (MSPM0_FLASHCTL_BASE + 0x1138U)
#define MSPM0_FLASHCTL_CMDDATA3          (MSPM0_FLASHCTL_BASE + 0x113cU)
#define MSPM0_FLASHCTL_CMDWEPROTA        (MSPM0_FLASHCTL_BASE + 0x11d0U)
#define MSPM0_FLASHCTL_CMDWEPROTB        (MSPM0_FLASHCTL_BASE + 0x11d4U)
#define MSPM0_FLASHCTL_CMDWEPROTC        (MSPM0_FLASHCTL_BASE + 0x11d8U)
#define MSPM0_FLASHCTL_CMDWEPROTNM       (MSPM0_FLASHCTL_BASE + 0x1210U)
#define MSPM0_FLASHCTL_CMDTYPE_NOOP      0U
#define MSPM0_FLASHCTL_CMDTYPE_PROG      1U
#define MSPM0_FLASHCTL_CMDTYPE_ERASE     2U
#define MSPM0_FLASHCTL_CMDTYPE_RDVERIFY  3U
#define MSPM0_FLASHCTL_CMDTYPE_BLVERIFY  6U
#define MSPM0_FLASHCTL_CMDTYPE_SZ_1WORD  (0U << 4U)
#define MSPM0_FLASHCTL_CMDTYPE_SZ_2WORDS (1U << 4U)
#define MSPM0_FLASHCTL_CMDTYPE_SZ_4WORDS (2U << 4U)
#define MSPM0_FLASHCTL_CMDTYPE_SZ_8WORDS (3U << 4U)
#define MSPM0_FLASHCTL_CMDTYPE_SZ_SECTOR (4U << 4U)
#define MSPM0_FLASHCTL_CMDTYPE_SZ_BANK   (5U << 4U)
#define MSPM0_FLASHCTL_CMDEXEC_EXEC      1U
#define MSPM0_FLASHCTL_STAT_DONE         0x01U
#define MSPM0_FLASHCTL_STAT_CMDPASS      0x02U

typedef struct mspm0_flash {
	target_flash_s target_flash;
	uint32_t banks;
} mspm0_flash_s;

static uint16_t mspm0_partnums[] = {
	0xbba1U, /* MSPM0C: 1103 1104 1103-Q1 1104-Q1 */
	0x0bbbU, /* MSPM0C: 1105-Q1 1106-Q1 */
	0xbbbaU, /* MSPM0C: 1105 1106 */
	0xbb82U, /* MSPM0L: 1105 1106 1304 1305 1305 1344 1345 1346 1345-Q1 1346-Q1 */
	0xbb9fU, /* MSPM0L: 1227 1228 2227 2228 1227-Q1 1228-Q1 2227-Q1 2228-Q1 */
	0xbbb4U, /* MSPM0L: 1116 1117 1116-Q1 1117-Q1 */
	0xbbc7U, /* MSPM0L: 1126 1127 2116 2117 */
	0x0bbaU, /* MSPM0H: 3215 3216 */
	0xbb88U, /* MSPM0G: 1105 1106 1107 1505 1506 1507 3105 3106 3107 3505 3506 3507 3105-Q1 3106-Q1 3107-Q1 */
			 /*         3505-Q1 3506-Q1 3507-Q1 */
	0xbba9U, /* MSPM0G: 1518 1519 3518 3519 3518-Q1 3519-Q1 3529-Q1 */
	0xbbbcU, /* MSPM0G: 5187 */
	0xbbceU, /* MSPM0G: 1207 1218 3207 3218 */
};

static const uint16_t mspm0_flash_write_stub[] = {
#include "flashstub/mspm0.stub"
};
#define STUB_BUFFER_BASE ALIGN(MSPM0_SRAM_BASE + sizeof(mspm0_flash_write_stub), 4)

static bool mspm0_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool mspm0_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);
static bool mspm0_mass_erase(target_s *target, platform_timeout_s *print_progess);

static void mspm0_add_flash(
	target_s *const target, const uint32_t base, const size_t length, const uint32_t banks, uint32_t write_size)
{
	mspm0_flash_s *const flash = calloc(1, sizeof(*flash));
	if (flash == NULL) {
		DEBUG_WARN("%s: calloc failed for %" PRIu32 " bytes\n", __func__, (uint32_t)length);
		return;
	}

	flash->banks = banks;
	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = base;
	target_flash->length = length;
	target_flash->blocksize = MSPM0_FLASH_SECTOR_SZ;
	target_flash->writesize = write_size;
	target_flash->erase = mspm0_flash_erase;
	target_flash->write = mspm0_flash_write;
	target_flash->erased = 0xffU;
	target_add_flash(target, target_flash);
}

bool mspm0_probe(target_s *const target)
{
	const uint32_t deviceid = target_mem32_read32(target, MSPM0_FACTORYREGION_DEVICEID);
	const uint32_t manufacturer = (deviceid & MSPM0_DEVICEID_MANUFACTURER_MASK) >> MSPM0_DEVICEID_MANUFACTURER_SHIFT;
	if (manufacturer != JEP106_MANUFACTURER_TEXAS)
		return false;

	const uint32_t partnum = (deviceid & MSPM0_DEVICEID_PARTNUM_MASK) >> MSPM0_DEVICEID_PARTNUM_SHIFT;
	size_t partnum_idx = 0;
	for (; partnum_idx < ARRAY_LENGTH(mspm0_partnums); ++partnum_idx) {
		if (partnum == mspm0_partnums[partnum_idx])
			break;
	}
	if (partnum_idx >= ARRAY_LENGTH(mspm0_partnums))
		return false;

	target->driver = "MSPM0";
	target->target_options |= TOPT_INHIBIT_NRST;
	target->mass_erase = mspm0_mass_erase;

	const uint32_t sramflash = target_mem32_read32(target, MSPM0_FACTORYREGION_SRAMFLASH);
	const uint32_t mainflash_size = 1024U *
		((sramflash & MSPM0_FACTORYREGION_SRAMFLASH_MAINFLASH_SZ_MASK) >>
			MSPM0_FACTORYREGION_SRAMFLASH_MAINFLASH_SZ_SHIFT);
	const uint32_t main_num_banks = (sramflash & MSPM0_FACTORYREGION_SRAMFLASH_MAINNUMBANKS_MASK) >>
		MSPM0_FACTORYREGION_SRAMFLASH_MAINNUMBANKS_SHIFT;
	const uint32_t sram_size = 1024U *
		((sramflash & MSPM0_FACTORYREGION_SRAMFLASH_SRAM_SZ_MASK) >> MSPM0_FACTORYREGION_SRAMFLASH_SRAM_SZ_SHIFT);
	const uint32_t dataflash_size = 1024U *
		((sramflash & MSPM0_FACTORYREGION_SRAMFLASH_DATAFLASH_SZ_MASK) >>
			MSPM0_FACTORYREGION_SRAMFLASH_DATAFLASH_SZ_SHIFT);

	target_add_ram32(target, MSPM0_SRAM_BASE, sram_size);

	/* Decrease flash write size until it fits within available RAM */
	uint32_t write_size = MSPM0_FLASH_WRITE_CHUNK_SZ;
	uint32_t avail_ram = sram_size - (STUB_BUFFER_BASE - MSPM0_SRAM_BASE);
	while (write_size > avail_ram)
		write_size >>= 1U;

	mspm0_add_flash(target, MSPM0_FLASH_MAIN, mainflash_size, main_num_banks, write_size);
	if (dataflash_size != 0)
		mspm0_add_flash(target, MSPM0_FLASH_DATA, dataflash_size, 1U, write_size);

	return true;
}

/* Wait for a flash command to finish and return the status word or 0 on timeout */
static uint32_t mspm0_flash_wait_done(target_s *const target)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500U);

	uint32_t status = 0U;
	while (!(status & MSPM0_FLASHCTL_STAT_DONE)) {
		status = target_mem32_read32(target, MSPM0_FLASHCTL_STATCMD);
		if (platform_timeout_is_expired(&timeout))
			return 0U;
	}

	return status;
}

static void mspm0_flash_unprotect(target_flash_s *const target_flash)
{
	target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTA, 0U);
	target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTB, 0U);
	target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTC, 0U);
}

static void mspm0_flash_unprotect_sector(target_flash_s *const target_flash, const target_addr_t addr)
{
	mspm0_flash_s *mspm0_flash = (mspm0_flash_s *)target_flash;
	uint32_t sector = (addr - target_flash->start) / MSPM0_FLASH_SECTOR_SZ;

	if (sector < 32U) { /* One sector per bit */
		uint32_t mask = ~(1U << sector);
		target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTA, mask);
	} else if (sector < 256U) { /* 8 sectors per bit */
		/*
		 * Sectors affected by PROTB depend on the flash configuration. In single-bank
		 * main flash, PROTB applies to sectors after those affected by PROTA
		 * (that is, starting at sector 32). In multi-bank configurations, PROTA overlaps
		 * PROTB, so PROTB applies starting at sector 0.
		 */
		uint32_t start_protb_sector = mspm0_flash->banks > 1U ? 0U : 32U;
		uint32_t mask = ~(1U << ((sector - start_protb_sector) >> 3U));
		target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTB, mask);
	} else { /* 8 sectors per bit, starts at sector 256 */
		uint32_t mask = ~(1U << ((sector - 256U) >> 3U));
		target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTC, mask);
	}
}

static bool mspm0_flash_erase(target_flash_s *const target_flash, const target_addr_t addr, const size_t length)
{
#ifdef DEBUG_TARGET_IS_NOOP
	(void)length;
#endif

	target_s *const target = target_flash->t;

	mspm0_flash_unprotect_sector(target_flash, addr);
	target_mem32_write32(
		target, MSPM0_FLASHCTL_CMDTYPE, MSPM0_FLASHCTL_CMDTYPE_SZ_SECTOR | MSPM0_FLASHCTL_CMDTYPE_ERASE);
	target_mem32_write32(target, MSPM0_FLASHCTL_BYTEN, 0xffffffffU);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDCTL, 0U);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDADDR, addr);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDEXEC, MSPM0_FLASHCTL_CMDEXEC_EXEC);

	const uint32_t status = mspm0_flash_wait_done(target);
	if (!(status & MSPM0_FLASHCTL_STAT_CMDPASS))
		DEBUG_TARGET("%s: Failed to erase flash, status %08" PRIx32 " addr %08" PRIx32 " length %08" PRIx32 "\n",
			__func__, status, addr, (uint32_t)length);

	return status & MSPM0_FLASHCTL_STAT_CMDPASS;
}

static bool mspm0_flash_write(
	target_flash_s *const target_flash, target_addr_t dest, const void *const src, const size_t length)
{
	target_s *const target = target_flash->t;
	DEBUG_TARGET(
		"%s: Writing flash addr %08" PRIx32 " length %08" PRIx32 "\n", __func__, (uint32_t)dest, (uint32_t)length);

	target_mem32_write(target, MSPM0_SRAM_BASE, mspm0_flash_write_stub, sizeof(mspm0_flash_write_stub));
	target_mem32_write(target, STUB_BUFFER_BASE, src, length);

	return cortexm_run_stub(target, MSPM0_SRAM_BASE, dest, STUB_BUFFER_BASE, length, 0);
}

static bool mspm0_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	bool success = true;
	for (mspm0_flash_s *flash = (mspm0_flash_s *)target->flash; flash && success;
		 flash = (mspm0_flash_s *)flash->target_flash.next) {
		/* Assume banks are of same size */
		uint32_t bank_size = flash->target_flash.length / flash->banks;
		for (uint32_t bank = 0U; bank < flash->banks; ++bank) {
			uint32_t bank_offset = bank * bank_size;
			uint32_t bank_address = flash->target_flash.start + bank_offset;
			DEBUG_INFO("%s: Mass erase flash bank starting %08" PRIx32 " length %08" PRIx32 "\n", __func__,
				bank_address, bank_size);

			mspm0_flash_unprotect(&flash->target_flash);
			target_mem32_write32(
				target, MSPM0_FLASHCTL_CMDTYPE, MSPM0_FLASHCTL_CMDTYPE_SZ_BANK | MSPM0_FLASHCTL_CMDTYPE_ERASE);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDCTL, 0U);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDADDR, bank_address);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDEXEC, MSPM0_FLASHCTL_CMDEXEC_EXEC);

			uint32_t status = 0U;
			while (status & MSPM0_FLASHCTL_STAT_DONE) {
				status = target_mem32_read32(target, MSPM0_FLASHCTL_STATCMD);
				if (print_progess)
					target_print_progress(print_progess);
			}

			if (!(status & MSPM0_FLASHCTL_STAT_CMDPASS))
				DEBUG_TARGET("%s: Failed to mass erase flash, status %08" PRIx32 " start %08" PRIx32 " length "
							 "%08" PRIx32 "\n",
					__func__, status, bank_address, (uint32_t)bank_size);

			success &= (status & MSPM0_FLASHCTL_STAT_CMDPASS) == MSPM0_FLASHCTL_STAT_CMDPASS;
		}
	}

	return success;
}
